// th10_port: see text_table.hpp. Each step names the Pillow 12.1.1 code
// (src/_imagingft.c) it mirrors; tools/fonts/make_font_msgothic.py is the
// reference and host_check/text_table_check.cpp compares the two.
#include "text_table.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_BITMAP_H
#include FT_GLYPH_H

#include <stdlib.h>
#include <string.h>

namespace {

struct CharsetCode {
    unsigned short code;
    unsigned int unicode;
};
// charset_th10.bin decoded with Python's cp932 codec, in file order.
const CharsetCode kCharset[] = {
#include "charset_th10_unicode.inc"
};
constexpr unsigned int kCodes = sizeof(kCharset) / sizeof(kCharset[0]);
constexpr unsigned int kUndecodable = 0xFFFFFFFFu;
constexpr int kEm = 32;               // ImageFont.truetype(msgothic.ttc, 32)
constexpr size_t kHeaderBytes = 24u;  // "T10F", version, height, ascent, count, data bytes
constexpr size_t kEntryBytes = 12u;   // <HBBbbBBI

struct Entry {
    unsigned short code;
    unsigned char width, height;
    signed char left, top;
    unsigned char advance;
    unsigned int offset;
};

// PIXEL(): a 26.6 value rounded to the nearest pixel.
inline int pixel(long value) { return static_cast<int>(((value + 32) & -64) >> 6); }

// int(round(length / 64)) in Python: halves go to the even neighbour.
int round_advance(long value)
{
    long whole = value >> 6;
    const long rest = value & 63;
    if (rest > 32 || (rest == 32 && (whole & 1) != 0))
        ++whole;
    return static_cast<int>(whole);
}

inline unsigned int clip8(unsigned int value) { return value < 256u ? value : 255u; }
inline unsigned int muldiv255(unsigned int a, unsigned int b)
{
    const unsigned int t = a * b + 128u;
    return ((t >> 8) + t) >> 8;
}

void put16(unsigned char *p, unsigned int v) { p[0] = static_cast<unsigned char>(v); p[1] = static_cast<unsigned char>(v >> 8); }
void put32(unsigned char *p, unsigned int v) { put16(p, v & 0xffffu); put16(p + 2, v >> 16); }

int by_code(const void *a, const void *b)
{
    const Entry &x = *static_cast<const Entry *>(a), &y = *static_cast<const Entry *>(b);
    if (x.code != y.code)
        return x.code < y.code ? -1 : 1;
    return x.offset < y.offset ? -1 : x.offset > y.offset ? 1 : 0;
}

// font_render() for one glyph into a zeroed width x height canvas: the
// glyph's bitmap box places it, rows and columns are clipped to the canvas.
int render_glyph(FT_Library library, FT_Face face, FT_UInt glyph,
                 unsigned char *canvas, int width, int height)
{
    if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT | FT_LOAD_RENDER) != 0)
        return TH10_TEXT_TABLE_GLYPH;
    const FT_GlyphSlot slot = face->glyph;
    const int x_min = slot->bitmap_left < 0 ? slot->bitmap_left : 0;
    const int y_max = slot->bitmap_top > 0 ? slot->bitmap_top : 0;
    const int xx = slot->bitmap_left - x_min;
    int yy = y_max - slot->bitmap_top;

    FT_Bitmap bitmap = slot->bitmap;
    FT_Bitmap converted;
    bool have_converted = false;
    unsigned int scale = 1u;
    switch (bitmap.pixel_mode) {
    case FT_PIXEL_MODE_MONO: scale = 255u; break;
    case FT_PIXEL_MODE_GRAY2: scale = 255u / 3u; break;
    case FT_PIXEL_MODE_GRAY4: scale = 255u / 15u; break;
    default: break;
    }
    switch (bitmap.pixel_mode) {
    case FT_PIXEL_MODE_MONO:
    case FT_PIXEL_MODE_GRAY2:
    case FT_PIXEL_MODE_GRAY4:
        FT_Bitmap_Init(&converted);
        have_converted = true;
        if (FT_Bitmap_Convert(library, &bitmap, &converted, 1) != 0) {
            FT_Bitmap_Done(library, &converted);
            return TH10_TEXT_TABLE_GLYPH;
        }
        bitmap = converted;
        break;
    case FT_PIXEL_MODE_GRAY:
        break;
    default:
        return TH10_TEXT_TABLE_GLYPH;
    }
    if (bitmap.buffer == NULL && bitmap.rows != 0) {
        if (have_converted)
            FT_Bitmap_Done(library, &converted);
        return TH10_TEXT_TABLE_GLYPH;
    }

    int x0 = 0, x1 = static_cast<int>(bitmap.width);
    if (xx < 0)
        x0 = -xx;
    if (xx + x1 > width)
        x1 = width - xx;
    const unsigned char *source = bitmap.buffer;
    for (unsigned int row = 0; row < bitmap.rows; ++row, ++yy) {
        if (yy >= 0 && yy < height) {
            unsigned char *target = canvas + static_cast<size_t>(yy) * width + xx;
            for (int k = x0; k < x1; ++k) {
                const unsigned int alpha = source[k] * scale;
                if (alpha > 0u)
                    target[k] = static_cast<unsigned char>(
                        target[k] > 0u ? clip8(alpha + muldiv255(target[k], 255u - alpha)) : alpha);
            }
        }
        source += bitmap.pitch;
    }
    if (have_converted)
        FT_Bitmap_Done(library, &converted);
    return TH10_TEXT_TABLE_OK;
}

}  // namespace

