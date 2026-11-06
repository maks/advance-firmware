// SPDX-License-Identifier: BSD-3-Clause

// Standard CRC-32 lookup table
const crcTable = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    }
    table[n] = c >>> 0;
  }
  return table;
})();

export function crc32(buf) {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc = (crc >>> 8) ^ crcTable[(crc ^ buf[i]) & 0xFF];
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

async function compressDeflateRaw(data) {
  if (typeof CompressionStream !== 'function') {
    return null; // Fallback to store
  }
  try {
    const cs = new CompressionStream('deflate-raw');
    const writer = cs.writable.getWriter();
    writer.write(data);
    writer.close();
    const reader = cs.readable.getReader();
    const chunks = [];
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
    }
    const total = chunks.reduce((acc, c) => acc + c.length, 0);
    const res = new Uint8Array(total);
    let off = 0;
    for (const c of chunks) {
      res.set(c, off);
      off += c.length;
    }
    return res;
  } catch (err) {
    console.warn('[zip] Deflate compression failed, falling back to Store:', err);
    return null;
  }
}

async function decompressDeflateRaw(data) {
  if (typeof DecompressionStream !== 'function') {
    throw new Error('DecompressionStream is not supported in this browser');
  }
  const ds = new DecompressionStream('deflate-raw');
  const writer = ds.writable.getWriter();
  writer.write(data);
  writer.close();
  const reader = ds.readable.getReader();
  const chunks = [];
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
  }
  const total = chunks.reduce((acc, c) => acc + c.length, 0);
  const res = new Uint8Array(total);
  let off = 0;
  for (const c of chunks) {
    res.set(c, off);
    off += c.length;
  }
  return res;
}

/**
 * Creates a valid PKZIP archive from an array of file entries.
 * @param {Array<{ path: string, data: Uint8Array|string }>} entries
 * @returns {Promise<Uint8Array>}
 */
export async function createZip(entries) {
  const encoder = new TextEncoder();
  const parts = [];
  const cdParts = [];
  let offset = 0;

  for (const entry of entries) {
    let cleanPath = entry.path.replace(/\\/g, '/');
    while (cleanPath.startsWith('/')) cleanPath = cleanPath.slice(1);
    if (!cleanPath) continue;

    const nameBytes = encoder.encode(cleanPath);
    const uncompressedData = entry.data instanceof Uint8Array
      ? entry.data
      : encoder.encode(String(entry.data));
    const crc = crc32(uncompressedData);

    let compressedData = await compressDeflateRaw(uncompressedData);
    let method = 8;
    if (!compressedData || compressedData.length >= uncompressedData.length) {
      compressedData = uncompressedData;
      method = 0;
    }

    // Local file header (30 bytes + name)
    const localHeader = new Uint8Array(30 + nameBytes.length);
    const lv = new DataView(localHeader.buffer);
    lv.setUint32(0, 0x04034b50, true);
    lv.setUint16(4, 20, true); // Version needed
    lv.setUint16(6, 0x0800, true); // General purpose flag: UTF-8 filenames
    lv.setUint16(8, method, true);
    lv.setUint16(10, 0, true); // Time
    lv.setUint16(12, 0, true); // Date
    lv.setUint32(14, crc, true);
    lv.setUint32(18, compressedData.length, true);
    lv.setUint32(22, uncompressedData.length, true);
    lv.setUint16(26, nameBytes.length, true);
    lv.setUint16(28, 0, true); // Extra length
    localHeader.set(nameBytes, 30);

    parts.push(localHeader);
    parts.push(compressedData);

    // Central directory header (46 bytes + name)
    const cdHeader = new Uint8Array(46 + nameBytes.length);
    const cv = new DataView(cdHeader.buffer);
    cv.setUint32(0, 0x02014b50, true);
    cv.setUint16(4, 20, true); // Version made by
    cv.setUint16(6, 20, true); // Version needed
    cv.setUint16(8, 0x0800, true); // UTF-8 flag
    cv.setUint16(10, method, true);
    cv.setUint16(12, 0, true);
    cv.setUint16(14, 0, true);
    cv.setUint32(16, crc, true);
    cv.setUint32(20, compressedData.length, true);
    cv.setUint32(24, uncompressedData.length, true);
    cv.setUint16(28, nameBytes.length, true);
    cv.setUint16(30, 0, true);
    cv.setUint16(32, 0, true);
    cv.setUint16(34, 0, true);
    cv.setUint16(36, 0, true);
    cv.setUint32(38, 0, true);
    cv.setUint32(42, offset, true); // Local header offset
    cdHeader.set(nameBytes, 46);

    cdParts.push(cdHeader);
    offset += localHeader.length + compressedData.length;
  }

  const cdOffset = offset;
  let cdSize = 0;
  for (const c of cdParts) {
    cdSize += c.length;
  }

  // End of Central Directory (22 bytes)
  const eocd = new Uint8Array(22);
  const ev = new DataView(eocd.buffer);
  ev.setUint32(0, 0x06054b50, true);
  ev.setUint16(4, 0, true);
  ev.setUint16(6, 0, true);
  ev.setUint16(8, cdParts.length, true);
  ev.setUint16(10, cdParts.length, true);
  ev.setUint32(12, cdSize, true);
  ev.setUint32(16, cdOffset, true);
  ev.setUint16(20, 0, true);

  const totalLength = cdOffset + cdSize + 22;
  const result = new Uint8Array(totalLength);
  let writePos = 0;
  for (const p of parts) {
    result.set(p, writePos);
    writePos += p.length;
  }
  for (const c of cdParts) {
    result.set(c, writePos);
    writePos += c.length;
  }
  result.set(eocd, writePos);
  return result;
}

