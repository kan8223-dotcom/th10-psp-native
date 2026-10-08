// Host check for text_table.cpp: builds th10_font32.bin from msgothic.ttc and
// compares it with a table made by tools/fonts/make_font_msgothic.py from the
// same file: SAME, or the header and per-glyph differences.
// usage: text_table_check <msgothic.ttc> <reference th10_font32.bin> [output]
#include "text_table.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace {
std::vector<unsigned char> read_file(const char *path)
{
    std::vector<unsigned char> bytes;
    FILE *f = std::fopen(path, "rb");
    if (f == nullptr)
        return bytes;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bytes.resize(size > 0 ? size_t(size) : 0u);
    if (!bytes.empty() && std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size())
        bytes.clear();
    std::fclose(f);
    return bytes;
}
unsigned int u32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (unsigned(p[3]) << 24); }

struct Glyph {
    int width, height, left, top, advance;
    std::vector<unsigned char> pixels;
};
bool parse(const std::vector<unsigned char> &t, unsigned int header[5], std::map<unsigned int, Glyph> &glyphs)
{
    if (t.size() < 24 || std::memcmp(t.data(), "T10F", 4) != 0)
        return false;
    for (int i = 0; i < 5; ++i)
        header[i] = u32(t.data() + 4 + 4 * i);
    const unsigned int count = header[3];
    const size_t data = 24 + size_t(count) * 12;
    if (data + header[4] > t.size())
        return false;
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned char *e = t.data() + 24 + size_t(i) * 12;
        Glyph g{e[2], e[3], (signed char)e[4], (signed char)e[5], e[6], {}};
        const size_t offset = data + u32(e + 8), bytes = size_t(g.width) * g.height;
        if (offset + bytes > t.size())
            return false;
        g.pixels.assign(t.begin() + offset, t.begin() + offset + bytes);
        glyphs[e[0] | (e[1] << 8)] = g;
    }
    return true;
}
}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <msgothic.ttc> <reference table> [output]\n", argv[0]);
        return 64;
    }
    const std::vector<unsigned char> ttc = read_file(argv[1]), reference = read_file(argv[2]);
    if (ttc.empty() || reference.empty()) {
        std::fprintf(stderr, "cannot read the inputs\n");
        return 66;
    }
    unsigned char *table = nullptr;
    size_t bytes = 0;
    const int result = th10_text_table_build(ttc.data(), ttc.size(), &table, &bytes, nullptr);
    if (result != TH10_TEXT_TABLE_OK) {
        std::fprintf(stderr, "th10_text_table_build failed: %d\n", result);
        return 1;
    }
    const std::vector<unsigned char> made(table, table + bytes);
    std::free(table);
    if (argc > 3) {
        if (FILE *f = std::fopen(argv[3], "wb")) {
            std::fwrite(made.data(), 1, made.size(), f);
            std::fclose(f);
        }
    }
    if (made == reference) {
        std::printf("SAME: %zu bytes\n", made.size());
        return 0;
    }
    unsigned int hm[5], hr[5];
    std::map<unsigned int, Glyph> gm, gr;
    if (!parse(made, hm, gm) || !parse(reference, hr, gr)) {
        std::printf("DIFF: a table does not parse (made %zu bytes, reference %zu bytes)\n", made.size(), reference.size());
        return 1;
    }
    std::printf("DIFF: made %zu bytes, reference %zu bytes\n", made.size(), reference.size());
    std::printf("header made    version=%u height=%u ascent=%u count=%u data=%u\n", hm[0], hm[1], hm[2], hm[3], hm[4]);
    std::printf("header reference version=%u height=%u ascent=%u count=%u data=%u\n", hr[0], hr[1], hr[2], hr[3], hr[4]);
    unsigned int box = 0, pixels = 0, missing = 0, shown = 0;
    for (const auto &[code, r] : gr) {
        const auto it = gm.find(code);
        if (it == gm.end()) {
            ++missing;
            continue;
        }
        const Glyph &m = it->second;
        const bool same_box = m.width == r.width && m.height == r.height && m.left == r.left &&
                              m.top == r.top && m.advance == r.advance;
        if (!same_box)
            ++box;
        else if (m.pixels != r.pixels)
            ++pixels;
        if ((!same_box || m.pixels != r.pixels) && shown++ < 12)
            std::printf("  %04X made w%d h%d l%d t%d a%d | reference w%d h%d l%d t%d a%d%s\n", code,
                        m.width, m.height, m.left, m.top, m.advance, r.width, r.height, r.left, r.top,
                        r.advance, same_box ? " (pixels differ)" : "");
    }
    std::printf("glyphs: %zu reference, %u missing, %u box differences, %u pixel differences\n",
                gr.size(), missing, box, pixels);
    return 1;
}
