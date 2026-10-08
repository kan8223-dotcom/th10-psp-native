// th10_port: the text table th10_font32.bin made on the PSP from the user's own
// MS Gothic (face 0 of msgothic.ttc). It follows what
// tools/fonts/make_font_msgothic.py does through Pillow 12.1.1 (FreeType at
// 32 px, RAQM layout, anchor "la"), so with the same FreeType it writes the
// same bytes as the PC tool.
#pragma once
#include <stddef.h>

// Called before each code of the character list with (done, total).
typedef void (*Th10TextTableProgress)(unsigned int done, unsigned int total);

enum {
    TH10_TEXT_TABLE_OK = 0,
    TH10_TEXT_TABLE_NO_MEMORY = -1,
    TH10_TEXT_TABLE_FREETYPE = -2,      // library, face or size setup failed
    TH10_TEXT_TABLE_NOT_MS_GOTHIC = -3, // face 0 is another family
    TH10_TEXT_TABLE_GLYPH = -4,         // a glyph failed to load or render
    TH10_TEXT_TABLE_LIMIT = -5,         // a glyph box does not fit the table's fields
};

// On success *out holds the table (malloc'd; the caller frees it) and
// *out_bytes its size. The TTC bytes must stay valid during the call.
int th10_text_table_build(const unsigned char *ttc, size_t ttc_bytes,
                          unsigned char **out, size_t *out_bytes,
                          Th10TextTableProgress progress);
