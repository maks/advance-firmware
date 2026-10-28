#!/usr/bin/env python3
"""Read RAM counters from running firmware using a matching symbol ELF."""
import argparse
import shutil
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf', type=Path)
parser.add_argument('--target', default='localhost:3333')
parser.add_argument('--seconds', type=float, default=30)
parser.add_argument('--gdb', default=shutil.which('arm-none-eabi-gdb') or 'gdb-multiarch')
parser.add_argument('--output', type=Path, default=Path('streaming-perf.txt'))
args = parser.parse_args()
if args.seconds <= 0:
    parser.error('--seconds must be positive')
# Each attach briefly halts for a coherent snapshot, then explicitly resumes.
def snapshot():
    commands = ['set remotetimeout 5', f'target extended-remote {args.target}',
                'set pagination off', 'p streamingPerf',
                'monitor resume', 'disconnect', 'quit']
    command = [args.gdb, '-nx', '--batch', str(args.elf)]
    for entry in commands:
        command.extend(['-ex', entry])
    return subprocess.run(command, text=True, capture_output=True, check=True).stdout
start = snapshot()
time.sleep(args.seconds)
end = snapshot()
args.output.write_text(f'Elapsed capture interval: {args.seconds} seconds\nBEFORE\n{start}\nAFTER\n{end}')
print(args.output.read_text())
