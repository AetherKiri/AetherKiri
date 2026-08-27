#!/usr/bin/env python3
"""Small offline decoder for the TLG5 files shipped by KiriKiri games.

This intentionally mirrors LoadTLG.cpp so we can inspect assets without
starting the game.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path


def decompress(data: bytes, text: bytearray, r: int) -> tuple[bytes, int]:
    out = bytearray()
    pos = 0
    flags = 0
    while pos < len(data):
        flags >>= 1
        if (flags & 0x100) == 0:
            flags = data[pos] | 0xff00
            pos += 1
            if flags == 0xff00 and r < 4096 - 8 and pos < len(data) - 8:
                chunk = data[pos : pos + 8]
                out += chunk
                text[r : r + 8] = chunk
                r += 8
                pos += 8
                flags = 0
                continue
        if flags & 1:
            if pos + 2 > len(data):
                raise ValueError("truncated match")
            word = data[pos] | (data[pos + 1] << 8)
            pos += 2
            mpos = word & 0xFFF
            mlen = (word >> 12) + 3
            if mlen == 18:
                if pos >= len(data):
                    raise ValueError("truncated long match")
                mlen += data[pos]
                pos += 1
            for _ in range(mlen):
                value = text[mpos]
                out.append(value)
                text[r] = value
                mpos = (mpos + 1) & 0xFFF
                r = (r + 1) & 0xFFF
        else:
            if pos >= len(data):
                raise ValueError("truncated literal")
            value = data[pos]
            pos += 1
            out.append(value)
            text[r] = value
            r = (r + 1) & 0xFFF
    return bytes(out), r


def decode(path: Path) -> tuple[int, int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"TLG5.0\x00raw\x1a"):
        raise ValueError("not TLG5.0.raw")
    p = 11
    colors = data[p]
    p += 1
    width, height, blockheight = struct.unpack_from("<III", data, p)
    p += 12
    blockcount = (height - 1) // blockheight + 1
    p += blockcount * 4
    text = bytearray(4096)
    r = 0
    rows = [bytearray(width * 4) for _ in range(height)]
    prev = None
    for y0 in range(0, height, blockheight):
        planes = []
        for _ in range(colors):
            mark = data[p]
            size = struct.unpack_from("<I", data, p + 1)[0]
            p += 5
            payload = data[p : p + size]
            p += size
            if mark == 0:
                plane, r = decompress(payload, text, r)
            else:
                plane = payload
            planes.append(plane)
        y1 = min(height, y0 + blockheight)
        offsets = [0] * colors
        for y in range(y0, y1):
            row = rows[y]
            if prev is None:
                # TLG stores the first row as the cumulative channel values.
                pr = pg = pb = pa = 0
                for x in range(width):
                    rr = planes[0][offsets[0] + x]
                    gg = planes[1][offsets[1] + x]
                    bb = planes[2][offsets[2] + x]
                    bb = (bb + gg) & 0xFF
                    rr = (rr + gg) & 0xFF
                    pb = (pb + bb) & 0xFF
                    pg = (pg + gg) & 0xFF
                    pr = (pr + rr) & 0xFF
                    row[x * 4 : x * 4 + 4] = bytes((pb, pg, pr, 0xFF if colors == 3 else (pa + planes[3][offsets[3] + x]) & 0xFF))
                    if colors == 4:
                        pa = (pa + planes[3][offsets[3] + x]) & 0xFF
                offsets = [o + width for o in offsets]
            else:
                pc = [0] * colors
                for x in range(width):
                    # ComposeColors4To4 uses B,G,R,A ordering in the output.
                    c0 = planes[2][offsets[2] + x]
                    c1 = planes[1][offsets[1] + x]
                    c2 = planes[0][offsets[0] + x]
                    c0 = (c0 + c1) & 0xFF
                    c2 = (c2 + c1) & 0xFF
                    pc[0] = (pc[0] + c0) & 0xFF
                    pc[1] = (pc[1] + c1) & 0xFF
                    pc[2] = (pc[2] + c2) & 0xFF
                    if colors == 4:
                        c3 = planes[3][offsets[3] + x]
                        pc[3] = (pc[3] + c3) & 0xFF
                        row[x * 4 : x * 4 + 4] = bytes(((pc[0] + prev[x * 4 + 0]) & 0xFF,
                                                         (pc[1] + prev[x * 4 + 1]) & 0xFF,
                                                         (pc[2] + prev[x * 4 + 2]) & 0xFF,
                                                         (pc[3] + prev[x * 4 + 3]) & 0xFF))
                    else:
                        row[x * 4 : x * 4 + 4] = bytes(((pc[0] + prev[x * 4 + 0]) & 0xFF,
                                                         (pc[1] + prev[x * 4 + 1]) & 0xFF,
                                                         (pc[2] + prev[x * 4 + 2]) & 0xFF,
                                                         0xFF))
                offsets = [o + width for o in offsets]
            prev = row
    return width, height, colors, b"".join(rows)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} INPUT.tlg OUTPUT.ppm")
    w, h, colors, rgba_bgra = decode(Path(sys.argv[1]))
    # Engine scanlines are BGRA; write RGB for easy inspection.
    with open(sys.argv[2], "wb") as f:
        f.write(f"P6\n{w} {h}\n255\n".encode())
        for i in range(0, len(rgba_bgra), 4):
            b, g, r, a = rgba_bgra[i : i + 4]
            # Composite against checker-neutral gray so transparent regions are visible.
            if a != 255:
                r = (r * a + 80 * (255 - a)) // 255
                g = (g * a + 80 * (255 - a)) // 255
                b = (b * a + 80 * (255 - a)) // 255
            f.write(bytes((r, g, b)))
    print(f"{w}x{h} colors={colors} -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