/**
 * Parses and extracts a PKZIP archive.
 * Validates path security against directory traversal attacks.
 * @param {ArrayBuffer|Uint8Array} buffer
 * @returns {Promise<Array<{ path: string, data: Uint8Array }>>}
 */
export async function readZip(buffer) {
  const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const decoder = new TextDecoder();

  // Search backwards for EOCD record signature
  let eocdOffset = -1;
  const maxSearch = Math.min(bytes.length, 65536 + 22);
  for (let i = bytes.length - 22; i >= bytes.length - maxSearch; i--) {
    if (view.getUint32(i, true) === 0x06054b50) {
      eocdOffset = i;
      break;
    }
  }
  if (eocdOffset === -1) {
    throw new Error('Invalid ZIP archive: End of Central Directory not found');
  }

  const numEntries = view.getUint16(eocdOffset + 10, true);
  const cdOffset = view.getUint32(eocdOffset + 16, true);

  const entries = [];
  let cdPos = cdOffset;

  for (let i = 0; i < numEntries; i++) {
    if (view.getUint32(cdPos, true) !== 0x02014b50) {
      throw new Error(`Invalid Central Directory header at offset ${cdPos}`);
    }
    const method = view.getUint16(cdPos + 10, true);
    const crc = view.getUint32(cdPos + 16, true);
    const compressedSize = view.getUint32(cdPos + 20, true);
    const uncompressedSize = view.getUint32(cdPos + 24, true);
    const nameLen = view.getUint16(cdPos + 28, true);
    const extraLen = view.getUint16(cdPos + 30, true);
    const commentLen = view.getUint16(cdPos + 32, true);
    const localHeaderOffset = view.getUint32(cdPos + 42, true);

    const rawName = decoder.decode(bytes.subarray(cdPos + 46, cdPos + 46 + nameLen));
    cdPos += 46 + nameLen + extraLen + commentLen;

    // Sanitize path: normalize slashes, strip leading slash, reject '..'
    let safePath = rawName.replace(/\\/g, '/');
    while (safePath.startsWith('/')) safePath = safePath.slice(1);
    const segments = safePath.split('/');
    if (segments.some((s) => s === '..')) {
      throw new Error(`Unsafe path in ZIP archive: ${rawName}`);
    }

    // Skip directory entries
    if (safePath.endsWith('/') || safePath === '') {
      continue;
    }

    if (view.getUint32(localHeaderOffset, true) !== 0x04034b50) {
      throw new Error(`Invalid Local File Header at offset ${localHeaderOffset}`);
    }
    const localNameLen = view.getUint16(localHeaderOffset + 26, true);
    const localExtraLen = view.getUint16(localHeaderOffset + 28, true);
    const dataOffset = localHeaderOffset + 30 + localNameLen + localExtraLen;
    const rawData = bytes.subarray(dataOffset, dataOffset + compressedSize);

    let data;
    if (method === 0) {
      data = new Uint8Array(rawData);
    } else if (method === 8) {
      data = await decompressDeflateRaw(rawData);
    } else {
      throw new Error(`Unsupported ZIP compression method: ${method}`);
    }

    if (data.length !== uncompressedSize) {
      console.warn(`[zip] Size mismatch for ${safePath}: expected ${uncompressedSize}, got ${data.length}`);
    }

    entries.push({ path: safePath, data });
  }

  return entries;
}
