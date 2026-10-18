"""Write the virtual-cursor image as a PNG using only the standard library: a black
arrow with a white outline and rounded corners (in the style of Bibata Modern
Classic; drawn here, no Bibata files used), anti-aliased.
usage: make_cursor.py <out.png> [scale]

The shape is a thin "core" arrow grown by BODY (black) and then OUTLINE (white),
measured as the distance to the core, so every corner is rounded and the outline
has the same width everywhere. HOTSPOT (pixels at scale 2, the size the runtime
uses) is the tip of the black body, where the eye reads the point on any
background; rsl_bridge.py passes the same numbers to Ren'Py."""
import math
import struct
import sys
import zlib

# Core arrow in design units (1 unit = `scale` pixels); tip at (0, 0).
CORE = [(0, 0), (0, 14.5), (3.6, 11.2), (6.0, 16.6), (8.2, 15.7), (5.9, 10.6), (10.6, 10.6)]
BODY, OUTLINE = 1.0, 1.0           # design units
BLACK, WHITE = (24, 24, 24), (255, 255, 255)
SAMPLES = 4                        # per pixel and axis, for anti-aliasing
HOTSPOT = (4, 3)                   # pixels at scale 2; keep in step with rsl_bridge.py


def inside(px, py, poly):
    hit = False
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        if (y1 > py) != (y2 > py) and px < x1 + (py - y1) * (x2 - x1) / (y2 - y1):
            hit = not hit
    return hit


def distance(px, py, poly):
    """Signed distance from (px, py) to the polygon's edge: negative inside."""
    best = float("inf")
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
        best = min(best, math.hypot(px - ax - t * dx, py - ay - t * dy))
    return -best if inside(px, py, poly) else best


def render(scale):
    grow = BODY + OUTLINE
    off = grow * scale + 1
    w = int(math.ceil((max(x for x, _ in CORE) + grow) * scale + off)) + 1
    h = int(math.ceil((max(y for _, y in CORE) + grow) * scale + off)) + 1
    core = [(x * scale + off, y * scale + off) for x, y in CORE]
    body, edge = BODY * scale, grow * scale
    rows = []
    for y in range(h):
        row = bytearray([0])  # PNG filter: none
        for x in range(w):
            black = white = 0
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    d = distance(x + (sx + 0.5) / SAMPLES, y + (sy + 0.5) / SAMPLES, core)
                    if d <= body:
                        black += 1
                    elif d <= edge:
                        white += 1
            n = black + white
            if n == 0:
                row += bytes((0, 0, 0, 0))
                continue
            rgb = [round((black * b + white * c) / n) for b, c in zip(BLACK, WHITE)]
            row += bytes(rgb + [round(255 * n / SAMPLES ** 2)])
        rows.append(bytes(row))
    return w, h, b"".join(rows)


def png(w, h, raw):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    w, h, raw = render(float(sys.argv[2]) if len(sys.argv) > 2 else 2.0)
    with open(sys.argv[1], "wb") as f:
        f.write(png(w, h, raw))
