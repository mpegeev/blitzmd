#!/usr/bin/env python3
"""Generate the small image assets used by samples/gfm/*.md.

Standard library only (zlib + struct). Produces PNG (RGBA, RGB, indexed+tRNS),
baseline JPEG, animated GIF, 24-bit BMP and a few SVG files into ../img/.

Run from anywhere:  python samples/gfm/tools/gen_images.py
The output is deterministic, so re-running produces identical files.
"""
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "img"))

# --------------------------------------------------------------------------
# Tiny 5x7 bitmap font (only the glyphs we need)
# --------------------------------------------------------------------------
FONT = {
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."],
    "x": [".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#"],
    "-": [".....", ".....", ".....", "#####", ".....", ".....", "....."],
    "@": [".###.", "#...#", "#.###", "#.#.#", "#.###", "#....", ".####"],
    " ": ["....."] * 7,
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
}


class Canvas:
    """Simple RGBA canvas stored as a flat bytearray."""

    def __init__(self, w, h, color=(255, 255, 255, 255)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(color) * (w * h))

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 4
            if len(c) == 3 or c[3] == 255:
                self.px[i:i + 3] = bytes(c[:3])
                self.px[i + 3] = 255
            else:  # alpha-blend onto existing pixel
                a = c[3] / 255.0
                for k in range(3):
                    self.px[i + k] = int(self.px[i + k] * (1 - a) + c[k] * a + 0.5)
                self.px[i + 3] = max(self.px[i + 3], c[3])

    def get(self, x, y):
        i = (y * self.w + x) * 4
        return tuple(self.px[i:i + 4])

    def rect(self, x0, y0, w, h, c):
        for y in range(max(0, y0), min(self.h, y0 + h)):
            for x in range(max(0, x0), min(self.w, x0 + w)):
                self.set(x, y, c)

    def text(self, x0, y0, s, scale, c):
        cx = x0
        for ch in s:
            glyph = FONT.get(ch, FONT[" "])
            for gy, row in enumerate(glyph):
                for gx, bit in enumerate(row):
                    if bit == "#":
                        self.rect(cx + gx * scale, y0 + gy * scale, scale, scale, c)
            cx += 6 * scale

    @staticmethod
    def text_width(s, scale):
        return len(s) * 6 * scale - scale

    def text_centered(self, s, scale, c, cy=None):
        tw = self.text_width(s, scale)
        th = 7 * scale
        y = (self.h - th) // 2 if cy is None else cy - th // 2
        self.text((self.w - tw) // 2, y, s, scale, c)


def lerp(a, b, t):
    return int(a + (b - a) * t + 0.5)


def lerp3(c1, c2, t):
    return tuple(lerp(c1[k], c2[k], t) for k in range(3))


# --------------------------------------------------------------------------
# PNG
# --------------------------------------------------------------------------
def _chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, cv, mode="RGBA"):
    raw = bytearray()
    for y in range(cv.h):
        raw.append(0)
        row = cv.px[y * cv.w * 4:(y + 1) * cv.w * 4]
        if mode == "RGBA":
            raw += row
        else:
            for x in range(cv.w):
                raw += row[x * 4:x * 4 + 3]
    ctype = 6 if mode == "RGBA" else 2
    data = b"\x89PNG\r\n\x1a\n"
    data += _chunk(b"IHDR", struct.pack(">IIBBBBB", cv.w, cv.h, 8, ctype, 0, 0, 0))
    data += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def write_png_indexed(path, w, h, indices, palette, alphas):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += bytes(indices[y * w:(y + 1) * w])
    data = b"\x89PNG\r\n\x1a\n"
    data += _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    data += _chunk(b"PLTE", b"".join(bytes(c) for c in palette))
    data += _chunk(b"tRNS", bytes(alphas))
    data += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


