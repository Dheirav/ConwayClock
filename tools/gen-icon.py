#!/usr/bin/env python3
"""Generate native/life-clock.ico: a 7-segment "12" with a colon, amber on a dark rounded square."""
import struct, os, zlib
BG = (7, 9, 15); FG = (255, 233, 168); OFF = (17, 20, 28)
N = 256
img = [[(0, 0, 0, 0)] * N for _ in range(N)]
def inside_round_rect(x, y, r):
    cx = min(max(x, r), N - 1 - r); cy = min(max(y, r), N - 1 - r); return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
for y in range(N):
    for x in range(N):
        if inside_round_rect(x + 0.5, y + 0.5, 48): img[y][x] = BG + (255,)
# 7-segment digits. Segments a..g as rectangles in a digit box (w x h), stroke t.
def seg_rects(x0, y0, w, h, t, g=6):
    # horizontal segments inset by t+g, vertical ones shortened by g at each junction
    return { 'a': (x0 + t + g, y0, w - 2 * (t + g), t), 'g': (x0 + t + g, y0 + h // 2 - t // 2, w - 2 * (t + g), t), 'd': (x0 + t + g, y0 + h - t, w - 2 * (t + g), t),
             'f': (x0, y0 + t + g, t, h // 2 - t - t // 2 - 2 * g), 'b': (x0 + w - t, y0 + t + g, t, h // 2 - t - t // 2 - 2 * g),
             'e': (x0, y0 + h // 2 + t // 2 + g, t, h // 2 - t - t // 2 - 2 * g), 'c': (x0 + w - t, y0 + h // 2 + t // 2 + g, t, h // 2 - t - t // 2 - 2 * g) }
DIG = { '1': 'bc', '2': 'abdeg' }
def fill(rect, col):
    x, y, w, h = rect
    for yy in range(int(y), int(y + h)):
        for xx in range(int(x), int(x + w)):
            if 0 <= xx < N and 0 <= yy < N: img[yy][xx] = col + (255,)
t = 16; h = 156; w = 78; y0 = (N - h) // 2
x1 = 34; xc = 128; x2 = N - 34 - w
for ch, x0 in (('1', x1), ('2', x2)):
    for s, r in seg_rects(x0, y0, w, h, t).items(): fill(r, FG if s in DIG[ch] else OFF)
for cy in (y0 + h // 3, y0 + 2 * h // 3): fill((xc - 9, cy - 9, 18, 18), FG)
def downsample(k):
    m = N // k; out = [[None] * m for _ in range(m)]
    for y in range(m):
        for x in range(m):
            acc = [0, 0, 0, 0]
            for dy in range(k):
                for dx in range(k):
                    p = img[y * k + dy][x * k + dx]; a = p[3]
                    acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a
            a = acc[3] // (k * k); out[y][x] = (acc[0] // max(acc[3], 1), acc[1] // max(acc[3], 1), acc[2] // max(acc[3], 1), a)
    return out
def bmp_entry(px, n):
    hdr = struct.pack('<IiiHHIIiiII', 40, n, n * 2, 1, 32, 0, n * n * 4, 0, 0, 0, 0)
    body = bytearray()
    for y in range(n - 1, -1, -1):
        for x in range(n): r, g, b, a = px[y][x]; body += bytes((b, g, r, a))
    mask = bytes(((n + 31) // 32) * 4) * n
    return hdr + bytes(body) + mask
sizes = [16, 32, 48, 256]
entries = [bmp_entry(downsample(N // s) if s != N else img, s) for s in sizes]
out = struct.pack('<HHH', 0, 1, len(sizes)); off = 6 + 16 * len(sizes); dir_ = b''
for s, e in zip(sizes, entries):
    dir_ += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(e), off); off += len(e)
out += dir_ + b''.join(entries)
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'native', 'life-clock.ico')
open(p, 'wb').write(out); print('wrote', p, len(out), 'bytes')
