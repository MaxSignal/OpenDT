#!/usr/bin/env python3
"""Generate the internal glyph tables (spec 01 §2.3) from Shinonome PCF fonts.

Usage: gen_font.py shnm8x16r.pcf.gz shnmk16.pcf.gz out.cpp

Output tables:
  half: 256 glyphs x 16 bytes (bit7 = left)
  full: 94*94 glyphs x 32 bytes (2 bytes per row)
The Shinonome fonts are public domain.
"""
import gzip
import struct
import sys

PCF_PROPERTIES = 1 << 0
PCF_ACCELERATORS = 1 << 1
PCF_METRICS = 1 << 2
PCF_BITMAPS = 1 << 3
PCF_BDF_ENCODINGS = 1 << 5
PCF_BDF_ACCELERATORS = 1 << 8

PCF_COMPRESSED_METRICS = 0x100


def load(path):
    data = open(path, 'rb').read()
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    return data


class Reader:
    def __init__(self, data, off, fmt):
        self.d = data
        self.p = off
        self.big = bool(fmt & (1 << 2))

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def i16(self):
        v = struct.unpack_from('>h' if self.big else '<h', self.d, self.p)[0]
        self.p += 2
        return v

    def u16(self):
        v = struct.unpack_from('>H' if self.big else '<H', self.d, self.p)[0]
        self.p += 2
        return v

    def i32(self):
        v = struct.unpack_from('>i' if self.big else '<i', self.d, self.p)[0]
        self.p += 4
        return v


def parse_pcf(data):
    assert data[:4] == b'\x01fcp', 'not a PCF file'
    n = struct.unpack_from('<I', data, 4)[0]
    toc = {}
    for i in range(n):
        t, fmt, size, off = struct.unpack_from('<IIII', data, 8 + 16 * i)
        toc[t] = (fmt, size, off)

    # metrics
    fmt, _, off = toc[PCF_METRICS]
    fmt2 = struct.unpack_from('<I', data, off)[0]
    r = Reader(data, off + 4, fmt2)
    metrics = []
    if fmt2 & PCF_COMPRESSED_METRICS:
        cnt = r.i16()
        for _ in range(cnt):
            lsb = r.u8() - 0x80
            rsb = r.u8() - 0x80
            width = r.u8() - 0x80
            asc = r.u8() - 0x80
            desc = r.u8() - 0x80
            metrics.append((lsb, rsb, width, asc, desc))
    else:
        cnt = r.i32()
        for _ in range(cnt):
            lsb = r.i16(); rsb = r.i16(); width = r.i16(); asc = r.i16(); desc = r.i16(); r.u16()
            metrics.append((lsb, rsb, width, asc, desc))

    # bitmaps
    fmt, _, off = toc[PCF_BITMAPS]
    fmt2 = struct.unpack_from('<I', data, off)[0]
    r = Reader(data, off + 4, fmt2)
    gcount = r.i32()
    offsets = [r.i32() for _ in range(gcount)]
    sizes = [r.i32() for _ in range(4)]
    pad = fmt2 & 3
    bitmap_base = r.p
    msb_bit = bool(fmt2 & (1 << 3))
    msb_byte = bool(fmt2 & (1 << 2))
    scan_unit = 1 << ((fmt2 >> 4) & 3)
    pad_bytes = 1 << pad

    def glyph_pixels(gi):
        lsb, rsb, width, asc, desc = metrics[gi]
        w = rsb - lsb
        h = asc + desc
        stride = ((w + pad_bytes * 8 - 1) // (pad_bytes * 8)) * pad_bytes
        base = bitmap_base + offsets[gi]
        rows = []
        for y in range(h):
            row = bytearray(data[base + y * stride: base + (y + 1) * stride])
            # normalize byte order inside scan units
            if scan_unit > 1 and msb_byte is False:
                for k in range(0, len(row), scan_unit):
                    row[k:k + scan_unit] = row[k:k + scan_unit][::-1]
            pix = []
            for x in range(w):
                b = row[x >> 3]
                bit = (b >> (7 - (x & 7))) & 1 if msb_bit else (b >> (x & 7)) & 1
                pix.append(bit)
            rows.append(pix)
        return lsb, asc, rows

    # encodings
    fmt, _, off = toc[PCF_BDF_ENCODINGS]
    fmt2 = struct.unpack_from('<I', data, off)[0]
    r = Reader(data, off + 4, fmt2)
    min2 = r.i16(); max2 = r.i16(); min1 = r.i16(); max1 = r.i16(); r.i16()
    enc = {}
    for b1 in range(min1, max1 + 1):
        for b2 in range(min2, max2 + 1):
            gi = r.u16()
            if gi != 0xFFFF:
                enc[(b1 << 8) | b2] = gi
    return enc, glyph_pixels


def render(glyph_pixels, gi, cellw):
    lsb, asc, rows = glyph_pixels(gi)
    top = 14 - asc
    cell = [[0] * cellw for _ in range(16)]
    for y, pix in enumerate(rows):
        cy = top + y
        if not (0 <= cy < 16):
            continue
        for x, v in enumerate(pix):
            cx = lsb + x
            if v and 0 <= cx < cellw:
                cell[cy][cx] = 1
    return cell


def main():
    half_path, full_path, out = sys.argv[1:4]
    henc, hgp = parse_pcf(load(half_path))
    fenc, fgp = parse_pcf(load(full_path))

    half = bytearray(256 * 16)
    for c in range(256):
        if c in henc:
            cell = render(hgp, henc[c], 8)
            for y in range(16):
                v = 0
                for x in range(8):
                    v |= cell[y][x] << (7 - x)
                half[c * 16 + y] = v

    full = bytearray(94 * 94 * 32)
    present = 0
    for row in range(0x21, 0x7F):
        for col in range(0x21, 0x7F):
            code = (row << 8) | col
            if code not in fenc:
                continue
            present += 1
            cell = render(fgp, fenc[code], 16)
            idx = ((row - 0x21) * 94 + (col - 0x21)) * 32
            for y in range(16):
                v = 0
                for x in range(16):
                    v |= cell[y][x] << (15 - x)
                full[idx + y * 2] = v >> 8
                full[idx + y * 2 + 1] = v & 0xFF

    with open(out, 'w') as f:
        f.write('// Generated by tools/gen_font.py from the Shinonome bitmap fonts (public domain).\n')
        f.write('// shnm8x16r (JIS X 0201 8x16) and shnmk16 (JIS X 0208 16x16).\n')
        f.write('#include <cstdint>\n#include <cstddef>\nnamespace core {\n')
        for name, arr in (('kFontHalf', half), ('kFontFull', full)):
            f.write('extern const uint8_t %s[%d];\n' % (name, len(arr)))
            f.write('const uint8_t %s[%d] = {\n' % (name, len(arr)))
            for i in range(0, len(arr), 32):
                f.write(','.join(str(b) for b in arr[i:i + 32]) + ',\n')
            f.write('};\n')
        f.write('}\n')
    print('full glyphs present:', present, file=sys.stderr)


if __name__ == '__main__':
    main()
