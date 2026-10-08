#!/usr/bin/env python3
# Pre-render the glyphs TH10 draws from Noto Sans CJK JP (SIL Open Font License
# 1.1) into th10_font32.bin, in the layout make_font_msgothic.py writes and
# with MS Gothic's metrics, which the game's text layout assumes: a 32 px em,
# the baseline 28 px below the line's top, half-width forms (OpenType 'hwid',
# advance 16) for the single-byte codes and full-width forms ('fwid', advance
# 32) for the double-byte ones. The table is refused when a glyph is missing
# or an advance differs from those.
#
# The input is the pinned NotoSansJP-Regular.ttf (Noto Sans CJK JP 2.004,
# OpenType/CFF, the size and SHA-256 below), published with its OFL.txt at
#   https://github.com/kan8223-dotcom/th08-psp-native/raw/540e48f7ca767f7278be6fce082e3e01208ba883/psp/assets/NotoSansJP-Regular.ttf
#   https://github.com/kan8223-dotcom/th08-psp-native/raw/540e48f7ca767f7278be6fce082e3e01208ba883/licenses/NotoSansJP/OFL.txt
# The output is a Modified Version of that font: share it only together with
# OFL.txt (unlike a table made from MS Gothic, which must not be shared).
#
# usage: make_font_noto.py <NotoSansJP-Regular.ttf> <th10_font32.bin to write> [charset_th10.bin]
# charset_th10.bin: the Shift_JIS codes TH10's texts use (1 or 2 bytes each, concatenated).
# Needs Pillow with FreeType and RAQM (python3 -c "from PIL import features; print(features.check('raqm'))").
import hashlib, os, struct, sys
from PIL import ImageFont, features

SOURCE_BYTES = 4_491_696
SOURCE_SHA256 = '6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05'
EM = 32          # TTF_OpenFont(msgothic.ttc, 32) in the SDL port
ASCENT = 28      # MS Gothic's ascender at 32 px: where the table's glyph tops are measured from

def main():
    if len(sys.argv) < 3:
        sys.exit('usage: make_font_noto.py <NotoSansJP-Regular.ttf> <th10_font32.bin> [charset_th10.bin]')
    font_path, out_path = sys.argv[1], sys.argv[2]
    charset = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'charset_th10.bin')
    source = open(font_path, 'rb').read()
    digest = hashlib.sha256(source).hexdigest()
    if len(source) != SOURCE_BYTES or digest != SOURCE_SHA256:
        sys.exit(f'not the pinned Noto Sans CJK JP 2.004 file: {len(source)} bytes, sha256 {digest}')
    if not features.check('raqm'):
        sys.exit('Pillow lacks RAQM (needed for the hwid/fwid forms)')
    font = ImageFont.truetype(font_path, EM, layout_engine=ImageFont.Layout.RAQM)
    cs = open(charset, 'rb').read(); codes = []; i = 0
    while i < len(cs):
        c = cs[i]
        if 0x81 <= c <= 0x9f or 0xe0 <= c <= 0xfc: codes.append(cs[i:i + 2]); i += 2
        else: codes.append(cs[i:i + 1]); i += 1
    from fontTools.ttLib import TTFont   # only to tell a missing glyph from a drawn one
    cmap = TTFont(font_path, lazy=True).getBestCmap()
    table = []; data = bytearray(); bad = 0; missing = []; wrong_advance = []
    for b in codes:
        try: ch = b.decode('cp932')
        except Exception: bad += 1; continue
        code = b[0] if len(b) == 1 else (b[0] << 8) | b[1]
        # Private-use codes (cp932 F040-F9FC) have no glyph in either font: like
        # make_font_msgothic.py (MS Gothic's box), they get the font's .notdef box.
        if any(ord(c) not in cmap and not 0xe000 <= ord(c) <= 0xf8ff for c in ch): missing.append(f'{code:x}:{ch}'); continue
        form = ['hwid'] if len(b) == 1 else ['fwid']
        width = 16 if len(b) == 1 else 32
        adv = int(round(font.getlength(ch, features=form)))
        if adv != width: wrong_advance.append(f'{code:x}:{ch}:{adv}'); continue
        mask, (ox, oy) = font.getmask2(ch, mode='L', anchor='ls', features=form)
        w, h = mask.size; top = oy + ASCENT
        if not (0 <= w <= 255 and 0 <= h <= 255 and -128 <= ox <= 127 and -128 <= top <= 127):
            wrong_advance.append(f'{code:x}:{ch}:box'); continue
        table.append((code, w, h, ox, top, adv, len(data))); data += bytes(mask)
    if missing or wrong_advance:
        sys.exit(f'refused: missing {missing}, advance/box {wrong_advance}')
    table.sort()
    out = bytearray(b'T10F') + struct.pack('<IIIII', 1, EM, ASCENT, len(table), len(data))
    for code, w, h, ox, top, adv, off in table: out += struct.pack('<HBBbbBBI', code, w, h, ox, top, adv, 0, off)
    out += data
    open(out_path, 'wb').write(out)
    print('glyphs', len(table), 'undecodable', bad, 'ascent', ASCENT, 'bytes', len(out))

if __name__ == '__main__':
    main()