# --------------------------------------------------------------------------
# BMP (24-bit, bottom-up)
# --------------------------------------------------------------------------
def write_bmp(path, cv):
    row_size = (cv.w * 3 + 3) & ~3
    pad = row_size - cv.w * 3
    pixels = bytearray()
    for y in range(cv.h - 1, -1, -1):
        for x in range(cv.w):
            r, g, b, _ = cv.get(x, y)
            pixels += bytes((b, g, r))
        pixels += b"\0" * pad
    header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, cv.w, cv.h, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(header + info + pixels)


# --------------------------------------------------------------------------
# Baseline JPEG (4:4:4, standard Annex K tables)
# --------------------------------------------------------------------------
ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
          12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
          35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
          58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63]
Q_LUM = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
         14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
         18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
         49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
Q_CHR = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
         24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99] + [99] * 32
DC_LUM_BITS = [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0]
DC_CHR_BITS = [0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0]
DC_VALS = list(range(12))
AC_LUM_BITS = [0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D]
AC_LUM_VALS = [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA]
AC_CHR_BITS = [0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77]
AC_CHR_VALS = [
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA]

_COS = [[math.cos((2 * x + 1) * u * math.pi / 16) for x in range(8)] for u in range(8)]
_C = [1 / math.sqrt(2)] + [1.0] * 7


def _huff_codes(bits, vals):
    codes, code, k = {}, 0, 0
    for length in range(1, 17):
        for _ in range(bits[length - 1]):
            codes[vals[k]] = (code, length)
            code += 1
            k += 1
        code <<= 1
    return codes


class _BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def write(self, value, length):
        self.acc = (self.acc << length) | (value & ((1 << length) - 1))
        self.n += length
        while self.n >= 8:
            self.n -= 8
            b = (self.acc >> self.n) & 0xFF
            self.out.append(b)
            if b == 0xFF:
                self.out.append(0)  # byte stuffing
        self.acc &= (1 << self.n) - 1

    def flush(self):
        if self.n:
            self.write((1 << (8 - self.n)) - 1, 8 - self.n)  # pad with 1-bits


def _fdct(block):
    tmp = [0.0] * 64
    for y in range(8):
        row = block[y * 8:y * 8 + 8]
        for u in range(8):
            cu = _COS[u]
            tmp[y * 8 + u] = sum(row[x] * cu[x] for x in range(8))
    out = [0.0] * 64
    for u in range(8):
        col = [tmp[y * 8 + u] for y in range(8)]
        for v in range(8):
            cv = _COS[v]
            out[v * 8 + u] = 0.25 * _C[u] * _C[v] * sum(col[y] * cv[y] for y in range(8))
    return out


def _category(v):
    v = abs(v)
    n = 0
    while v:
        n += 1
        v >>= 1
    return n


