"""riscosdraw.py - (from riscos-gtk, unchanged apart from this note) drawing icons in the style of the RISC OS
desktop, shared by the icon tools (tools/mkclawsicons.py: ClawsMail's
icons; tools/mkriscosicons.py: the RISCOS icon theme every program has).

Original icons: dark outlines, flat colours lit from the top left, the
Wimp's palette; drawn as vectors S times bigger and reduced, with
outlines on pixel centres (n + 0.5) so they stay sharp.

  c = Icon(w, h, k)      a w x h drawing, made k times bigger (k may be a
                         fraction: a 24-unit drawing at 16 pixels is k 2/3)
  c.poly / line / rect / ellipse / arc, then c.image()
  the shapes below (envelope, sheet, arrow, folder, printer...) draw on c
  in its units.

Needs Pillow. MIT licence. Copyright (C) 2026 Andrew Youll.
"""
import math
from PIL import Image, ImageDraw

S = 8  # supersampling

# the palette: RISC OS's Wimp colours and the tones of its 32bpp icons
K = (32, 32, 32, 255)          # outline
K2 = (96, 96, 96, 255)         # light outline / detail
WHITE = (255, 255, 255, 255)
PAPER = (255, 255, 250, 255)
PAPER2 = (226, 222, 204, 255)
CREAM = (255, 255, 204, 255)
FLAP = (236, 232, 212, 255)
FLAP2 = (204, 198, 172, 255)
GREY = (204, 204, 204, 255)
GREY2 = (153, 153, 153, 255)
GREY3 = (110, 110, 110, 255)
RED = (230, 34, 34, 255)
RED2 = (170, 0, 0, 255)
GREEN = (60, 200, 60, 255)
GREEN2 = (0, 136, 0, 255)
BLUE = (64, 136, 238, 255)
BLUE2 = (0, 68, 170, 255)
LBLUE = (170, 210, 255, 255)
YELLOW = (255, 221, 0, 255)
YELLOW2 = (204, 153, 0, 255)
ORANGE = (255, 153, 0, 255)
ORANGE2 = (204, 85, 0, 255)
BROWN = (180, 120, 60, 255)
BROWN2 = (120, 70, 20, 255)
GOLD = (255, 204, 51, 255)
GOLD2 = (190, 130, 0, 255)
PINK = (255, 150, 170, 255)
PINK2 = (200, 60, 90, 255)
FOLDER = (255, 214, 102, 255)
FOLDER2 = (214, 154, 30, 255)
FOLDERIN = (255, 236, 170, 255)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(4))


