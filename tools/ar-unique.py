#!/usr/bin/env python3
"""Extract the members of a static library (GNU ar format) into a
directory, once each: a member that is a byte-for-byte copy of one already
extracted is skipped, and members that share a name but differ are kept
under numbered names. Prints the object files, one per line, in archive
order.

build/build-shared.sh links a shared library from these: Mesa's folded
libOSMesa.a holds some objects twice (meson's internal libraries overlap),
which a static link never notices (the linker takes each symbol from the
first member that has it) but --whole-archive would turn into duplicate
definitions.

Usage: ar-unique.py LIBRARY.a OUTDIR"""
import hashlib
import os
import sys

path, out = sys.argv[1], sys.argv[2]
with open(path, "rb") as f:
    data = f.read()
if not data.startswith(b"!<arch>\n"):
    sys.exit("%s: not an ar archive" % path)
os.makedirs(out, exist_ok=True)

pos, longnames, seen, used = 8, b"", set(), {}
while pos + 60 <= len(data):
    hdr = data[pos:pos + 60]
    name = hdr[:16].decode().rstrip()
    size = int(hdr[48:58].decode().strip())
    body = data[pos + 60:pos + 60 + size]
    pos += 60 + size + (size & 1)
    if name in ("/", "/SYM64/"):            # symbol table
        continue
    if name == "//":                         # long names table
        longnames = body
        continue
    if name.startswith("/"):                 # "/123": offset into the long names
        off = int(name[1:])
        name = longnames[off:longnames.index(b"/\n", off)].decode()
    else:
        name = name.rstrip("/")
    digest = hashlib.sha256(body).digest()
    if digest in seen:
        continue
    seen.add(digest)
    n = used.get(name, 0)
    used[name] = n + 1
    leaf = name if n == 0 else "%d-%s" % (n, name)
    dest = os.path.join(out, leaf)
    with open(dest, "wb") as f:
        f.write(body)
    print(dest)
