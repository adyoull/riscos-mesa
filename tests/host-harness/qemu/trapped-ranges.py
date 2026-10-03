#!/usr/bin/env python3
"""trapped-ranges.py MAP PATTERN... : the QEMU_ARM_ALIGN_IGNORE value for a
statically linked program, from its GNU ld link map (-Wl,-Map=...).

Code from input files matching a PATTERN (substrings, e.g. "libOSMesa.a("
and "check.o") keeps RISC OS's alignment rules; everything else (glibc,
libstdc++, libgcc: code that never runs on RISC OS, whose string
functions rely on unaligned loads) is listed as exempt.
"""
import re, sys

mapfile, patterns = sys.argv[1], sys.argv[2:]
text = open(mapfile).read()
text = text[text.find("Linker script and memory map"):]
# an input section: " .text[.name]  0xADDR  0xSIZE  file", the address
# sometimes on the next line when the section name is long
sect = re.compile(r"^ (\.text\S*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", re.M)
text = re.sub(r"^ (\.text\S*)\n\s+", r" \1 ", text, flags=re.M)
keep = []
for m in sect.finditer(text):
    addr, size, src = int(m.group(2), 16), int(m.group(3), 16), m.group(4)
    if size and addr and any(p in src for p in patterns):
        keep.append((addr, addr + size))
if not keep:
    sys.exit("no code from %s in %s" % (patterns, mapfile))
keep.sort()
merged = [list(keep[0])]
for lo, hi in keep[1:]:
    if lo <= merged[-1][1] + 64:        # small gaps (alignment padding)
        merged[-1][1] = max(merged[-1][1], hi)
    else:
        merged.append([lo, hi])
ignore, at = [], 0
for lo, hi in merged:
    if lo > at:
        ignore.append((at, lo))
    at = hi
ignore.append((at, 0xFFFFFFFF))
if len(ignore) > 4096:
    sys.exit("too many ranges (%d) for the patched qemu" % len(ignore))
print(",".join("%x-%x" % r for r in ignore))
