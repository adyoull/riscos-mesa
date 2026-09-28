#!/usr/bin/env python3
"""colours.py SCREEN.ppm triangle|teapot: checks what an example drew.

triangle: the dark blue background (0.1, 0.1, 0.3) and pixels close to
each corner colour (red, green, blue) of the smoothly shaded triangle.
teapot: the background and plenty of lit orange."""
import sys

def load(path):
    data = open(path, 'rb').read()
    parts = data.split(b'\n', 1)
    _, w, h, _ = parts[0].split()
    return int(w), int(h), parts[1]

def main():
    w, h, px = load(sys.argv[1])
    counts = dict(bg=0, red=0, green=0, blue=0, orange=0)
    for i in range(0, w * h * 3, 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        if abs(r - 25) < 8 and abs(g - 25) < 8 and abs(b - 76) < 8:
            counts['bg'] += 1
        elif r > 170 and g < 90 and b < 90:
            counts['red'] += 1
        elif g > 170 and r < 90 and b < 90:
            counts['green'] += 1
        elif b > 170 and r < 90 and g < 90:
            counts['blue'] += 1
        elif r > 110 and r > g * 1.2 and g > b * 1.3 and b < 90:
            counts['orange'] += 1
    print(sys.argv[1], counts)
    if sys.argv[2] == 'triangle':
        ok = counts['bg'] > 5000 and min(counts['red'], counts['green'], counts['blue']) >= 10
    else:
        ok = counts['bg'] > 5000 and counts['orange'] > 2000
    sys.exit(0 if ok else 1)

main()
