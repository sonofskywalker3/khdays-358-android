// Synthetic cache bounds and exact grayscale-compositing regressions.
#include "../runtime/mods/font_raster.cpp"

#include <cstdio>
#include <cstring>

using namespace khdays::hd_fonts;
static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("line %d: %s\n", __LINE__, #c);                                                                \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::uint64_t gray_hash()
{
    Atlas atlas;
    atlas.width = atlas.height = 16;
    atlas.cell_width = atlas.cell_height = 8;
    atlas.sized = true;
    atlas.scale = .35;
    atlas.rgba.resize(16 * 16 * 4);
    for (int i = 0; i < 256; ++i) {
        atlas.rgba[i * 4] = atlas.rgba[i * 4 + 1] = atlas.rgba[i * 4 + 2] = static_cast<unsigned char>(i);
        atlas.rgba[i * 4 + 3] = static_cast<unsigned char>((i * 71) % 256);
    }
    Glyph g;
    g.image_w = g.image_h = 16;
    g.width = 8;
    atlas.glyphs.push_back(g);
    std::uint64_t hash = 14695981039346656037ull;
    for (int tint : {0, 1, 17, 127, 254, 255}) {
        for (int opacity : {1, 127, 255}) {
            for (int i = 0; i < 20; ++i) {
                GlyphPaint p;
                p.glyph_index = 0;
                p.logical_width = p.logical_height = 3;
                p.x = (i % 9) * .125 - .25;
                p.y = (i % 5) * .35 - .3;
                p.red = p.green = p.blue = static_cast<unsigned char>(tint);
                p.alpha = static_cast<unsigned char>(opacity);
                std::vector<unsigned char> pixels(32 * 32 * 4);
                for (std::size_t j = 0; j < pixels.size(); j += 4) {
                    pixels[j] = pixels[j + 1] = pixels[j + 2] = static_cast<unsigned char>(j * 23 + i);
                    pixels[j + 3] = i % 2 ? 0 : static_cast<unsigned char>(j * 31 + i);
                }
                CHECK(draw_glyph(atlas, p, pixels.data(), 32, 32, 128, 8));
                for (auto value : pixels)
                    hash = (hash ^ value) * 1099511628211ull;
            }
        }
    }
    return hash;
}

int main(int argc, char **argv)
{
    const auto reference = gray_hash();
    std::printf("Grayscale raster reference: %llu\n", static_cast<unsigned long long>(reference));
    if (argc > 1 && std::strcmp(argv[1], "--hash-only") == 0)
        return failures ? 1 : 0;
    CHECK(reference == 1969680584620278764ull);
    cache_index.clear();
    cache_order.clear();
    cached_bytes = 0;
    Atlas atlas;
    atlas.width = atlas.height = 2;
    atlas.rgba.assign(16, 255);
    Glyph g;
    g.image_w = g.image_h = 2;
    g.width = 128;
    atlas.glyphs.assign(9000, g);
    GlyphPaint p;
    p.glyph_index = 0;
    p.logical_width = p.logical_height = 2;
    p.x = p.y = .25;
    std::vector<unsigned char> pixels(16 * 16 * 4);
    CHECK(draw_glyph(atlas, p, pixels.data(), 16, 16, 64, 1));
    const auto first = pixels;
    CHECK(cache_order.size() == 2);
    p.x += 2;
    p.clip_right = 3;
    p.red = 17;
    CHECK(draw_glyph(atlas, p, pixels.data(), 16, 16, 64, 1));
    CHECK(cache_order.size() == 2); // Integer translation, tint and clipping reuse the samples.
    p.x += .01;
    CHECK(draw_glyph(atlas, p, pixels.data(), 16, 16, 64, 1));
    CHECK(cache_order.size() == 3); // A distinct exact phase stays distinct.
    p.x = .25;
    p.clip_right = 16384;
    p.red = 255;
    for (int i = 0; i < 9000; ++i) {
        p.glyph_index = i;
        CHECK(draw_glyph(atlas, p, pixels.data(), 16, 16, 64, 1));
        CHECK(cache_order.size() <= CACHE_ENTRIES && cached_bytes <= CACHE_LIMIT);
    }
    CHECK(cache_order.size() == CACHE_ENTRIES);
    std::fill(pixels.begin(), pixels.end(), std::uint8_t{0});
    p.glyph_index = 0;
    CHECK(draw_glyph(atlas, p, pixels.data(), 16, 16, 64, 1));
    CHECK(pixels == first); // Eviction cannot alter rendering.
    p.logical_width = p.logical_height = 128;
    for (int i = 0; i < 300; ++i) {
        p.glyph_index = i;
        CHECK(draw_glyph(atlas, p, pixels.data(), 16, 16, 64, 1));
        CHECK(cache_order.size() <= CACHE_ENTRIES && cached_bytes <= CACHE_LIMIT);
    }
    CHECK(cached_bytes > CACHE_LIMIT * 9 / 10);
    std::printf("Raster cache bounds: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