int th10_text_table_build(const unsigned char *ttc, size_t ttc_bytes,
                          unsigned char **out, size_t *out_bytes,
                          Th10TextTableProgress progress)
{
    if (out == NULL || out_bytes == NULL || ttc == NULL || ttc_bytes == 0u)
        return TH10_TEXT_TABLE_FREETYPE;
    *out = NULL;
    *out_bytes = 0u;

    Entry *entries = static_cast<Entry *>(malloc(sizeof(Entry) * kCodes));
    size_t data_capacity = 1024u * 1024u, data_bytes = 0u;
    unsigned char *data = static_cast<unsigned char *>(malloc(data_capacity));
    FT_Library library = NULL;
    FT_Face face = NULL;
    int result = TH10_TEXT_TABLE_OK;
    int ascent = 0;
    unsigned int count = 0;
    if (entries == NULL || data == NULL) {
        result = TH10_TEXT_TABLE_NO_MEMORY;
        goto done;
    }
    if (FT_Init_FreeType(&library) != 0 ||
        FT_New_Memory_Face(library, ttc, static_cast<FT_Long>(ttc_bytes), 0, &face) != 0) {
        result = TH10_TEXT_TABLE_FREETYPE;
        goto done;
    }
    if (face->family_name == NULL || strcmp(face->family_name, "MS Gothic") != 0) {
        result = TH10_TEXT_TABLE_NOT_MS_GOTHIC;
        goto done;
    }
    {
        // getfont(): a nominal size request in 26.6 pixels, resolution 0.
        FT_Size_RequestRec request;
        memset(&request, 0, sizeof(request));
        request.type = FT_SIZE_REQUEST_TYPE_NOMINAL;
        request.width = kEm * 64;
        request.height = kEm * 64;
        if (FT_Request_Size(face, &request) != 0) {
            result = TH10_TEXT_TABLE_FREETYPE;
            goto done;
        }
    }
    ascent = pixel(face->size->metrics.ascender);  // font.getmetrics()[0]

    for (unsigned int i = 0; i < kCodes; ++i) {
        if (progress != NULL)
            progress(i, kCodes);
        if (kCharset[i].unicode == kUndecodable)
            continue;  // b.decode('cp932') failed: skipped by the PC tool too
        const FT_UInt glyph = FT_Get_Char_Index(face, kCharset[i].unicode);

        // text_layout_raqm(): HarfBuzz's FreeType functions give the advance
        // unhinted (FT_LOAD_NO_HINTING), from 16.16 to 26.6 pixels.
        FT_Fixed advance16 = 0;
        if (FT_Get_Advance(face, glyph, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING, &advance16) != 0) {
            result = TH10_TEXT_TABLE_GLYPH;
            goto done;
        }
        if (advance16 < 0)
            advance16 = -advance16;
        const long advance = static_cast<long>((advance16 + (1 << 9)) >> 10);

        // bounding_box_and_anchors(): the hinted outline's pixel box, plus the
        // pen line (0 to the advance) and the baseline.
        int x_min = 0, x_max = 0, y_min = 0, y_max = 0;
        if (pixel(advance) > x_max)
            x_max = pixel(advance);
        FT_Glyph outline = NULL;
        if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) != 0 ||
            FT_Get_Glyph(face->glyph, &outline) != 0) {
            result = TH10_TEXT_TABLE_GLYPH;
            goto done;
        }
        FT_BBox box;
        FT_Glyph_Get_CBox(outline, FT_GLYPH_BBOX_PIXELS, &box);
        FT_Done_Glyph(outline);
        if (box.xMax > x_max) x_max = static_cast<int>(box.xMax);
        if (box.xMin < x_min) x_min = static_cast<int>(box.xMin);
        if (box.yMax > y_max) y_max = static_cast<int>(box.yMax);
        if (box.yMin < y_min) y_min = static_cast<int>(box.yMin);
        const int width = x_max - x_min, height = y_max - y_min;
        const int left = x_min, top = ascent - y_max;  // anchor "la"
        const int rounded_advance = round_advance(advance);
        if (width > 255 || height > 255 || left < -128 || left > 127 ||
            top < -128 || top > 127 || rounded_advance < 0 || rounded_advance > 255) {
            result = TH10_TEXT_TABLE_LIMIT;
            goto done;
        }

        const size_t pixels = static_cast<size_t>(width) * height;
        if (data_bytes + pixels > data_capacity) {
            size_t grown = data_capacity * 2u;
            while (grown < data_bytes + pixels)
                grown *= 2u;
            unsigned char *larger = static_cast<unsigned char *>(realloc(data, grown));
            if (larger == NULL) {
                result = TH10_TEXT_TABLE_NO_MEMORY;
                goto done;
            }
            data = larger;
            data_capacity = grown;
        }
        memset(data + data_bytes, 0, pixels);
        if (pixels != 0u) {
            result = render_glyph(library, face, glyph, data + data_bytes, width, height);
            if (result != TH10_TEXT_TABLE_OK)
                goto done;
        }
        Entry &entry = entries[count++];
        entry.code = kCharset[i].code;
        entry.width = static_cast<unsigned char>(width);
        entry.height = static_cast<unsigned char>(height);
        entry.left = static_cast<signed char>(left);
        entry.top = static_cast<signed char>(top);
        entry.advance = static_cast<unsigned char>(rounded_advance);
        entry.offset = static_cast<unsigned int>(data_bytes);
        data_bytes += pixels;
    }
    if (progress != NULL)
        progress(kCodes, kCodes);

    // table.sort(): by code (then by data offset, the only other field that
    // can differ); the data stays in character-list order.
    qsort(entries, count, sizeof(Entry), by_code);
    {
        const size_t bytes = kHeaderBytes + kEntryBytes * count + data_bytes;
        unsigned char *table = static_cast<unsigned char *>(malloc(bytes));
        if (table == NULL) {
            result = TH10_TEXT_TABLE_NO_MEMORY;
            goto done;
        }
        memcpy(table, "T10F", 4);
        put32(table + 4, 1u);
        put32(table + 8, static_cast<unsigned int>(kEm));
        put32(table + 12, static_cast<unsigned int>(ascent));
        put32(table + 16, count);
        put32(table + 20, static_cast<unsigned int>(data_bytes));
        unsigned char *p = table + kHeaderBytes;
        for (unsigned int i = 0; i < count; ++i, p += kEntryBytes) {
            put16(p, entries[i].code);
            p[2] = entries[i].width;
            p[3] = entries[i].height;
            p[4] = static_cast<unsigned char>(entries[i].left);
            p[5] = static_cast<unsigned char>(entries[i].top);
            p[6] = entries[i].advance;
            p[7] = 0u;
            put32(p + 8, entries[i].offset);
        }
        memcpy(p, data, data_bytes);
        *out = table;
        *out_bytes = bytes;
    }

done:
    if (face != NULL)
        FT_Done_Face(face);
    if (library != NULL)
        FT_Done_FreeType(library);
    free(data);
    free(entries);
    return result;
}