class Icon:
    def __init__(self, w, h, k=1, minline=False):
        """w x h pixels of drawing, made k times bigger (the logos);
        minline: lines at least as wide as at k = 1 when k < 1 (a 24-unit
        drawing shown at 16 pixels keeps 1-pixel outlines)"""
        self.w, self.h, self.k = w, h, k
        self.lk = max(k, 1) if minline else k
        self.im = Image.new("RGBA", (int(round(w * k * S)), int(round(h * k * S))), (0, 0, 0, 0))

    def _p(self, pts):
        return [(x * S * self.k, y * S * self.k) for x, y in pts]

    def _layer(self, mask, fill):
        """fill (a colour or (top, bottom) gradient) through mask"""
        if isinstance(fill[0], tuple):
            top, bot = fill
            box = mask.getbbox()
            if not box:
                return
            y0, y1 = box[1], box[3]
            g = Image.new("RGBA", self.im.size)
            d = ImageDraw.Draw(g)
            for y in range(y0, y1):
                d.line([(0, y), (self.im.size[0], y)], fill=lerp(top, bot, (y - y0) / max(1, y1 - y0 - 1)))
        else:
            g = Image.new("RGBA", self.im.size, fill)
        a = Image.new("L", self.im.size, 0)
        a.paste(g.getchannel("A"), mask=mask)
        g.putalpha(a)
        self.im.alpha_composite(g)

    def poly(self, pts, fill=None, outline=K, ow=1.0, closed=True):
        if fill:
            m = Image.new("L", self.im.size, 0)
            ImageDraw.Draw(m).polygon(self._p(pts), fill=255)
            self._layer(m, fill)
        if outline:
            q = self._p(pts + ([pts[0]] if closed else []))
            l = Image.new("RGBA", self.im.size)
            ImageDraw.Draw(l).line(q, fill=outline, width=int(ow * S * self.lk), joint="curve")
            for x, y in q:
                r = ow * S * self.lk / 2
                ImageDraw.Draw(l).ellipse([x - r, y - r, x + r, y + r], fill=outline)
            self.im.alpha_composite(l)

    def line(self, pts, col=K, w=1.0):
        self.poly(pts, None, col, w, closed=False)

    def rect(self, x0, y0, x1, y1, fill=None, outline=K, ow=1.0):
        self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], fill, outline, ow)

    def ellipse(self, x0, y0, x1, y1, fill=None, outline=K, ow=1.0):
        n = 48
        cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
        pts = [(cx + rx * math.cos(2 * math.pi * i / n), cy + ry * math.sin(2 * math.pi * i / n)) for i in range(n)]
        self.poly(pts, fill, outline, ow)

    def arc(self, cx, cy, r, a0, a1, col=K, w=1.0, n=24):
        pts = [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
                cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
        self.line(pts, col, w)

    def erase(self, pts=None, line=None, w=1.0, ellipse=None):
        """make transparent: inside the polygon pts, along the line (width
        w) or inside the ellipse (x0, y0, x1, y1): cut-outs in one-colour
        (symbolic) icons"""
        m = Image.new("L", self.im.size, 0)
        d = ImageDraw.Draw(m)
        if pts:
            d.polygon(self._p(pts), fill=255)
        if line:
            q = self._p(line)
            d.line(q, fill=255, width=int(w * S * self.lk), joint="curve")
            for x, y in q:
                r = w * S * self.lk / 2
                d.ellipse([x - r, y - r, x + r, y + r], fill=255)
        if ellipse:
            x0, y0, x1, y1 = ellipse
            d.ellipse(self._p([(x0, y0), (x1, y1)]), fill=255)
        a = self.im.getchannel("A")
        from PIL import ImageChops
        self.im.putalpha(ImageChops.subtract(a, m))

    def text(self, x, y, s, size, font, fill=K, anchor=None):
        """s in the TrueType font (a path), its top left at (x, y), size
        units high; anchor "mm": centred on (x, y)"""
        from PIL import ImageFont
        f = ImageFont.truetype(font, int(size * S * self.k))
        l = Image.new("RGBA", self.im.size)
        ImageDraw.Draw(l).text((x * S * self.k, y * S * self.k), s, font=f, fill=fill, anchor=anchor)
        self.im.alpha_composite(l)

    def image(self):
        return self.im.reduce(S)


# ---- shapes, in pixel coordinates (outlines on pixel centres: n + 0.5) ----

def envelope(c, x0, y0, x1, y1, open_=False):
    """an envelope, back flap closed (a V) or open (a letter showing)"""
    mx = (x0 + x1) / 2
    if open_:
        c.poly([(x0, y0 + 4), (mx, y0 - 2), (x1, y0 + 4)], (FLAP, FLAP2))
        c.rect(x0 + 2, y0 - 1, x1 - 2, y1 - 3, PAPER, K2)
        for i in range(3):
            c.line([(x0 + 4, y0 + 1 + 2 * i), (x1 - 4, y0 + 1 + 2 * i)], GREY2, 1)
        c.poly([(x0, y0 + 4), (mx, y0 + (y1 - y0) * 0.65), (x1, y0 + 4), (x1, y1), (x0, y1)], (PAPER, PAPER2))
        c.line([(x0, y1), (mx - 1, y0 + (y1 - y0) * 0.55)], FLAP2, 1)
        c.line([(x1, y1), (mx + 1, y0 + (y1 - y0) * 0.55)], FLAP2, 1)
    else:
        c.rect(x0, y0, x1, y1, (PAPER, PAPER2))
        c.poly([(x0, y0), (mx, y0 + (y1 - y0) * 0.6), (x1, y0)], (FLAP, FLAP2))
        c.line([(x0, y1), (mx - 2, y0 + (y1 - y0) * 0.5)], FLAP2, 1)
        c.line([(x1, y1), (mx + 2, y0 + (y1 - y0) * 0.5)], FLAP2, 1)


def sheet(c, x0, y0, x1, y1, lines=True, fold=4):
    c.poly([(x0, y0), (x1 - fold, y0), (x1, y0 + fold), (x1, y1), (x0, y1)], (WHITE, PAPER2))
    c.poly([(x1 - fold, y0), (x1 - fold, y0 + fold), (x1, y0 + fold)], GREY, K)
    if lines:
        y = y0 + fold + 2
        while y < y1 - 1:
            c.line([(x0 + 2, y), (x1 - 2 - (3 if int(y) % 4 == 1 else 0), y)], GREY2, 1)
            y += 2


def arrow(c, x0, y0, x1, y1, direction, fill):
    """a fat block arrow in the box; direction 'up', 'down', 'left', 'right'"""
    w, h = x1 - x0, y1 - y0
    if direction in ("down", "up"):
        sh = w * 0.25
        hy = y0 + h * 0.5
        pts = [(x0 + sh, y0), (x1 - sh, y0), (x1 - sh, hy), (x1, hy), ((x0 + x1) / 2, y1), (x0, hy), (x0 + sh, hy)]
        if direction == "up":
            pts = [(x, y0 + y1 - y) for x, y in pts]
    else:
        sh = h * 0.25
        hx = x0 + w * 0.5
        pts = [(x0, y0 + sh), (hx, y0 + sh), (hx, y0), (x1, (y0 + y1) / 2), (hx, y1), (hx, y1 - sh), (x0, y1 - sh)]
        if direction == "left":
            pts = [(x0 + x1 - x, y) for x, y in pts]
    c.poly(pts, fill)


def curved_arrow(c, cx, cy, r, left, fill, thick=3.2):
    """a curling arrow (reply: points left, forward: right) round (cx, cy)"""
    n = 14
    s = -1 if left else 1
    outer, inner = [], []
    for i in range(n + 1):
        a = math.radians(180 + 180 * i / n) if not left else math.radians(-180 * i / n)
        outer.append((cx + (r + thick / 2) * math.cos(a), cy + (r + thick / 2) * math.sin(a)))
        inner.append((cx + (r - thick / 2) * math.cos(a), cy + (r - thick / 2) * math.sin(a)))
    # the shaft from one end round, then the head at the other end (pointing down)
    end = outer[-1], inner[-1]
    ex = (end[0][0] + end[1][0]) / 2
    ey = end[0][1]
    head = [(ex - s * 0 + 3.2, ey), (ex, ey + 3.6), (ex - 3.2, ey)]
    pts = outer + [head[0] if not left else head[2]] + [head[1]] + [head[2] if not left else head[0]] + inner[::-1]
    c.poly(pts, fill)


def pencil(c, x0, y0, x1, y1, body=(YELLOW, YELLOW2)):
    """a pencil from its tip (x0, y0) to the end (x1, y1), 3 px thick"""
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = -uy * 1.7, ux * 1.7
    t = 4.0
    bx, by = x0 + ux * t, y0 + uy * t
    c.poly([(bx + nx, by + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (bx - nx, by - ny)], body)
    c.poly([(x0, y0), (bx + nx, by + ny), (bx - nx, by - ny)], (PAPER, FLAP2))
    c.poly([(x0, y0), (x0 + ux * 1.6 + nx * 0.4, y0 + uy * 1.6 + ny * 0.4),
            (x0 + ux * 1.6 - nx * 0.4, y0 + uy * 1.6 - ny * 0.4)], K, K, 0.6)
    ex, ey = x1 - ux * 2, y1 - uy * 2
    c.poly([(ex + nx, ey + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (ex - nx, ey - ny)], PINK, K)


def padlock(c, x0, y0, x1, y1, closed=True):
    w = x1 - x0
    by = y0 + (y1 - y0) * 0.45
    cx = (x0 + x1) / 2
    r = w * 0.28
    sx = cx if closed else cx + w * 0.42
    sy = by if closed else by - 2.5
    pts = [(sx - r, by)] + [(sx + r * math.cos(math.radians(a)), sy - r * 0.2 + r * math.sin(math.radians(a)))
                             for a in range(180, 361, 15)] + [(sx + r, by if closed else sy + 1)]
    pts[0] = (sx - r, by + 0.5)
    c.line(pts, K, 3.4)
    c.line(pts, (200, 200, 200, 255), 1.4)
    c.rect(x0, by, x1, y1, (GOLD, GOLD2))
    c.ellipse(cx - 1.3, by + 1.8, cx + 1.3, by + 4.4, K, None)
    c.line([(cx, by + 4), (cx, y1 - 1.6)], K, 1)


def clock(c, cx, cy, r):
    c.ellipse(cx - r, cy - r, cx + r, cy + r, (WHITE, GREY))
    c.line([(cx, cy), (cx, cy - r * 0.65)], K, 1)
    c.line([(cx, cy), (cx + r * 0.55, cy + r * 0.2)], K, 1)


def tick(c, x0, y0, x1, y1, col=GREEN):
    w, h = x1 - x0, y1 - y0
    pts = [(x0, y0 + h * 0.55), (x0 + w * 0.38, y1), (x1, y0)]
    c.line(pts, K, 3.4)
    c.line(pts, col, 1.6)


def cross(c, x0, y0, x1, y1, col=RED):
    for p in ([(x0, y0), (x1, y1)], [(x1, y0), (x0, y1)]):
        c.line(p, K, 3.4)
    for p in ([(x0, y0), (x1, y1)], [(x1, y0), (x0, y1)]):
        c.line(p, col, 1.6)


def badge(c, cx, cy, r, fill, symbol):
    """a round badge with a white symbol: '!' 'i' 'x' '?'"""
    c.ellipse(cx - r, cy - r, cx + r, cy + r, fill)
    if symbol == "!":
        c.line([(cx, cy - r * 0.55), (cx, cy + r * 0.15)], WHITE, 2)
        c.ellipse(cx - 1, cy + r * 0.35, cx + 1, cy + r * 0.35 + 2, WHITE, None)
    elif symbol == "i":
        c.ellipse(cx - 1, cy - r * 0.6, cx + 1, cy - r * 0.6 + 2, WHITE, None)
        c.line([(cx, cy - r * 0.15), (cx, cy + r * 0.55)], WHITE, 2)
    elif symbol == "x":
        d = r * 0.42
        c.line([(cx - d, cy - d), (cx + d, cy + d)], WHITE, 2)
        c.line([(cx + d, cy - d), (cx - d, cy + d)], WHITE, 2)
    elif symbol == "?":
        c.arc(cx, cy - r * 0.25, r * 0.32, 200, 400, WHITE, 1.8)
        c.line([(cx + r * 0.1, cy), (cx, cy + r * 0.25)], WHITE, 1.8)
        c.ellipse(cx - 1, cy + r * 0.45, cx + 1, cy + r * 0.45 + 2, WHITE, None)


def triangle_warn(c, cx, cy, r):
    c.poly([(cx, cy - r), (cx + r * 1.05, cy + r * 0.8), (cx - r * 1.05, cy + r * 0.8)], (YELLOW, ORANGE))
    c.line([(cx, cy - r * 0.4), (cx, cy + r * 0.2)], K, 2)
    c.ellipse(cx - 1, cy + r * 0.38, cx + 1, cy + r * 0.38 + 2, K, None)


def seal(c, cx, cy, r, fill=(RED, RED2)):
    n = 16
    pts = []
    for i in range(n * 2):
        a = math.pi * i / n
        rr = r if i % 2 == 0 else r * 0.82
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    c.poly([(cx - r * 0.5, cy + r * 0.5), (cx - r * 0.8, cy + r * 1.5), (cx - r * 0.2, cy + r * 1.2),
            (cx, cy + r * 1.6), (cx + r * 0.2, cy + r * 0.6)], (BLUE, BLUE2))
    c.poly(pts, fill)
    c.ellipse(cx - r * 0.5, cy - r * 0.5, cx + r * 0.5, cy + r * 0.5, None, (255, 220, 220, 255), 1)


def wastebasket(c, x0, y0, x1, y1):
    """RISC OS's wastebasket: a tapered grey bin with ribs"""
    w = x1 - x0
    c.poly([(x0, y0 + 2), (x1, y0 + 2), (x1 - w * 0.15, y1), (x0 + w * 0.15, y1)], (WHITE, GREY2))
    c.ellipse(x0, y0, x1, y0 + 4, (GREY, GREY2))
    for i in range(1, 4):
        x = x0 + w * i / 4
        c.line([(x, y0 + 4.5), (x0 + w * 0.15 + (w * 0.7) * i / 4, y1 - 1)], GREY3, 1)


def gear(c, cx, cy, r, fill=(GREY, GREY3)):
    n = 8
    pts = []
    for i in range(n * 4):
        a = 2 * math.pi * i / (n * 4)
        rr = r if (i % 4) in (0, 1) else r * 0.72
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    c.poly(pts, fill)
    c.ellipse(cx - r * 0.3, cy - r * 0.3, cx + r * 0.3, cy + r * 0.3, WHITE)


def folder(c, x0, y0, x1, y1, open_=False):
    """a desktop directory, as the Filer draws it (fallback for small_dir)"""
    tab = (x1 - x0) * 0.4
    c.poly([(x0, y0 + 1), (x0 + 1, y0), (x0 + tab, y0), (x0 + tab + 2, y0 + 2), (x1, y0 + 2), (x1, y1), (x0, y1)],
           (FOLDER, FOLDER2))
    if open_:
        c.poly([(x0 + 2, y0 + 5), (x1 + 1, y0 + 5), (x1 - 1, y1), (x0, y1)], (FOLDERIN, FOLDER))
    else:
        c.line([(x0 + 1, y0 + 4), (x1 - 1, y0 + 4)], FOLDERIN, 1)


def person(c, cx, cy, s):
    c.ellipse(cx - s * 0.35, cy - s, cx + s * 0.35, cy - s * 0.3, (CREAM, (240, 200, 150, 255)))
    c.poly([(cx - s * 0.65, cy + s * 0.5), (cx - s * 0.5, cy - s * 0.15), (cx + s * 0.5, cy - s * 0.15),
            (cx + s * 0.65, cy + s * 0.5)], (BLUE, BLUE2))


def printer(c, x0, y0, x1, y1):
    w = x1 - x0
    c.rect(x0 + w * 0.22, y0, x1 - w * 0.22, y0 + 6, WHITE)
    c.poly([(x0, y0 + 5), (x1, y0 + 5), (x1, y1 - 3), (x0, y1 - 3)], (GREY, GREY3))
    c.rect(x0 + w * 0.18, y1 - 6, x1 - w * 0.18, y1, (WHITE, PAPER2))
    c.line([(x0 + w * 0.3, y1 - 3.5), (x1 - w * 0.3, y1 - 3.5)], GREY2, 1)
    c.ellipse(x1 - 4, y0 + 7, x1 - 2, y0 + 9, GREEN, None)


def paperclip(c, x0, y0, x1, y1, col=GREY3):
    w = x1 - x0
    r = w / 2
    cx = x0 + r
    pts = []
    # a three-loop clip, drawn as one stroked path
    pts += [(cx + r * 0.55, y1 - r * 1.2), (cx + r * 0.55, y0 + r)]
    for i in range(13):
        a = math.radians(i * 15)
        pts.append((cx + r * math.cos(a), y0 + r - r * math.sin(a)))
    pts += [(cx - r, y1 - r)]
    for i in range(13):
        a = math.radians(180 + i * 15)
        pts.append((cx + r * 0.8 * math.cos(a), y1 - r - r * 0.8 * math.sin(a)))
    pts += [(cx + r * 0.8, y0 + r * 1.6)]
    c.line(pts, K, 2.6)
    c.line(pts, col, 1.0)


def book(c, x0, y0, x1, y1, fill=(RED, RED2)):
    c.rect(x0, y0, x1, y1, fill)
    c.rect(x0 + 2, y1 - 3, x1, y1, PAPER, K)
    c.line([(x0 + 2, y0), (x0 + 2, y1 - 3)], K, 1)


def thread_lines(c, x0, y0, x1, y1):
    """a thread: a message and two replies under it, joined by a tree line"""
    h = (y1 - y0) / 3
    for i, ind in enumerate((0, 4, 4)):
        yy = y0 + i * h
        c.rect(x0 + ind, yy, x1, yy + h - 2, (PAPER, PAPER2))
        c.line([(x0 + ind + 2, yy + (h - 2) / 2), (x1 - 2, yy + (h - 2) / 2)], GREY2, 1)
    c.line([(x0 + 2, y0 + h - 2), (x0 + 2, y0 + 2.5 * h - 1), (x0 + 4, y0 + 2.5 * h - 1)], K, 1)
    c.line([(x0 + 2, y0 + 1.5 * h - 1), (x0 + 4, y0 + 1.5 * h - 1)], K, 1)


def eye(c, cx, cy, r):
    c.poly([(cx - r, cy)] + [(cx + r * math.cos(math.radians(a)), cy - r * 0.6 * math.sin(math.radians(a)))
                              for a in range(180, -1, -15)] +
           [(cx + r * math.cos(math.radians(a)), cy + r * 0.6 * math.sin(math.radians(a))) for a in range(15, 180, 15)],
           WHITE)
    c.ellipse(cx - r * 0.45, cy - r * 0.45, cx + r * 0.45, cy + r * 0.45, (BLUE, BLUE2), None)
    c.ellipse(cx - r * 0.18, cy - r * 0.18, cx + r * 0.18, cy + r * 0.18, K, None)


def no_sign(c, cx, cy, r):
    c.ellipse(cx - r, cy - r, cx + r, cy + r, None, K, 3.2)
    c.ellipse(cx - r, cy - r, cx + r, cy + r, None, RED, 1.6)
    d = r * 0.7
    c.line([(cx - d, cy + d), (cx + d, cy - d)], K, 3.2)
    c.line([(cx - d, cy + d), (cx + d, cy - d)], RED, 1.6)


def clipboard(c, x0, y0, x1, y1):
    c.rect(x0, y0 + 1, x1, y1, (BROWN, BROWN2))
    c.rect(x0 + 2, y0 + 3, x1 - 2, y1 - 2, (WHITE, PAPER2), K2)
    w = x1 - x0
    c.rect(x0 + w * 0.3, y0, x1 - w * 0.3, y0 + 3, (GREY, GREY3))
