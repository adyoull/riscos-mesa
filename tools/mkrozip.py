#!/usr/bin/env python3
"""mkrozip.py - make a zip that RISC OS unzips with real filetypes.

Files named NAME,xxx (xxx = 3 hex digits) are stored as NAME with an Acorn
extra field (header ID 0x4341 "AC", "ARC0" block: load address, exec
address, attributes), the form SparkFS and InfoZIP for RISC OS write and
read. The load/exec pair carries the filetype and the file's date in RISC
OS form. Files without a ,xxx suffix are stored as they are (untyped).

Usage: mkrozip.py OUT.zip DIR [DIR ...]   (paths are stored relative to
       each DIR's parent, so the top folder name is kept)
"""
import os, re, struct, sys, time, zipfile

SUFFIX = re.compile(r'^(.*),([0-9a-fA-F]{3})$')
EPOCH_OFFSET_CS = 2208988800 * 100  # 1900-01-01 to 1970-01-01, in centiseconds

def acorn_extra(filetype, mtime, attr=0x13):
    """Acorn/SparkFS extra field: 'AC' id, 20 bytes: ARC0, load, exec, attr, 0."""
    cs = int(mtime * 100) + EPOCH_OFFSET_CS          # RISC OS 5-byte time
    load = 0xFFF00000 | (filetype << 8) | ((cs >> 32) & 0xFF)
    exec_ = cs & 0xFFFFFFFF
    body = b'ARC0' + struct.pack('<IIII', load, exec_, attr, 0)
    return struct.pack('<HH', 0x4341, len(body)) + body

def add_tree(zf, top):
    base = os.path.dirname(os.path.abspath(top))
    for root, dirs, files in os.walk(top):
        dirs.sort(); files.sort()
        rel_root = os.path.relpath(root, base)
        zi = zipfile.ZipInfo(rel_root.replace(os.sep, '/') + '/', time.localtime(os.path.getmtime(root))[:6])
        zi.external_attr = 0o40755 << 16
        zf.writestr(zi, b'')
        for f in files:
            path = os.path.join(root, f)
            m = SUFFIX.match(f)
            name = m.group(1) if m else f
            arc = os.path.join(rel_root, name).replace(os.sep, '/')
            st = os.stat(path)
            zi = zipfile.ZipInfo(arc, time.localtime(st.st_mtime)[:6])
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100755 if os.access(path, os.X_OK) else 0o100644) << 16
            if m:
                zi.extra = acorn_extra(int(m.group(2), 16), st.st_mtime)
            with open(path, 'rb') as fh:
                zf.writestr(zi, fh.read())

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with zipfile.ZipFile(sys.argv[1], 'w') as zf:
        for d in sys.argv[2:]:
            add_tree(zf, d)

if __name__ == '__main__':
    main()
