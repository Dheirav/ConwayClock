#!/usr/bin/env python3
"""Read the seven-segment display off a frame the wallpaper rendered.

native/test_clock.c reads the segments off the Life grid. This reads them off
the picture, so it covers what that cannot: the view geometry, the scale, the
palette, and the AM/PM dot that is drawn on top rather than computed by the
machine. Give it the BMP written by --frame and the life-clock.log beside it.

    tools/read-frame.py frame.bmp life-clock.log [--expect "12:05 PM"]

The segment rectangles are the ones in test_clock.c, in pattern coordinates;
the log's "geometry:" line says how those land on screen.
"""
import re, sys, struct

def geometry(log):
    m = None
    for line in open(log, encoding="utf-8", errors="replace"):
        if "geometry:" in line:
            m = line          # the last one: the view may have been rebuilt
    if not m:
        sys.exit("no 'geometry:' line in the log -- is this a --frame run?")
    g = dict(re.findall(r"(\w+) (-?\d+)", m.split("geometry:")[1]))
    return {k: int(v) for k, v in g.items()}

def load_bmp(path):
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        sys.exit(f"{path}: not a BMP")
    off = struct.unpack_from("<I", d, 10)[0]
    w, h, bits = struct.unpack_from("<iih", d, 18)[0], struct.unpack_from("<iih", d, 18)[1], struct.unpack_from("<H", d, 28)[0]
    if bits != 32:
        sys.exit(f"{path}: expected 32bpp, got {bits}")
    return d, off, w, abs(h), h < 0

class Frame:
    def __init__(self, bmp, geo):
        self.d, self.off, self.w, self.h, self.topdown = load_bmp(bmp)
        self.g = geo
        self.sx = geo["dstw"] / geo["pw"]
        self.sy = geo["dsth"] / geo["ph"]

    def to_screen(self, px, py):
        z = 1 << self.g["z"]
        return (self.g["dstx"] + (px - self.g["viewx"]) / z * self.sx,
                self.g["dsty"] + (py - self.g["viewy"]) / z * self.sy)

    def brightness(self, px, py, pw, ph):
        """Mean of b+g+r over a rectangle given in pattern coordinates."""
        x0, y0 = self.to_screen(px, py)
        x1, y1 = self.to_screen(px + pw, py + ph)
        x0, x1 = max(0, int(x0)), min(self.w, int(x1) + 1)
        y0, y1 = max(0, int(y0)), min(self.h, int(y1) + 1)
        if x1 <= x0 or y1 <= y0:
            return 0.0
        total = n = 0
        for y in range(y0, y1):
            row = self.off + (y if self.topdown else self.h - 1 - y) * self.w * 4
            for x in range(x0, x1):
                i = row + x * 4
                total += self.d[i] + self.d[i + 1] + self.d[i + 2]
                n += 1
        return total / n

# Segment sample rectangles, in pattern coordinates, from test_clock.c.
def segment_rects(x0, x1):
    xm = (x0 + x1) // 2
    return [(xm - 300, 4130, 600, 200),   # a top
            (xm, 4500, x1 - xm, 650),     # b upper right
            (xm, 5700, x1 - xm, 600),     # c lower right
            (xm - 300, 6530, 600, 200),   # d bottom
            (x0, 5700, xm - x0, 600),     # e lower left
            (x0, 4500, xm - x0, 650),     # f upper left
            (xm - 300, 5330, 600, 200)]   # g middle

PATTERNS = {"abcdef": "0", "bc": "1", "abdeg": "2", "abcdg": "3", "bcfg": "4",
            "acdfg": "5", "acdefg": "6", "abc": "7", "abcdefg": "8", "abcdfg": "9"}
DIGITS = [(3080, 4720), (5880, 7320), (8200, 9720)]

def read(frame):
    vals = [[frame.brightness(*r) for r in segment_rects(*d)] for d in DIGITS]
    flat = [v for row in vals for v in row]
    # A lit segment is a bundle of gliders, but how much of a sample rectangle it
    # fills depends on the segment's orientation: measured, a lit horizontal reads
    # about 175 and a lit vertical about 75 against a background of 31, so a
    # midpoint split would throw away every lit vertical. What is uniform is the
    # unlit level -- an unlit segment is just background. So take the background
    # from the dimmest sample (no valid time lights all 21) and call a segment lit
    # when it clears it by 15 % of the range; the dimmest lit segment measured
    # clears it by 28 %, and an unlit one by 0 %.
    bg, top = min(flat), max(flat)
    cut = bg + 0.15 * (top - bg)
    digits = []
    for row in vals:
        on = "".join(c for c, v in zip("abcdefg", row) if v > cut)
        digits.append(PATTERNS.get(on, "?"))
    lead = frame.brightness(2040, 4500, 360, 1800) > cut
    pm = frame.brightness(1380, 5255, 240, 240) > cut     # the drawn AM/PM dot
    return ("1" if lead else " ") + digits[0] + ":" + digits[1] + digits[2], pm, vals, cut

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    bmp, log = sys.argv[1], sys.argv[2]
    expect = None
    if "--expect" in sys.argv:
        expect = sys.argv[sys.argv.index("--expect") + 1]
    frame = Frame(bmp, geometry(log))
    text, pm, vals, cut = read(frame)
    got = f"{text} {'PM' if pm else 'AM'}"
    print(f"{bmp}: reads {got!r}  (threshold {cut:.0f})")
    for d, row in zip(DIGITS, vals):
        print("   digit at %5d: %s" % (d[0], " ".join(f"{c}{'*' if v > cut else '.'}{v:6.0f}" for c, v in zip("abcdefg", row))))
    if expect is not None:
        if got.strip() != expect.strip():
            sys.exit(f"FAIL: expected {expect!r}, read {got!r}")
        print(f"   matches {expect!r}")

main()
