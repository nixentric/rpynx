"""Write the virtual-cursor image (white arrow, dark outline) as a PNG using
only the standard library. usage: make_cursor.py <out.png> [scale]"""
import struct
import sys
import zlib

# Arrow outline in a 12 x 19 design grid; hotspot at (0, 0).
ARROW = [(0, 0), (0, 16), (4, 12), (7, 18.5), (9.5, 17.5), (6.8, 11.5), (11.5, 11.5)]


def inside(px, py, poly):
    hit = False
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        if (y1 > py) != (y2 > py) and px < x1 + (py - y1) * (x2 - x1) / (y2 - y1):
            hit = not hit
    return hit


def render(scale):
    w, h = int(13 * scale), int(20 * scale)
    poly = [(x * scale + 1, y * scale + 1) for x, y in ARROW]
    fill = [[inside(x + 0.5, y + 0.5, poly) for x in range(w)] for y in range(h)]
    rows = []
    for y in range(h):
        row = bytearray([0])  # PNG filter: none
        for x in range(w):
            if fill[y][x]:
                edge = any(not (0 <= x + dx < w and 0 <= y + dy < h and fill[y + dy][x + dx])
                           for dx in (-1, 0, 1) for dy in (-1, 0, 1))
                row += bytes((20, 20, 20, 255) if edge else (255, 255, 255, 255))
            else:
                row += bytes((0, 0, 0, 0))
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