def write_jpeg(path, cv, quality=85):
    assert cv.w % 8 == 0 and cv.h % 8 == 0
    scale = 5000 // quality if quality < 50 else 200 - 2 * quality
    ql = [max(1, min(255, (q * scale + 50) // 100)) for q in Q_LUM]
    qc = [max(1, min(255, (q * scale + 50) // 100)) for q in Q_CHR]
    dc_l, dc_c = _huff_codes(DC_LUM_BITS, DC_VALS), _huff_codes(DC_CHR_BITS, DC_VALS)
    ac_l, ac_c = _huff_codes(AC_LUM_BITS, AC_LUM_VALS), _huff_codes(AC_CHR_BITS, AC_CHR_VALS)

    # colour conversion
    Y, Cb, Cr = [], [], []
    for i in range(cv.w * cv.h):
        r, g, b = cv.px[i * 4], cv.px[i * 4 + 1], cv.px[i * 4 + 2]
        Y.append(0.299 * r + 0.587 * g + 0.114 * b - 128)
        Cb.append(-0.168736 * r - 0.331264 * g + 0.5 * b)
        Cr.append(0.5 * r - 0.418688 * g - 0.081312 * b)

    bw = _BitWriter()
    prev = [0, 0, 0]
    comps = [(Y, ql, dc_l, ac_l), (Cb, qc, dc_c, ac_c), (Cr, qc, dc_c, ac_c)]
    for by in range(0, cv.h, 8):
        for bx in range(0, cv.w, 8):
            for ci, (plane, q, dct_dc, dct_ac) in enumerate(comps):
                block = [plane[(by + y) * cv.w + bx + x] for y in range(8) for x in range(8)]
                coef = _fdct(block)
                zz = [int(round(coef[ZIGZAG[k]] / q[ZIGZAG[k]])) for k in range(64)]
                diff = zz[0] - prev[ci]
                prev[ci] = zz[0]
                cat = _category(diff)
                bw.write(*dct_dc[cat])
                if cat:
                    bw.write(diff if diff > 0 else diff + (1 << cat) - 1, cat)
                run = 0
                for k in range(1, 64):
                    v = zz[k]
                    if v == 0:
                        run += 1
                        continue
                    while run > 15:
                        bw.write(*dct_ac[0xF0])
                        run -= 16
                    cat = _category(v)
                    bw.write(*dct_ac[(run << 4) | cat])
                    bw.write(v if v > 0 else v + (1 << cat) - 1, cat)
                    run = 0
                if run:
                    bw.write(*dct_ac[0x00])
    bw.flush()

    def seg(marker, payload):
        return struct.pack(">BBH", 0xFF, marker, len(payload) + 2) + payload

    out = b"\xFF\xD8"
    out += seg(0xE0, b"JFIF\0\x01\x01\x00\x00\x01\x00\x01\x00\x00")
    out += seg(0xDB, b"\x00" + bytes(ql[ZIGZAG[k]] for k in range(64))
               + b"\x01" + bytes(qc[ZIGZAG[k]] for k in range(64)))
    out += seg(0xC0, struct.pack(">BHHB", 8, cv.h, cv.w, 3)
               + b"\x01\x11\x00" + b"\x02\x11\x01" + b"\x03\x11\x01")
    dht = b""
    for cls_id, bits, vals in ((0x00, DC_LUM_BITS, DC_VALS), (0x10, AC_LUM_BITS, AC_LUM_VALS),
                               (0x01, DC_CHR_BITS, DC_VALS), (0x11, AC_CHR_BITS, AC_CHR_VALS)):
        dht += bytes([cls_id]) + bytes(bits) + bytes(vals)
    out += seg(0xC4, dht)
    out += seg(0xDA, b"\x03" + b"\x01\x00" + b"\x02\x11" + b"\x03\x11" + b"\x00\x3F\x00")
    out += bytes(bw.out) + b"\xFF\xD9"
    with open(path, "wb") as f:
        f.write(out)


# --------------------------------------------------------------------------
# GIF (animated, global palette, LZW)
# --------------------------------------------------------------------------
def _lzw(indices, min_size):
    clear, eoi = 1 << min_size, (1 << min_size) + 1
    out = bytearray()
    acc = nbits = 0

    def emit(code, size):
        nonlocal acc, nbits
        acc |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nbits -= 8

    def reset():
        return {(i,): i for i in range(clear)}, eoi + 1, min_size + 1

    table, nxt, size = reset()
    emit(clear, size)
    w = (indices[0],)
    for k in indices[1:]:
        wk = w + (k,)
        if wk in table:
            w = wk
            continue
        emit(table[w], size)
        table[wk] = nxt
        nxt += 1
        if nxt - 1 == (1 << size) and size < 12:
            size += 1
        if nxt >= 4095:
            emit(clear, size)
            table, nxt, size = reset()
        w = (k,)
    emit(table[w], size)
    emit(eoi, size)
    if nbits:
        out.append(acc & 0xFF)
    blocks = bytearray()
    for i in range(0, len(out), 255):
        part = out[i:i + 255]
        blocks.append(len(part))
        blocks += part
    blocks.append(0)
    return bytes([min_size]) + bytes(blocks)


def write_gif(path, w, h, frames, palette, delay_cs=50):
    assert len(palette) == 16
    data = b"GIF89a" + struct.pack("<HHBBB", w, h, 0x80 | 0x30 | 0x03, 0, 0)
    data += b"".join(bytes(c) for c in palette)
    data += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00"  # loop forever
    for idx in frames:
        data += b"\x21\xF9\x04" + struct.pack("<BHBB", 0x04, delay_cs, 0, 0)
        data += b"\x2C" + struct.pack("<HHHHB", 0, 0, w, h, 0)
        data += _lzw(idx, 4)
    data += b"\x3B"
    with open(path, "wb") as f:
        f.write(data)


# --------------------------------------------------------------------------
# The actual assets
# --------------------------------------------------------------------------
def ruler_image(w, h, horizontal, label):
    cv = Canvas(w, h)
    c1, c2 = (0x09, 0x69, 0xDA), (0x82, 0x50, 0xDF)  # GitHub blue -> purple
    length = w if horizontal else h
    for i in range(length):
        col = lerp3(c1, c2, i / (length - 1)) + (255,)
        if horizontal:
            cv.rect(i, 0, 1, h, col)
        else:
            cv.rect(0, i, w, 1, col)
    white = (255, 255, 255, 255)
    for i in range(0, length, 10):
        tick = 60 if i % 100 == 0 else (35 if i % 50 == 0 else 15)
        if horizontal:
            cv.rect(i, 0, 1 if i % 100 else 2, tick, white)
            cv.rect(i, h - tick, 1 if i % 100 else 2, tick, white)
        else:
            cv.rect(0, i, tick, 1 if i % 100 else 2, white)
            cv.rect(w - tick, i, tick, 1 if i % 100 else 2, white)
    if horizontal:
        for i in range(200, length, 200):
            cv.text(i + 4, 64, str(i), 2, white)
        cv.text_centered(label, 8, white)
    else:
        for i in range(100, length, 100):
            cv.text(64, i + 4, str(i), 2, white)
        cv.text_centered(label, 4, white)
    return cv


def main():
    os.makedirs(OUT, exist_ok=True)
    p = lambda name: os.path.join(OUT, name)

    # 1. wide + tall rulers (RGB PNG)
    write_png(p("wide-2400x300.png"), ruler_image(2400, 300, True, "WIDE 2400x300"), "RGB")
    write_png(p("tall-300x1200.png"), ruler_image(300, 1200, False, "300x1200"), "RGB")

    # 2. transparent RGBA PNG: soft ring + fully transparent background
    cv = Canvas(160, 160, (0, 0, 0, 0))
    for y in range(160):
        for x in range(160):
            d = math.hypot(x - 79.5, y - 79.5)
            if d < 76:
                t = x / 159
                col = lerp3((0x1F, 0x88, 0x3D), (0x09, 0x69, 0xDA), t)
                a = 255 if d > 56 else int(255 * (d / 56) ** 2)
                i = (y * 160 + x) * 4
                cv.px[i:i + 4] = bytes(col + (a,))
    cv.text_centered("ALPHA", 4, (0x24, 0x29, 0x2F, 255))
    write_png(p("transparent.png"), cv, "RGBA")

    # 3. indexed PNG with tRNS (palette transparency, 4 alpha levels)
    w, h = 128, 64
    palette = [(0xCF, 0x22, 0x2E), (0xBF, 0x87, 0x00), (0x1F, 0x88, 0x3D), (0x09, 0x69, 0xDA), (0, 0, 0)]
    alphas = [255, 170, 85, 255, 0]
    idx = []
    for y in range(h):
        for x in range(w):
            if (x // 8 + y // 8) % 2 == 0:
                idx.append(4)  # fully transparent checker squares
            else:
                idx.append(min(3, x // 32))
    write_png_indexed(p("indexed-trns.png"), w, h, idx, palette, alphas)

    # 4. HiDPI image: 400x160 intended to be displayed at 200x80
    cv = Canvas(400, 160, (0xF6, 0xF8, 0xFA, 255))
    for x in range(0, 400, 2):
        cv.rect(x, 0, 1, 24, (0x24, 0x29, 0x2F, 255))  # 1-px hairlines
    for y in range(24, 160, 4):
        cv.rect(0, y, 400, 1, (0xD0, 0xD7, 0xDE, 255))
    cv.text_centered("2x HIDPI", 8, (0x09, 0x69, 0xDA, 255), cy=92)
    write_png(p("hidpi@2x.png"), cv, "RGB")

    # 5. Cyrillic + spaces in the file name
    cv = Canvas(240, 120, (0xDD, 0xF4, 0xFF, 255))
    cv.rect(0, 0, 240, 6, (0x09, 0x69, 0xDA, 255))
    cv.rect(0, 114, 240, 6, (0x09, 0x69, 0xDA, 255))
    cv.text_centered("CYRILLIC", 4, (0x09, 0x69, 0xDA, 255), cy=44)
    cv.text_centered("FILE NAME OK", 3, (0x24, 0x29, 0x2F, 255), cy=84)
    write_png(p("картинка с пробелами.png"), cv, "RGB")

    # 6. JPEG "photo": sky gradient, sun, hills
    cv = Canvas(400, 240)
    for y in range(240):
        sky = lerp3((0x3B, 0x7D, 0xD8), (0xFF, 0xD3, 0x9A), y / 239)
        for x in range(400):
            cv.set(x, y, sky)
    for y in range(240):
        for x in range(400):
            if math.hypot(x - 300, y - 90) < 34:
                cv.set(x, y, (0xFF, 0xF1, 0xB5))
            hill1 = 170 + 25 * math.sin(x / 45.0)
            hill2 = 195 + 18 * math.sin(x / 23.0 + 1.3)
            if y > hill2:
                cv.set(x, y, lerp3((0x1A, 0x7F, 0x37), (0x0B, 0x3D, 0x1A), (y - hill2) / 60))
            elif y > hill1:
                cv.set(x, y, lerp3((0x4A, 0xA3, 0x5A), (0x2A, 0x6F, 0x3A), (y - hill1) / 60))
    cv.text(12, 12, "JPEG 400x240", 2, (255, 255, 255, 255))
    write_jpeg(p("photo.jpg"), cv, quality=85)

    # 7. BMP 24-bit
    cv = Canvas(160, 100)
    for y in range(100):
        for x in range(160):
            cv.set(x, y, (int(255 * x / 159), int(255 * y / 99), 160))
    cv.text_centered("BMP", 5, (255, 255, 255, 255))
    write_bmp(p("palette.bmp"), cv)

    # 8. Animated GIF, 3 frames (red/amber/green "traffic light")
    pal = [(255, 255, 255), (0x24, 0x29, 0x2F), (0xCF, 0x22, 0x2E), (0xBF, 0x87, 0x00),
           (0x1F, 0x88, 0x3D), (0x57, 0x60, 0x6A), (0xD0, 0xD7, 0xDE)] + [(0, 0, 0)] * 9
    frames = []
    size = 96
    for active in range(3):
        f = []
        for y in range(size):
            for x in range(size):
                cidx = 0
                if 30 <= x < 66 and 4 <= y < 92:
                    cidx = 1
                for lamp in range(3):
                    cy = 18 + lamp * 30
                    if math.hypot(x - 47.5, y - cy) < 11:
                        cidx = (2 + lamp) if lamp == active else 5
                f.append(cidx)
        frames.append(f)
    write_gif(p("animated.gif"), size, size, frames, pal, delay_cs=70)

    # 9. SVG files
    with open(p("badge-build-passing.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(badge_svg("build", "passing", 37, 53, "#4c1"))
    with open(p("badge-coverage.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(badge_svg("coverage", "97%", 61, 37, "#97ca00"))
    with open(p("gradients.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(GRADIENT_SVG)
    with open(p("viewbox-only.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(VIEWBOX_SVG)
    print("written to", OUT)
    for name in sorted(os.listdir(OUT)):
        print(f"  {os.path.getsize(os.path.join(OUT, name)):>8}  {name}")


# shields.io "flat" style badge layout (text is drawn twice: shadow + face)
BADGE_SVG = """<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{total}" height="20" role="img" aria-label="{label}: {value}">
  <title>{label}: {value}</title>
  <linearGradient id="s" x2="0" y2="100%">
    <stop offset="0" stop-color="#bbb" stop-opacity=".1"/>
    <stop offset="1" stop-opacity=".1"/>
  </linearGradient>
  <clipPath id="r">
    <rect width="{total}" height="20" rx="3" fill="#fff"/>
  </clipPath>
  <g clip-path="url(#r)">
    <rect width="{lw}" height="20" fill="#555"/>
    <rect x="{lw}" width="{vw}" height="20" fill="{color}"/>
    <rect width="{total}" height="20" fill="url(#s)"/>
  </g>
  <g fill="#fff" text-anchor="middle" font-family="Verdana,Geneva,DejaVu Sans,sans-serif" text-rendering="geometricPrecision" font-size="110">
    <text aria-hidden="true" x="{lx}" y="150" fill="#010101" fill-opacity=".3" transform="scale(.1)">{label}</text>
    <text x="{lx}" y="140" transform="scale(.1)" fill="#fff">{label}</text>
    <text aria-hidden="true" x="{vx}" y="150" fill="#010101" fill-opacity=".3" transform="scale(.1)">{value}</text>
    <text x="{vx}" y="140" transform="scale(.1)" fill="#fff">{value}</text>
  </g>
</svg>
"""


def badge_svg(label, value, lw, vw, color):
    """lw/vw = pixel width of the label/value halves; text is laid out at 10x scale."""
    return BADGE_SVG.format(label=label, value=value, lw=lw, vw=vw, color=color,
                            total=lw + vw, lx=lw * 5, vx=lw * 10 + vw * 5)

GRADIENT_SVG = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#0969da"/>
      <stop offset="100%" stop-color="#8250df"/>
    </linearGradient>
    <radialGradient id="sun" cx="50%" cy="50%" r="50%">
      <stop offset="0%" stop-color="#fff8c5"/>
      <stop offset="60%" stop-color="#d4a72c"/>
      <stop offset="100%" stop-color="#d4a72c" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="ground" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#1f883d"/>
      <stop offset="1" stop-color="#116329"/>
    </linearGradient>
  </defs>
  <rect width="320" height="200" rx="12" fill="url(#sky)"/>
  <circle cx="240" cy="70" r="46" fill="url(#sun)"/>
  <path d="M0 150 C 60 110, 120 190, 180 140 S 280 110, 320 150 L 320 200 L 0 200 Z" fill="url(#ground)"/>
  <path d="M20 40 q 20 -20 40 0 t 40 0" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" opacity="0.8"/>
  <polygon points="60,150 90,95 120,150" fill="#57606a" stroke="#24292f" stroke-width="2" stroke-linejoin="round"/>
  <rect x="140" y="20" width="60" height="40" rx="6" fill="none" stroke="#ffffff" stroke-width="2" stroke-dasharray="6 4"/>
  <g transform="translate(170 40) rotate(20)">
    <ellipse rx="18" ry="8" fill="#ffffff" fill-opacity="0.6"/>
  </g>
  <text x="16" y="188" font-family="Segoe UI, Helvetica, Arial, sans-serif" font-size="14" font-weight="600" fill="#ffffff">SVG: gradients, paths, text</text>
</svg>
"""

VIEWBOX_SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
  <!-- No width/height: intrinsic size must come from the viewBox (or the <img> attributes). -->
  <rect x="2" y="2" width="96" height="96" rx="16" fill="#ddf4ff" stroke="#0969da" stroke-width="4"/>
  <path d="M28 52 L44 68 L74 34" fill="none" stroke="#1f883d" stroke-width="10" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
"""

if __name__ == "__main__":
    main()
