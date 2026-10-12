"""Builds a Scaleform font library (SWF with one DefineFont3) from a TrueType font, for Skyrim's fontconfig.txt.

    py -3 tools/make_font_swf.py Mario64.ttf dist/TES64/Data/Interface/fonts_tes64.swf Mario64

The font's TrueType outlines are quadratic, as SWF's are, so glyphs go across curve for curve. Needs fontTools and
skia-pathops (overlaps merged).
"""
import struct
import sys

from fontTools.pens.recordingPen import RecordingPen
from fontTools.ttLib import TTFont

EM = 1024 * 20   # DefineFont3's em square, in twentieths


class Bits:
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def u(self, v, n):
        for i in range(n - 1, -1, -1):
            self.acc = (self.acc << 1) | ((v >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc, self.n = 0, 0

    def s(self, v, n):
        self.u(v & ((1 << n) - 1), n)

    def flush(self):
        if self.n:
            self.out.append(self.acc << (8 - self.n))
            self.acc, self.n = 0, 0
        return bytes(self.out)


def sbits(*vals):
    """bits for signed values"""
    m = max([abs(v) for v in vals] + [1])
    n = 1
    while not (-(1 << (n - 1)) <= min(vals) and max(vals) <= (1 << (n - 1)) - 1):
        n += 1
    return max(n, 2) if m else 2


def rect(xmin, xmax, ymin, ymax):
    b = Bits()
    n = sbits(xmin, xmax, ymin, ymax)
    b.u(n, 5)
    for v in (xmin, xmax, ymin, ymax):
        b.s(v, n)
    return b.flush()


def contours(glyphset, name, scale):
    """contours as lists of ('L', x, y) / ('Q', cx, cy, x, y), first item ('M', x, y); SWF axes (y down)"""
    pen = RecordingPen()
    glyphset[name].draw(pen)
    out, cur, start, pos = [], None, None, None
    P = lambda p: (round(p[0] * scale), round(-p[1] * scale))
    for op, args in pen.value:
        if op == "moveTo":
            cur = [("M",) + P(args[0])]
            start = pos = P(args[0])
            out.append(cur)
        elif op == "lineTo":
            pos = P(args[0])
            cur.append(("L",) + pos)
        elif op == "qCurveTo":
            pts = [P(p) if p is not None else None for p in args]
            if pts[-1] is None:   # a closed all-off-curve contour: its start is implied
                pts = pts[:-1]
                mid = ((pts[-1][0] + pts[0][0]) // 2, (pts[-1][1] + pts[0][1]) // 2)
                if cur is None:
                    cur = [("M",) + mid]
                    out.append(cur)
                    start = pos = mid
                pts = pts + [mid]
            offs, end = pts[:-1], pts[-1]
            for i, c in enumerate(offs):
                a = end if i == len(offs) - 1 else ((c[0] + offs[i + 1][0]) // 2, (c[1] + offs[i + 1][1]) // 2)
                cur.append(("Q",) + c + a)
            pos = end
        elif op == "curveTo":
            raise SystemExit("cubic outlines (OpenType CFF) aren't supported: use a TrueType font")
        elif op in ("closePath", "endPath"):
            if cur and pos != start:
                cur.append(("L",) + start)
            cur = None
    return out


def shape(cs):
    b = Bits()
    b.u(1, 4)   # fill bits
    b.u(0, 4)   # line bits
    first = True
    for c in cs:
        x, y = c[0][1], c[0][2]
        n = sbits(x, y)
        b.u(0, 1)                 # style change
        b.u(0, 1); b.u(0, 1)             # new styles, line style
        b.u(1 if first else 0, 1)       # fill style 1: glyphs fill on the right of their edges (TrueType's
        b.u(0, 1)                       # clockwise outers, flipped to SWF's y-down), fill style 0 unused
        b.u(1, 1)                       # move to
        b.u(n, 5); b.s(x, n); b.s(y, n)
        if first:
            b.u(1, 1)   # fill style 1 = 1
            first = False
        px, py = x, y
        for e in c[1:]:
            if e[0] == "L":
                dx, dy = e[1] - px, e[2] - py
                if dx == 0 and dy == 0:
                    continue
                n = sbits(dx, dy)
                b.u(1, 1); b.u(1, 1); b.u(n - 2, 4); b.u(1, 1); b.s(dx, n); b.s(dy, n)
                px, py = e[1], e[2]
            else:
                cdx, cdy, adx, ady = e[1] - px, e[2] - py, e[3] - e[1], e[4] - e[2]
                n = sbits(cdx, cdy, adx, ady)
                b.u(1, 1); b.u(0, 1); b.u(n - 2, 4)
                for v in (cdx, cdy, adx, ady):
                    b.s(v, n)
                px, py = e[3], e[4]
    b.u(0, 6)   # end
    return b.flush()


def tag(code, body):
    if len(body) < 63:
        return struct.pack("<H", (code << 6) | len(body)) + body
    return struct.pack("<HI", (code << 6) | 0x3F, len(body)) + body


def main(ttf, out, name):
    f = TTFont(ttf)
    # pixel fonts are drawn as overlapping squares, which SWF's even-odd fill would punch holes through: merged first
    from fontTools.ttLib.removeOverlaps import removeOverlaps
    removeOverlaps(f)
    scale = EM / f["head"].unitsPerEm
    cmap = f.getBestCmap()
    gs = f.getGlyphSet()
    hmtx = f["hmtx"]
    codes = sorted(c for c in cmap if c < 0x10000)
    shapes, advances, bounds = [], [], []
    for code in codes:
        g = cmap[code]
        cs = contours(gs, g, scale)
        shapes.append(shape(cs))
        advances.append(min(32767, round(hmtx[g][0] * scale)))
        xs = [p for c in cs for e in c for p in e[1::2]] or [0]
        ys = [p for c in cs for e in c for p in e[2::2]] or [0]
        bounds.append(rect(min(xs), max(xs), min(ys), max(ys)))

    n = len(codes)
    offsets, pos = [], 4 * (n + 1)
    for s in shapes:
        offsets.append(pos)
        pos += len(s)
    nm = name.encode("ascii")
    body = struct.pack("<HBBB", 1, 0x80 | 0x08 | 0x04, 1, len(nm)) + nm + struct.pack("<H", n)
    body += b"".join(struct.pack("<I", o) for o in offsets) + struct.pack("<I", pos)
    body += b"".join(shapes)
    body += b"".join(struct.pack("<H", c) for c in codes)
    asc = round(f["hhea"].ascent * scale)
    desc = round(abs(f["hhea"].descent) * scale)
    body += struct.pack("<HHh", min(asc, 65535), min(desc, 65535), 0)
    body += b"".join(struct.pack("<h", a) for a in advances)
    body += b"".join(bounds)
    body += struct.pack("<H", 0)   # no kerning

    tags = tag(69, struct.pack("<I", 0))                                  # FileAttributes
    tags += tag(75, body)                                                  # DefineFont3
    tags += tag(88, struct.pack("<H", 1) + nm + b"\0" + b"\0")             # DefineFontName
    tags += tag(56, struct.pack("<HH", 1, 1) + nm + b"\0")                 # ExportAssets
    tags += tag(1, b"") + tag(0, b"")                                      # ShowFrame, End
    head = rect(0, 11000, 0, 8000) + struct.pack("<HH", 24 << 8, 1)
    data = head + tags
    with open(out, "wb") as fh:
        fh.write(b"FWS" + bytes([10]) + struct.pack("<I", 8 + len(data)) + data)
    print(f"{out}: {n} glyphs as '{name}'")


if __name__ == "__main__":
    main(*sys.argv[1:4])
