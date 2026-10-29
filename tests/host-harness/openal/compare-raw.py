#!/usr/bin/env python3
"""Compare two raw stereo mixes (almix.c's output, neon.sh).

Silence before the first sound is left out of both, and the part both
have is compared, so recordings that start or stop at slightly different
points (SDL's disk audio driver, which writes in real time) can be
compared too. Exit status 0 when it's the same, sample for sample.

Usage: compare-raw.py A B int16|float32"""
import struct
import sys

a_path, b_path, kind = sys.argv[1], sys.argv[2], sys.argv[3]
fmt, size = ("h", 2) if kind == "int16" else ("f", 4)


def samples(path):
    with open(path, "rb") as f:
        data = f.read()
    n = len(data) // size
    v = struct.unpack("<%d%s" % (n, fmt), data[:n * size])
    first = next((i for i, x in enumerate(v) if x != 0), len(v))
    return v[first - first % 2:]          # from the first sound, whole frames


va, vb = samples(a_path), samples(b_path)
n = min(len(va), len(vb))
diff = [abs(x - y) for x, y in zip(va[:n], vb[:n]) if x != y]
if not diff:
    print("  the same: %d samples compared (from the first sound)" % n)
    sys.exit(0)
print("  %d of %d samples differ, largest difference %s" % (len(diff), n, max(diff)))
sys.exit(1)
