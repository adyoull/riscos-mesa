#!/usr/bin/env python3
"""tune.py RECORDING.raw: checks example 6's tune, recorded by SDL's disk
driver as 16-bit stereo at 22050 Hz: three notes (523, 659, 784 Hz) in
that order, the first on the left, the second in the middle, the third on
the right."""
import sys
import numpy as np

RATE = 22050
s = np.frombuffer(open(sys.argv[1], 'rb').read(), dtype='<i2').astype(float)
left, right = s[0::2], s[1::2]
block = RATE // 50
energy = np.array([np.abs(left[i:i + block]).mean() + np.abs(right[i:i + block]).mean()
                   for i in range(0, len(left) - block, block)])
loud = energy > energy.max() * 0.2
# runs of loud blocks = notes (a gap of a few blocks between them)
notes, start = [], None
for i, on in enumerate(list(loud) + [False]):
    if on and start is None:
        start = i
    elif not on and start is not None:
        if i - start >= 10:            # at least 0.2 s
            notes.append((start * block, i * block))
        start = None
ok = len(notes) == 3
want = [(523.25, 'left'), (659.25, 'middle'), (783.99, 'right')]
for (a, b), (freq, where) in zip(notes, want):
    l, r = left[a:b], right[a:b]
    spectrum = np.abs(np.fft.rfft(l + r))
    got = np.argmax(spectrum) * RATE / len(l)
    bal = np.abs(l).mean() / max(np.abs(r).mean(), 1e-9)
    side = 'left' if bal > 1.5 else 'right' if bal < 1 / 1.5 else 'middle'
    good = abs(got - freq) < 10 and side == where
    ok = ok and good
    print('%s %.0f Hz (want %.0f), L/R %.2f -> %s (want %s), %.2f s' %
          ('ok  ' if good else 'FAIL', got, freq, bal, side, where, (b - a) / RATE))
print('%d notes found' % len(notes))
sys.exit(0 if ok else 1)
