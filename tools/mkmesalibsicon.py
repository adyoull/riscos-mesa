#!/usr/bin/env python3
"""mkmesalibsicon.py - riscos-mesa: the icon of !MesaLibs (the shared
libraries for mixed programs), in the RISC OS style.

    mkmesalibsicon.py OUTDIR [PREVIEWDIR]

OUTDIR/!Sprites,ff9 (90 dpi) and OUTDIR/!Sprites11,ff9 (180 dpi), each
with "!mesalibs" (34 x 34) and "sm!mesalibs" (17 x 17): a mesa (a
flat-topped desert hill, in layers of rock) with a colour-shaded triangle
over it like a sun - red, green and blue corners blended across it, the
first thing an OpenGL program draws. An original drawing, not the Mesa 3D
project's logo. PREVIEWDIR: the PNGs too.

Drawn with tools/riscosdraw.py and made into sprites by
tools/png2sprite.py (both from riscos-gtk, whose !GTKLibs icon is made the
same way). Needs Pillow. MIT licence. Copyright (C) 2026 Andrew Youll.
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from riscosdraw import S, K, Icon  # noqa: E402

ROCK = (236, 140, 72, 255)       # sunlit rock
ROCK2 = (176, 84, 36, 255)       # rock in shade, low down
CAP = (250, 196, 120, 255)       # the flat top
CAP2 = (226, 150, 80, 255)
STRATA = (150, 70, 30, 255)      # the layers
GL_R = (232, 40, 40)             # the triangle's corners
GL_G = (40, 196, 64)
GL_B = (48, 104, 232)


def shaded_triangle(c, pts, cols):
    """a triangle with a colour at each corner, blended across it (as
    OpenGL's smooth shading does), then outlined"""
    (x0, y0), (x1, y1), (x2, y2) = [(x * S * c.k, y * S * c.k) for x, y in pts]
    det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
    tri = Image.new("RGBA", c.im.size, (0, 0, 0, 0))
    px = tri.load()
    xa, xb = int(min(x0, x1, x2)), int(max(x0, x1, x2)) + 1
    ya, yb = int(min(y0, y1, y2)), int(max(y0, y1, y2)) + 1
    for y in range(max(0, ya), min(c.im.size[1], yb)):
        for x in range(max(0, xa), min(c.im.size[0], xb)):
            fx, fy = x + 0.5, y + 0.5
            a = ((y1 - y2) * (fx - x2) + (x2 - x1) * (fy - y2)) / det
            b = ((y2 - y0) * (fx - x2) + (x0 - x2) * (fy - y2)) / det
            g = 1 - a - b
            if a >= 0 and b >= 0 and g >= 0:
                px[x, y] = tuple(int(round(a * cols[0][i] + b * cols[1][i] + g * cols[2][i]))
                                 for i in range(3)) + (255,)
    c.im.alpha_composite(tri)
    c.poly(pts, None, K, 1)


def mesa(c, top, cliff, base, ys, strata=True):
    """the hill: a flat top, steep cliffs, then a skirt of scree
    spreading to the ground. top/cliff/base: (left, right) x at the top,
    the foot of the cliffs and the ground; ys: their heights"""
    (t0, t1), (c0, c1), (b0, b1) = top, cliff, base
    ty, cy, by = ys
    c.poly([(t0, ty), (t1, ty), (c1, cy), (b1, by), (b0, by), (c0, cy)], (ROCK, ROCK2), K, 1)
    c.poly([(t0, ty), (t1, ty), (t1 + 0.2, ty + 1.6), (t0 - 0.2, ty + 1.6)], (CAP, CAP2), None)
    c.line([(t0, ty), (t1, ty)], K, 1)
    if strata:                       # bands of rock across the cliff face
        for f in (0.45, 0.78):
            y = ty + (cy - ty) * f
            c.line([(t0 + (c0 - t0) * f + 1.0, y), (t1 + (c1 - t1) * f - 1.0, y)], STRATA, 0.9)
        c.line([(c0 + 1.5, cy), (c1 - 1.5, cy)], STRATA, 0.9)


def large(k):
    c = Icon(34, 34, k)
    shaded_triangle(c, [(17.0, 1.5), (7.0, 19.5), (27.0, 19.5)], (GL_R, GL_G, GL_B))
    mesa(c, (7.5, 26.5), (6.0, 28.0), (0.5, 33.5), (17.5, 26.5, 32.5))
    return c.image()


def small(k):
    c = Icon(17, 17, k)
    shaded_triangle(c, [(8.5, 0.5), (3.5, 10.5), (13.5, 10.5)], (GL_R, GL_G, GL_B))
    mesa(c, (3.5, 13.5), (2.5, 14.5), (0.5, 16.5), (9.5, 13.5, 16.5), strata=False)
    return c.image()


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as t:
        for k, name in ((1, "!Sprites,ff9"), (2, "!Sprites11,ff9")):
            lp, sp = os.path.join(t, "l%d.png" % k), os.path.join(t, "s%d.png" % k)
            large(k).save(lp)
            small(k).save(sp)
            args = [sys.executable, os.path.join(HERE, "png2sprite.py")]
            if k == 2:
                args += ["--dpi", "180"]
            subprocess.check_call(args + [os.path.join(out, name), "!mesalibs=" + lp, "sm!mesalibs=" + sp])
            if len(sys.argv) > 2:
                os.makedirs(sys.argv[2], exist_ok=True)
                large(k).save(os.path.join(sys.argv[2], "mesalibs%d.png" % k))
                small(k).save(os.path.join(sys.argv[2], "smmesalibs%d.png" % k))


if __name__ == "__main__":
    main()
