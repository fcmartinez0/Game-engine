#!/usr/bin/env python3
"""Look inside a PNG without any image library: size, number of colours, average brightness, and a small ASCII
picture of it (so a CI log can show what the game drew).

    python3 tools/png_stats.py screenshot.png [--min-colors N] [--width COLUMNS]

Reads 8-bit RGB/RGBA PNGs (what the game's F12 screenshot writes). Exits 1 if the picture has fewer than N
different colours (a blank or one-colour window), 2 if it is not a PNG it understands.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, ihdr = 8, [], None
    while pos + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if not ihdr:
        raise ValueError("no header")
    w, h, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError("only 8-bit RGB/RGBA, not interlaced")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    if len(raw) < h * (stride + 1):
        raise ValueError("truncated")
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
            elif f != 0:
                raise ValueError("bad filter")
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def main():
    args = sys.argv[1:]
    min_colors, cols, path = 0, 100, None
    while args:
        a = args.pop(0)
        if a == "--min-colors":
            min_colors = int(args.pop(0))
        elif a == "--width":
            cols = int(args.pop(0))
        else:
            path = a
    if not path:
        print(__doc__)
        return 2
    try:
        w, h, bpp, rows = read_png(path)
    except Exception as e:
        print("%s: %s" % (path, e))
        return 2
    colors, total = set(), 0
    for line in rows:
        for x in range(0, w * bpp, bpp):
            r, g, b = line[x], line[x + 1], line[x + 2]
            colors.add((r, g, b))
            total += (299 * r + 587 * g + 114 * b) // 1000
    print("%s: %dx%d, %d colours, average brightness %d/255" % (path, w, h, len(colors), total // (w * h)))
    ramp = " .:-=+*#%@"
    rows_out = max(1, cols * h // w // 2)    # characters are about twice as tall as wide
    for ty in range(rows_out):
        y0, y1 = ty * h // rows_out, max(ty * h // rows_out + 1, (ty + 1) * h // rows_out)
        out = []
        for tx in range(cols):
            x0, x1 = tx * w // cols, max(tx * w // cols + 1, (tx + 1) * w // cols)
            s = n = 0
            for y in range(y0, y1):
                line = rows[y]
                for x in range(x0, x1):
                    s += (299 * line[x * bpp] + 587 * line[x * bpp + 1] + 114 * line[x * bpp + 2]) // 1000
                    n += 1
            out.append(ramp[min(len(ramp) - 1, (s // n) * len(ramp) // 256)])
        print("".join(out).rstrip())
    if len(colors) < min_colors:
        print("FAIL: only %d colours (wanted at least %d): the picture is blank" % (len(colors), min_colors))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
