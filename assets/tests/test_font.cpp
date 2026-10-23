#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/font.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

void set_u16(Bytes& b, std::size_t o, std::uint16_t v) {
    b.at(o) = static_cast<std::uint8_t>(v & 0xFFU);
    b.at(o + 1U) = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}
void set_u32(Bytes& b, std::size_t o, std::uint32_t v) {
    b.at(o) = static_cast<std::uint8_t>(v & 0xFFU);
    b.at(o + 1U) = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
    b.at(o + 2U) = static_cast<std::uint8_t>((v >> 16U) & 0xFFU);
    b.at(o + 3U) = static_cast<std::uint8_t>((v >> 24U) & 0xFFU);
}

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

void expect_invalid(const Bytes& bytes, const char* what) {
    try {
        (void)khdays::assets::decode_nftr(bytes);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(what);
}

void malformed_fonts(const Bytes& valid) {
    for (const std::size_t size : {0U, 0x20U, 0x2BU, 0x47U, 0x48U, 0x65U, 0x69U, 0x79U, 0x7DU}) {
        expect_invalid(Bytes(valid.begin(), valid.begin() + size), "truncated font accepted");
    }
    for (const std::size_t field : {0x20U, 0x24U, 0x28U}) {
        for (const std::uint32_t pointer : {1U, 0xFFFFFFFFU}) {
            auto bad = valid;
            set_u32(bad, field, pointer);
            expect_invalid(bad, "invalid section pointer accepted");
        }
    }
    for (const std::uint32_t size : {0U, 0xFU, 0xFFFFFFFFU}) {
        auto bad = valid;
        set_u32(bad, 0x3CU, size);
        expect_invalid(bad, "invalid bitmap section size accepted");
    }
    for (const int bpp : {0, 9, 32, 255}) {
        auto bad = valid;
        bad[0x46U] = static_cast<std::uint8_t>(bpp);
        expect_invalid(bad, "invalid bit depth accepted");
    }
    auto bad = valid;
    set_u16(bad, 0x42U, 0U);
    expect_invalid(bad, "zero bitmap stride accepted");
    bad = valid;
    bad[0x40U] = 9;
    expect_invalid(bad, "undersized bitmap stride accepted");
    bad = valid;
    set_u32(bad, 0x64U, 0x60U);
    expect_invalid(bad, "cyclic width chain accepted");
    bad = valid;
    set_u16(bad, 0x62U, 1U);
    expect_invalid(bad, "out-of-bounds width index accepted");
    bad = valid;
    set_u32(bad, 0x78U, 0x70U);
    expect_invalid(bad, "cyclic character map accepted");
    for (const int method : {0, 1, 2}) {
        bad = valid;
        set_u16(bad, 0x74U, static_cast<std::uint16_t>(method));
        if (method == 2U) {
            set_u16(bad, 0x7CU, 1U);
            set_u16(bad, 0x7EU, u'A');
            bad.resize(0x82U);
            set_u16(bad, 0x80U, 0U);
        }
        expect(khdays::assets::decode_nftr(bad).glyph_of(u'A') == 0, "CMAP addressing mode");
        set_u16(bad, method == 2U ? 0x80U : 0x7CU, 1U);
        expect_invalid(bad, "invalid mapped glyph accepted");
        bad.resize(method == 2U ? 0x81U : 0x7DU);
        expect_invalid(bad, "truncated character map accepted");
    }
    bad = valid;
    set_u16(bad, 0x74U, 3U);
    expect_invalid(bad, "unknown character map accepted");
}

}  // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() / "khdays_test.nftr";
    try {
        // Minimal NFTR: one 2x2 1bpp glyph mapped from 'A', advance 3.
        Bytes d(0x80, 0U);
        d[0] = 'R';
        d[1] = 'T';
        d[2] = 'F';
        d[3] = 'N';
        d[0x19] = 2;               // line feed
        set_u32(d, 0x20U, 0x40U);  // CGLP data pointer
        set_u32(d, 0x24U, 0x60U);  // CWDH data pointer
        set_u32(d, 0x28U, 0x70U);  // CMAP data pointer

        set_u32(d, 0x3CU, 0x11U);  // CGLP section size (read at cglp-4)
        d[0x40U] = 2;              // cell width
        d[0x41U] = 2;              // cell height
        set_u16(d, 0x42U, 1U);     // cell size (1 byte per glyph)
        d[0x44U] = 1;              // baseline
        d[0x45U] = 2;              // maximum character width
        d[0x46U] = 1;              // bpp
        d[0x48U] = 0xA0U;          // glyph 0: pixels 1,0,1,0 (MSB first)

        set_u16(d, 0x60U, 0U);  // CWDH first glyph
        set_u16(d, 0x62U, 0U);  // CWDH last glyph
        set_u32(d, 0x64U, 0U);  // next
        d[0x68U] = 0;           // left
        d[0x69U] = 2;           // width
        d[0x6AU] = 3;           // advance

        set_u16(d, 0x70U, 0x41U);  // CMAP first char 'A'
        set_u16(d, 0x72U, 0x41U);  // CMAP last char 'A'
        set_u32(d, 0x74U, 0U);     // method 0 (direct)
        set_u32(d, 0x78U, 0U);     // next
        set_u16(d, 0x7CU, 0U);     // first glyph index

        {
            std::ofstream out{path, std::ios::binary};
            out.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
        }

        const auto font = khdays::assets::decode_nftr(path);
        expect(font.glyphs.size() == 1U, "glyph count");
        expect(font.cell_width == 2 && font.cell_height == 2, "cell size");
        expect(font.bpp == 1, "bpp");
        expect(font.baseline == 1 && font.max_width == 2, "exported cell metrics");
        expect(font.glyph_of(u'A') == 0, "char map");
        expect(font.glyph_of(u'B') == -1, "unmapped char");
        expect(font.glyphs[0].advance == 3, "advance");
        expect(font.glyphs[0].alpha[0] == 255 && font.glyphs[0].alpha[1] == 0 && font.glyphs[0].alpha[2] == 255,
               "glyph bitmap");

        const auto image = khdays::assets::render_text(font, u"A");
        expect(image.width == 3 && image.height == 2, "text image size");
        expect(image.rgba[3] == 255, "pixel (0,0) opaque");  // alpha
        expect(image.rgba[4 + 3] == 0, "pixel (1,0) transparent");

        const auto two = khdays::assets::render_text(font, u"AA");
        expect(two.width == 6, "two-glyph width");
        const auto buffered = khdays::assets::decode_nftr(d);
        expect(khdays::assets::render_text(buffered, u"AA").rgba == two.rgba, "buffer and file decoding agree");
        malformed_fonts(d);

        std::filesystem::remove(path);
        std::cout << "Font decoder test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << "Font decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
