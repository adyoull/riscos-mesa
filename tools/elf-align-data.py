#!/usr/bin/env python3
"""elf-align-data.py OBJECT.o: raise to 4 the alignment of every writable
data section (.data*, .bss*) of 4 bytes or more that has less, in a 32-bit
little-endian ELF relocatable object, in place. The linker then pads
before them; nothing else changes.

Why: RISC OS 5 on a Pi runs with alignment exceptions on, and riscos-crossdev's
checks/check-data-align.py refuses libraries with such sections (where they
land depends on the link). The GPU library's ld -r object had one (Mesa
26's 9-byte "initialized" flags, byte-accessed, so harmless, but flagged).

Copyright (c) 2026 Andrew Youll. MIT licence."""
import struct, sys

SHF_WRITE, SHF_ALLOC = 1, 2
SHT_PROGBITS, SHT_NOBITS = 1, 8

def main(path):
    with open(path, 'r+b') as f:
        d = bytearray(f.read())
        if d[:4] != b'\x7fELF' or d[4] != 1 or d[5] != 1:
            sys.exit('%s: not a 32-bit little-endian ELF file' % path)
        shoff, = struct.unpack_from('<I', d, 0x20)
        shentsize, shnum = struct.unpack_from('<HH', d, 0x2E)
        raised = 0
        for i in range(shnum):
            o = shoff + i * shentsize
            _name, typ, flags, _addr, _off, size, _link, _info, align, _ent = struct.unpack_from('<10I', d, o)
            if typ in (SHT_PROGBITS, SHT_NOBITS) and flags & SHF_WRITE and flags & SHF_ALLOC \
                    and size >= 4 and align < 4:
                struct.pack_into('<I', d, o + 32, 4)
                raised += 1
        f.seek(0)
        f.write(d)
    print('%s: %d section(s) raised to 4-byte alignment' % (path, raised))

for p in sys.argv[1:]:
    main(p)
