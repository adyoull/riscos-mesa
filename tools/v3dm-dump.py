#!/usr/bin/env python3
"""Prints the jobs in a V3DM$Dump file (v3d/libdrm/src/drm_riscos_dump.c):
each SUBMIT_CL's fields and its buffers; -x DIR also writes each buffer's
contents to DIR/job<N>-bo<handle>-<gpu address>.bin.

  tools/v3dm-dump.py DUMPFILE [-x DIR]

Copyright (c) 2026 Andrew Youll. MIT licence."""
import os, struct, sys

FIELDS = ('bcl_start bcl_end rcl_start rcl_end in_sync_bcl in_sync_rcl out_sync '
          'qma qms qts').split()

def main():
    args = sys.argv[1:]
    out = None
    if '-x' in args:
        i = args.index('-x'); out = args[i + 1]; del args[i:i + 2]
        os.makedirs(out, exist_ok=True)
    d = open(args[0], 'rb').read()
    p = 0
    while p < len(d):
        if d[p:p + 8] != b'V3DMDMP1':
            sys.exit('bad record at %d' % p)
        job, size = struct.unpack_from('<II', d, p + 8); p += 16
        s = d[p:p + size]; p += size
        v = struct.unpack_from('<10I', s)
        handles, count, flags, perfmon = struct.unpack_from('<QIII', s, 40)
        print('job %d: %s' % (job, ' '.join('%s=%08x' % f for f in zip(FIELDS, v))))
        print('  %d buffers, flags %x, perfmon %d' % (count, flags, perfmon))
        (n,) = struct.unpack_from('<I', d, p); p += 4
        for _ in range(n):
            h, gpu, bsize, has = struct.unpack_from('<4I', d, p); p += 16
            what = []
            for name, a in (('bcl', v[0]), ('rcl', v[2]), ('tile alloc', v[7]), ('tile state', v[9])):
                if gpu <= a < gpu + bsize:
                    what.append(name)
            print('  bo %4d at %08x, %7d bytes%s%s' % (h, gpu, bsize, '' if has else ' (not mapped: no contents)',
                                                      ' <- ' + ', '.join(what) if what else ''))
            if has:
                if out:
                    open(os.path.join(out, 'job%d-bo%d-%08x.bin' % (job, h, gpu)), 'wb').write(d[p:p + bsize])
                p += bsize

if __name__ == '__main__':
    main()
