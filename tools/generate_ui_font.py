#!/usr/bin/env python3
"""Offline LVGL 8.2 4bpp subset font generator; Pillow 7.0.0 used for checked-in output."""
import argparse
import hashlib
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--font', default='/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
args = parser.parse_args()
font_path = Path(args.font)
font = ImageFont.truetype(str(font_path), 16, index=0)
chars = sorted(set(chr(n) for n in range(32, 127)) | set(''.join(
    (root / 'gui/fonts/charset.txt').read_text().split())))
bitmaps = []
glyphs = ['{0,0,0,0,0,0}']
for char in chars:
    image = Image.new('L', (32, 24))
    ImageDraw.Draw(image).text((0, -4), char, font=font, fill=255)
    box = image.getbbox()
    advance = font.getsize(char)[0] * 16
    if box:
        x, y, right, bottom = box
        pixels = [round(p / 17) for p in image.crop(box).getdata()]
        offset = len(bitmaps)
        if len(pixels) % 2:
            pixels.append(0)
        bitmaps.extend((pixels[i] << 4) | pixels[i+1] for i in range(0, len(pixels), 2))
        glyphs.append('{%d,%d,%d,%d,%d,%d}' % (offset, advance, right-x, bottom-y, x, 18-bottom))
    else:
        glyphs.append('{%d,%d,0,0,0,0}' % (len(bitmaps), advance))
source = '''/* Vehicle UI subset; derived from Noto Sans CJK (SIL OFL 1.1).
 * Source SHA256: %s; generated with tools/generate_ui_font.py */
#include "lvgl.h"
static const uint8_t bitmap[] = {%s};
static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {%s};
static const uint16_t unicode_list[] = {%s};
static const lv_font_fmt_txt_cmap_t cmaps[] = {{
 .range_start=32,.range_length=%d,.glyph_id_start=1,
 .unicode_list=unicode_list,.glyph_id_ofs_list=NULL,.list_length=%d,
 .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
}};
static lv_font_fmt_txt_glyph_cache_t cache;
static const lv_font_fmt_txt_dsc_t descriptor = {
 .glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=cmaps,.kern_dsc=NULL,
 .kern_scale=0,.cmap_num=1,.bpp=4,.kern_classes=0,.bitmap_format=0,.cache=&cache
};
const lv_font_t vehicle_ui_16 = {
 .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,
 .line_height=22,.base_line=4,.subpx=LV_FONT_SUBPX_NONE,
 .underline_position=-1,.underline_thickness=1,.dsc=&descriptor
};
''' % (hashlib.sha256(font_path.read_bytes()).hexdigest(),
       ','.join(str(b) for b in bitmaps), ',\n'.join(glyphs),
       ','.join(str(ord(c)-32) for c in chars), ord(chars[-1])-31, len(chars))
(root / 'gui/fonts/vehicle_ui_16.c').write_text(source)
(root / 'gui/fonts/codepoints.txt').write_text(' '.join(str(ord(c)) for c in chars))
print('generated', len(chars), 'glyphs,', len(bitmaps), 'bitmap bytes')
