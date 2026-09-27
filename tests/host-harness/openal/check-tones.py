#!/usr/bin/env python3
"""Checks altest's recording (16-bit stereo, as SDL's disk driver wrote it):
440 Hz in the middle, then 660 Hz on the left, then 880 Hz on the right,
each lasting about a second.

    check-tones.py FILE RATE
"""
import math
import struct
import sys

TONES = [(440, "middle"), (660, "left"), (880, "right")]


def main():
    path, rate = sys.argv[1], int(sys.argv[2])
    data = open(path, "rb").read()
    n = len(data) // 4
    frames = struct.unpack("<%dh" % (2 * n), data[: 4 * n])
    left, right = frames[0::2], frames[1::2]
    print("recording: %.2f s" % (n / rate))

    # Classify 50 ms windows by frequency (from zero crossings of L+R)
    win = rate // 20
    windows = []
    for start in range(0, n - win, win):
        l, r = left[start : start + win], right[start : start + win]
        mono = [a + b for a, b in zip(l, r)]
        level = math.sqrt(sum(x * x for x in mono) / win)
        if level < 1000:
            windows.append(None)
            continue
        crossings = sum(1 for a, b in zip(mono, mono[1:]) if (a < 0) != (b < 0))
        freq = crossings * rate / (2 * win)
        tone = min(TONES, key=lambda t: abs(t[0] - freq))
        if abs(tone[0] - freq) > tone[0] * 0.05:
            print("     window at %.2f s: %.0f Hz (a change of tone)" % (start / rate, freq))
            windows.append("?")
            continue
        rl = math.sqrt(sum(x * x for x in l) / win)
        rr = math.sqrt(sum(x * x for x in r) / win)
        windows.append((tone, rl, rr))

    failures = 0
    for tone in TONES:
        mine = [w for w in windows if isinstance(w, tuple) and w[0] == tone]
        first = windows.index(mine[0]) if mine else -1
        secs = len(mine) / 20
        rl = sum(w[1] for w in mine) / max(len(mine), 1)
        rr = sum(w[2] for w in mine) / max(len(mine), 1)
        ratio = rl / rr if rr else float("inf")
        where = {"middle": 0.8 <= ratio <= 1.25, "left": ratio > 2, "right": ratio < 0.5}[tone[1]]
        ok = 0.8 <= secs <= 1.1 and where
        failures += not ok
        print("%s %d Hz: %.2f s from %.2f s, left/right level %.2f (%s expected)"
              % ("ok  " if ok else "FAIL", tone[0], secs, first / 20, ratio, tone[1]))
    order = [w[0][0] for w in windows if isinstance(w, tuple)]
    in_order = order == sorted(order)
    failures += not in_order
    print("%s tones in order" % ("ok  " if in_order else "FAIL"))
    # A window where one tone ends and the next starts (or with a fade)
    # gives a mixed reading; one inside a tone is a fault.
    def tone_of(w):
        return w[0] if isinstance(w, tuple) else w
    inside = [i for i, w in enumerate(windows) if w == "?" and 0 < i < len(windows) - 1
              and isinstance(windows[i - 1], tuple) and tone_of(windows[i - 1]) == tone_of(windows[i + 1])]
    failures += len(inside)
    print("%s no stray sound inside a tone" % ("ok  " if not inside else "FAIL"))
    print("PASS" if not failures else "FAIL")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
