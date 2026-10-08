#!/usr/bin/env python3
# Pre-render the MS Gothic glyphs TH10 draws (32 px, as the SDL port's
# TTF_OpenFont(msgothic.ttc, 32)) into th10_font32.bin: per glyph the 8-bit
# coverage, its offset from the ascender line and the advance.
#
# The font comes from your own Windows installation (C:\Windows\Fonts\msgothic.ttc);
# the output is for your own use and must not be redistributed.
#
# usage: make_font_msgothic.py <msgothic.ttc> <th10_font32.bin to write> [charset_th10.bin]
# charset_th10.bin: the Shift_JIS codes TH10's texts use (1 or 2 bytes each, concatenated).
import os, struct, sys
from PIL import ImageFont

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__ or 'usage: make_font_msgothic.py <msgothic.ttc> <th10_font32.bin> [charset_th10.bin]')
    ttc, out_path = sys.argv[1], sys.argv[2]
    charset = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'charset_th10.bin')
    font = ImageFont.truetype(ttc, 32, index=0)
    cs = open(charset, 'rb').read(); codes = []; i = 0
    while i < len(cs):
        c = cs[i]
        if 0x81 <= c <= 0x9f or 0xe0 <= c <= 0xfc: codes.append(cs[i:i + 2]); i += 2
        else: codes.append(cs[i:i + 1]); i += 1
    table = []; data = bytearray(); ascent, descent = font.getmetrics(); bad = 0
    for b in codes:
        try: ch = b.decode('cp932')
        except Exception: bad += 1; continue
        code = b[0] if len(b) == 1 else (b[0] << 8) | b[1]
        mask, (ox, oy) = font.getmask2(ch, mode='L', anchor='la')
        w, h = mask.size; adv = int(round(font.getlength(ch)))
        table.append((code, w, h, ox, oy, adv, len(data))); data += bytes(mask)
    table.sort()
    out = bytearray(b'T10F') + struct.pack('<IIIII', 1, 32, ascent, len(table), len(data))
    for code, w, h, ox, oy, adv, off in table: out += struct.pack('<HBBbbBBI', code, w, h, ox, oy, adv, 0, off)
    out += data
    open(out_path, 'wb').write(out)
    print('glyphs', len(table), 'undecodable', bad, 'ascent', ascent, 'descent', descent, 'bytes', len(out))

if __name__ == '__main__':
    main()
