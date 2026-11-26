#!/usr/bin/env python3
"""hotfuncs.py PROG HOTBLOCKS.txt [N] [--base OTHER.txt]: qemu's hotblocks
plugin output (every block: pc, tcount, icount, ecount; built with its limit
raised) added up by function, from PROG's symbol table (nm, both local and
global). Prints the N (default 40) functions with the most instructions run,
with their share. --base: another run's output, subtracted per function (to
see what one mode adds over another of the same length).

Copyright (c) 2026 Andrew Youll. MIT licence."""
import bisect, re, subprocess, sys

def symbols(prog):
    out = subprocess.run(['arm-linux-gnueabihf-nm', '-n', '--defined-only', prog],
                         capture_output=True, text=True, check=True).stdout
    addrs, names = [], []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'tTwW':
            addrs.append(int(p[0], 16) & ~1)
            names.append(p[2])
    return addrs, names

def load(path, addrs, names):
    per = {}
    total = 0
    for line in open(path):
        m = re.match(r'0x([0-9a-f]+), (\d+), (\d+), (\d+)', line)
        if not m:
            continue
        pc, insns, execs = int(m.group(1), 16), int(m.group(3)), int(m.group(4))
        i = bisect.bisect_right(addrs, pc) - 1
        name = names[i] if i >= 0 else '?'
        per[name] = per.get(name, 0) + insns * execs
        total += insns * execs
    return per, total

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    base = None
    if '--base' in sys.argv:
        base = sys.argv[sys.argv.index('--base') + 1]
        args.remove(base)
    prog, path = args[0], args[1]
    n = int(args[2]) if len(args) > 2 else 40
    addrs, names = symbols(prog)
    per, total = load(path, addrs, names)
    if base:
        bper, btotal = load(base, addrs, names)
        for k, v in bper.items():
            per[k] = per.get(k, 0) - v
        total -= btotal
        print('difference: %.1fM instructions' % (total / 1e6))
    else:
        print('total: %.1fM instructions' % (total / 1e6))
    for name, v in sorted(per.items(), key=lambda kv: -kv[1])[:n]:
        print('%10.2fM %6.1f%%  %s' % (v / 1e6, 100.0 * v / total if total else 0, name))

main()
