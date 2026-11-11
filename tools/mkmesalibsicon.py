#!/usr/bin/env python3
"""mkmesalibsicon.py - riscos-mesa: the icon of !MesaLibs (the shared
libraries for mixed programs), in the RISC OS style.

    mkmesalibsicon.py OUTDIR [PREVIEWDIR]

OUTDIR/!Sprites,ff9 (90 dpi) and OUTDIR/!Sprites11,ff9 (180 dpi), each
with "!mesalibs" (34 x 34) and "sm!mesalibs" (17 x 17): three meshing
gears, red, green and blue, after the gears demo that has come with Mesa
since its early days. An original drawing. PREVIEWDIR: the PNGs too.

Drawn with tools/riscosdraw.py and made into sprites by
tools/png2sprite.py (both from riscos-gtk, whose !GTKLibs icon is made the
same way). Needs Pillow. MIT licence. Copyright (C) 2026 Andrew Youll.
"""
import math
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from riscosdraw import K, Icon  # noqa: E402

# each gear lit from the top: (light, dark)
RED = ((240, 72, 40, 255), (168, 20, 0, 255))
GREEN = ((72, 214, 96, 255), (0, 136, 40, 255))
BLUE = ((96, 128, 255, 255), (32, 48, 200, 255))


def gear(c, cx, cy, r_out, r_in, teeth, fill, phase=0.0, hub=0.32):
    """a spur gear: teeth round the rim (each a trapezium, narrower at its
    tip), outlined, with a hole through the middle"""
    pts = []
    step = 2 * math.pi / teeth
    for i in range(teeth):
        a = phase + i * step
        # root, flank up, tip, flank down: tooth 45% of the pitch at the
        # root, 25% at the tip
        for da, r in ((-0.27, r_in), (-0.13, r_out), (0.13, r_out), (0.27, r_in)):
            pts.append((cx + r * math.cos(a + da * step * 2), cy + r * math.sin(a + da * step * 2)))
    c.poly(pts, fill, K, 1)
    h = r_out * hub
    c.erase(ellipse=(cx - h, cy - h, cx + h, cy + h))
    c.ellipse(cx - h, cy - h, cx + h, cy + h, None, K, 1)


def large(k):
    c = Icon(34, 34, k)
    gear(c, 22.5, 8.0, 6.5, 5.0, 9, BLUE, phase=0.10)
    gear(c, 26.0, 24.0, 7.0, 5.4, 10, GREEN, phase=0.30)
    gear(c, 12.0, 20.0, 10.5, 8.6, 13, RED)
    return c.image()


def small(k):
    c = Icon(17, 17, k)
    gear(c, 12.0, 4.0, 3.6, 2.6, 7, BLUE, phase=0.2, hub=0.0)
    gear(c, 13.3, 12.6, 3.6, 2.6, 7, GREEN, phase=0.4, hub=0.0)
    gear(c, 6.0, 10.0, 5.6, 4.4, 9, RED, hub=0.3)
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
