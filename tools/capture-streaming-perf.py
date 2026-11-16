#!/usr/bin/env python3
"""Capture RAM performance counters from a matching Release ELF via OpenOCD."""
import argparse
import json
import shutil
import subprocess
import struct
import time
from pathlib import Path


def firmware_identity(path):
    image = path.read_bytes()
    if image[:6] != b'\x7fELF\x01\x01':
        raise RuntimeError('Expected a little-endian ELF32 firmware image')
    section_table = struct.unpack_from('<I', image, 0x20)[0]
    entry_size, count, names_index = struct.unpack_from('<HHH', image, 0x2E)
    names = struct.unpack_from('<I', image, section_table + names_index * entry_size + 0x10)[0]
    for index in range(count):
        entry = section_table + index * entry_size
        name_offset = struct.unpack_from('<I', image, entry)[0] + names
        name_end = image.index(b'\0', name_offset)
        if image[name_offset:name_end] == b'.firmware_info':
            address, offset, size = struct.unpack_from('<III', image, entry + 0x0C)
            return address, image[offset:offset + size].hex()
    raise RuntimeError('ELF has no firmware identity section')


def snapshot(args, begin):
    identity_address, identity_hex = firmware_identity(args.elf)
    # A finally block resumes even when symbol validation or a read fails.
    body = f'''
import json, time

def encode(value):
    if value.type.strip_typedefs().code == gdb.TYPE_CODE_STRUCT:
        return {{field.name: encode(value[field.name]) for field in value.type.fields()}}
    return int(value)

connected = False
try:
    gdb.execute('set remotetimeout 5')
    gdb.execute('target extended-remote {args.target}')
    connected = True
    gdb.execute('monitor halt')
    if int(gdb.parse_and_eval('streamingPerf.version')) != 2:
        raise gdb.GdbError('Expected streamingPerf version 2; flash the matching firmware')
    if {begin!r}:
        # Reading the build CRC/length header never executes a target algorithm.
        # OpenOCD CRC commands use RAM scratch and can overwrite live task stacks.
        identity = bytes(gdb.selected_inferior().read_memory({identity_address}, {len(identity_hex) // 2}))
        if identity.hex() != '{identity_hex}':
            raise gdb.GdbError('Running firmware identity does not match the supplied ELF')
        gdb.execute('set streamingPerf.captureEpoch = streamingPerf.captureEpoch + 1')
    print('PERF_JSON=' + json.dumps({{'host_time': time.monotonic(),
                                      'counters': encode(gdb.parse_and_eval('streamingPerf'))}}))
finally:
    if connected:
        gdb.execute('monitor resume')
        gdb.execute('disconnect')
'''
    result = subprocess.run(
        [args.gdb, '-nx', '--batch', str(args.elf), '-ex', 'python exec(' + repr(body) + ')'],
        text=True, capture_output=True, timeout=60,
    )
    for line in result.stdout.splitlines():
        if line.startswith('PERF_JSON='):
            return json.loads(line[len('PERF_JSON='):])
    raise RuntimeError(f'Counter capture failed:\n{result.stdout}\n{result.stderr}')


def summarize(before, after):
    elapsed = after['host_time'] - before['host_time']
    start, end = before['counters'], after['counters']
    hz = end['cpuHz']
    if hz <= 0:
        raise RuntimeError('Audio task has not initialized the CPU frequency')
    summary = {'elapsed_seconds': elapsed, 'metrics': {}, 'counter_deltas': {}}
    for name, value in end.items():
        if isinstance(value, dict):
            count = (value['count'] - start[name]['count']) & 0xFFFFFFFF
            cycles = (value['totalCycles'] - start[name]['totalCycles']) & 0xFFFFFFFFFFFFFFFF
            summary['metrics'][name] = {
                'count': count,
                'mean_ms': cycles * 1000 / hz / count if count else None,
                'window_max_ms': value['windowMaxCycles'] * 1000 / hz
                if count and value['epoch'] == end['captureEpoch'] else None,
                'elapsed_percent': cycles * 100 / hz / elapsed,
            }
        elif name not in ('version', 'cpuHz', 'captureEpoch', 'queueHighWater'):
            summary['counter_deltas'][name] = (value - start[name]) & 0xFFFFFFFF
    summary['queue_high_water_since_boot'] = end['queueHighWater']
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--target', default='localhost:3333')
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--gdb', default=shutil.which('arm-none-eabi-gdb') or 'gdb-multiarch')
    parser.add_argument('--output', type=Path, default=Path('streaming-perf.json'))
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('--seconds must be positive')
    # target is interpolated into a GDB command, never a shell command.
    if any(character in args.target for character in "\n\r'\\"):
        parser.error('invalid target address')
    before = snapshot(args, True)
    time.sleep(args.seconds)
    after = snapshot(args, False)
    report = {'before': before, 'after': after, 'summary': summarize(before, after)}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['summary'], indent=2))
    print(f'Saved snapshots: {args.output}')


if __name__ == '__main__':
    main()
