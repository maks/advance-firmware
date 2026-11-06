// SPDX-License-Identifier: BSD-3-Clause

// Minimal Emscripten-FS-like in-memory filesystem for seeding tests
export class MemFS {
  constructor() {
    this.nodes = new Map();
    this.nodes.set('/', { dir: true });
  }
  _mk(path) {
    const parts = path.split('/').filter(Boolean);
    let cur = '';
    for (const p of parts) {
      const full = cur + '/' + p;
      if (!this.nodes.has(full)) this.nodes.set(full, { dir: true });
      cur = full;
    }
  }
  mkdir(path) {
    if (this.nodes.has(path)) throw new Error('EEXIST ' + path);
    this._mk(path.substring(0, path.lastIndexOf('/')));
    this.nodes.set(path, { dir: true });
  }
  writeFile(path, data) {
    this._mk(path.substring(0, path.lastIndexOf('/')));
    this.nodes.set(path, { dir: false, data: typeof data === 'string' ? data : Buffer.from(data) });
  }
  readFile(path, opts = {}) {
    const n = this.nodes.get(path);
    if (!n || n.dir) throw new Error('ENOENT ' + path);
    if (opts.encoding === 'utf8') return Buffer.isBuffer(n.data) ? n.data.toString('utf8') : String(n.data);
    if (Buffer.isBuffer(n.data)) return new Uint8Array(n.data);
    return new Uint8Array(Buffer.from(String(n.data)));
  }
  stat(path) {
    const n = this.nodes.get(path);
    if (!n) throw new Error('ENOENT ' + path);
    return { mode: n.dir ? 0o040755 : 0o100644, size: n.dir ? 0 : String(n.data ?? '').length };
  }
  isDir(mode) { return (mode & 0o170000) === 0o040000; }
  readdir(path) {
    const prefix = path === '/' ? '/' : path + '/';
    const out = [];
    for (const key of this.nodes.keys()) {
      if (!key.startsWith(prefix) || key === path) continue;
      const rest = key.slice(prefix.length);
      if (!rest || rest.includes('/')) continue;
      out.push(rest);
    }
    return out;
  }
  analyzePath(path) {
    const exists = this.nodes.has(path);
    return { exists, isDir: exists ? this.nodes.get(path).dir : false };
  }
  unlink(path) { if (!this.nodes.delete(path)) throw new Error('ENOENT ' + path); }
  rmdir(path) { if (!this.nodes.delete(path)) throw new Error('ENOENT ' + path); }
  list() { return [...this.nodes.keys()].filter((k) => !this.nodes.get(k).dir).sort(); }
}
