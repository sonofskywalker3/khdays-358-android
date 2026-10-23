// Synthetic atlas tests: arbitrary rectangles, PNG alpha, logical size and clipping.
#include "../runtime/mods/font_raster.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <vector>

static int failures;
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

int main()
{
    using namespace khdays::hd_fonts;
    Atlas atlas;
    atlas.width = 8;
    atlas.height = 4;
    atlas.rgba.assign(8 * 4 * 4, 255);
    Glyph glyph;
    glyph.image_x = 2;
    glyph.image_y = 1;
    glyph.image_w = 2;
    glyph.image_h = 2;
    atlas.glyphs.push_back(glyph);
    Glyph blank = glyph;
    blank.image_x = 3;
    blank.image_y = 1;
    blank.image_w = blank.image_h = 1;
    atlas.glyphs.push_back(blank);
    atlas.charmap.push_back({0x41, 0});
    auto texel = [&](int x, int y, int r, int g, int b, int a) {
        auto *p = &atlas.rgba[(y * 8 + x) * 4];
        p[0] = static_cast<unsigned char>(r);
        p[1] = static_cast<unsigned char>(g);
        p[2] = static_cast<unsigned char>(b);
        p[3] = static_cast<unsigned char>(a);
    };
    texel(2, 1, 255, 255, 255, 128);
    texel(3, 1, 255, 0, 0, 0); // RGB under transparent alpha must never leak.
    texel(2, 2, 0, 255, 0, 255);
    texel(3, 2, 0, 0, 255, 255);
    GlyphPaint paint;
    paint.glyph_index = find_glyph(atlas, 0x41);
    paint.x = 1;
    paint.y = 1;
    paint.logical_width = paint.logical_height = 2;
    paint.red = 128;
    paint.green = 64;
    paint.blue = 255;
    std::vector<unsigned char> out(4 * 4 * 4, 0);
    CHECK(draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    const auto *p = &out[(1 * 4 + 1) * 4];
    CHECK(p[0] >= 127 && p[0] <= 129);
    CHECK(p[1] >= 63 && p[1] <= 65);
    CHECK(p[2] == 255);
    CHECK(p[3] == 128);
    CHECK(out[(1 * 4 + 2) * 4 + 3] == 0);
    CHECK(out[(2 * 4 + 1) * 4 + 1] == 64);
    CHECK(out[(2 * 4 + 2) * 4 + 2] == 255);
    CHECK(out[3] == 0);
    CHECK(find_glyph(atlas, 0x42) == -1);
    auto cached = out;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    CHECK(draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    CHECK(out == cached);
    // Synthetic KH2 spacing failure: an 18-unit advance beside a Days-fitted
    // 6x8 image leaves a 12-unit gap; source-sized artwork occupies the same 18 units.
    Atlas kh2;
    kh2.width = kh2.cell_width = 18;
    kh2.height = kh2.cell_height = 24;
    kh2.rgba.assign(18 * 24 * 4, 255);
    Glyph source;
    source.w = 18;
    source.h = 24;
    source.image_w = 18;
    source.image_h = 24;
    source.width = source.advance = 18;
    kh2.glyphs.push_back(source);
    GlyphPaint source_paint;
    source_paint.glyph_index = 0;
    source_paint.logical_width = 7;
    source_paint.logical_height = 8;
    std::vector<unsigned char> before(48 * 48 * 4), after(before.size());
    auto alpha_count = [](const std::vector<unsigned char> &pixels) {
        int count = 0;
        for (std::size_t i = 3; i < pixels.size(); i += 4)
            count += pixels[i] != 0;
        return count;
    };
    CHECK(draw_glyph(kh2, source_paint, before.data(), 48, 48, 192, 1));
    CHECK(alpha_count(before) == 6 * 8);
    kh2.sized = true;
    CHECK(draw_glyph(kh2, source_paint, after.data(), 48, 48, 192, 1));
    CHECK(alpha_count(after) == 18 * 24);
    CHECK(after[(23 * 48 + 17) * 4 + 3] == 255);
    CHECK(layout_units(kh2, source.advance) == 18);
    kh2.scale = 0.5;
    std::fill(after.begin(), after.end(), std::uint8_t{0});
    CHECK(draw_glyph(kh2, source_paint, after.data(), 48, 48, 192, 1));
    CHECK(alpha_count(after) == 9 * 12 && layout_units(kh2, source.advance) == 9);
    // Event cells retain their 3:4 ratio while exceeding the replaced 10x10 cell.
    Atlas event;
    event.width = event.cell_width = 24;
    event.height = event.cell_height = 32;
    event.sized = true;
    event.scale = 0.5;
    event.rgba.assign(24 * 32 * 4, 255);
    source.image_w = source.w = 24;
    source.image_h = source.h = 32;
    source.width = source.advance = 24;
    event.glyphs.push_back(source);
    source_paint.logical_width = source_paint.logical_height = 10;
    std::fill(after.begin(), after.end(), std::uint8_t{0});
    CHECK(draw_glyph(event, source_paint, after.data(), 48, 48, 192, 1));
    CHECK(alpha_count(after) == 12 * 16 && after[(15 * 48 + 11) * 4 + 3] == 255);
    source_paint.logical_width = source_paint.logical_height = 16384;
    kh2.sized = false;
    CHECK(!draw_glyph(kh2, source_paint, after.data(), 48, 48, 192, 1));
    std::printf("KH2 spacing example: legacy artwork 6x8 / advance 18; source artwork 18x24 / advance 18\n");
    paint.clip_left = 2;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    CHECK(draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    CHECK(out[(1 * 4 + 1) * 4 + 3] == 0);
    CHECK(out[(2 * 4 + 2) * 4 + 3] == 255);
    // Independent physical scaling keeps the exact same logical rectangle.
    paint.clip_left = -16384;
    std::vector<unsigned char> scaled(8 * 8 * 4, 0);
    CHECK(draw_glyph(atlas, paint, scaled.data(), 8, 8, 32, 2));
    CHECK(scaled[(2 * 8 + 2) * 4 + 3] == 128);
    CHECK(scaled[(1 * 8 + 2) * 4 + 3] == 0);
    CHECK(scaled[(6 * 8 + 2) * 4 + 3] == 0);
    // Source-over alpha is stable through repeated cached glyph paints.
    CHECK(draw_glyph(atlas, paint, scaled.data(), 8, 8, 32, 2));
    CHECK(scaled[(2 * 8 + 2) * 4 + 3] == 192);
    // Downsampling the arbitrary rectangle filters alpha without atlas neighbors.
    paint.logical_width = paint.logical_height = 1;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    CHECK(draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    CHECK(out[(1 * 4 + 1) * 4 + 3] == 160);
    paint.glyph_index = 1;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    CHECK(draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    CHECK(std::all_of(out.begin(), out.end(), [](unsigned char v) { return v == 0; }));
    paint.glyph_index = -1;
    CHECK(!draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    // Logical metrics cannot distort artwork; a non-square cell keeps PNG aspect.
    paint.glyph_index = 0;
    paint.logical_width = 4;
    paint.logical_height = 2;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    CHECK(draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    CHECK(out == cached);
    atlas.glyphs[0].left = -130;
    atlas.glyphs[0].width = 512;
    atlas.glyphs[0].advance = 300;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    CHECK(draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    CHECK(out == cached);
    atlas.glyphs[0].width = 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    CHECK(draw_glyph(atlas, paint, out.data(), 4, 4, 16, 1));
    CHECK(out == cached);
    // EVT 24x32 at .35: x=.35 must survive rasterization at output scale 4.
    // Doubling PNG resolution changes sampling, never the 8.4x11.2 logical cell.
    Atlas evt;
    evt.sized = true;
    evt.scale = .35;
    evt.cell_width = evt.width = 24;
    evt.cell_height = evt.height = 32;
    evt.rgba.assign(24 * 32 * 4, 255);
    Glyph evt_glyph;
    evt_glyph.image_w = 24;
    evt_glyph.image_h = 32;
    evt_glyph.width = 24;
    evt.glyphs.push_back(evt_glyph);
    GlyphPaint fractional;
    fractional.glyph_index = 0;
    fractional.x = .35;
    fractional.y = .25;
    fractional.logical_width = 9;
    fractional.logical_height = 12;
    std::vector<unsigned char> fractional_pixels(64 * 64 * 4, 0);
    CHECK(draw_glyph(evt, fractional, fractional_pixels.data(), 64, 64, 256, 4));
    CHECK(fractional_pixels[(1 * 64 + 1) * 4 + 3] > 0);
    CHECK(fractional_pixels[(1 * 64 + 1) * 4 + 3] < 255);
    CHECK(fractional_pixels[(1 * 64 + 0) * 4 + 3] == 0);
    auto subpixel_result = fractional_pixels;
    fractional.x = 0;
    std::fill(fractional_pixels.begin(), fractional_pixels.end(), std::uint8_t{0});
    CHECK(draw_glyph(evt, fractional, fractional_pixels.data(), 64, 64, 256, 4));
    CHECK(fractional_pixels != subpixel_result); // No rounding to a DS pixel first.
    fractional.x = .35;
    evt.width *= 2;
    evt.height *= 2;
    evt.glyphs[0].image_w *= 2;
    evt.glyphs[0].image_h *= 2;
    evt.rgba.assign(evt.width * evt.height * 4, 255);
    Atlas doubled_evt = evt; // A distinct immutable atlas also gives this check a fresh cache key.
    std::fill(fractional_pixels.begin(), fractional_pixels.end(), std::uint8_t{0});
    CHECK(draw_glyph(doubled_evt, fractional, fractional_pixels.data(), 64, 64, 256, 4));
    CHECK(fractional_pixels == subpixel_result);
    // Fractional translation and partial tile clips compose without seams.
    std::fill(fractional_pixels.begin(), fractional_pixels.end(), std::uint8_t{0});
    fractional.clip_right = 8;
    CHECK(draw_glyph(evt, fractional, fractional_pixels.data(), 64, 64, 256, 4));
    fractional.clip_left = 8;
    fractional.clip_right = 16384;
    CHECK(draw_glyph(evt, fractional, fractional_pixels.data(), 64, 64, 256, 4));
    CHECK(fractional_pixels == subpixel_result);
    // Synthetic reference captured before the sampler optimization: phases, clips,
    // tints and translucent destinations must retain every output byte at 1x-8x.
    Atlas reference_case;
    reference_case.width = 48;
    reference_case.height = 64;
    reference_case.cell_width = 24;
    reference_case.cell_height = 32;
    reference_case.sized = true;
    reference_case.scale = .35;
    reference_case.rgba.resize(48 * 64 * 4);
    for (std::size_t i = 0; i < reference_case.rgba.size(); ++i)
        reference_case.rgba[i] = static_cast<unsigned char>((i * 71 + i / 13) % 256);
    Glyph reference_glyph;
    reference_glyph.image_w = 48;
    reference_glyph.image_h = 64;
    reference_glyph.width = 24;
    reference_case.glyphs.push_back(reference_glyph);
    Atlas sparse_case = reference_case;
    for (int alpha_y = 0; alpha_y < 64; ++alpha_y)
        for (int alpha_x = 0; alpha_x < 48; ++alpha_x)
            if (alpha_x < 14 || alpha_x > 31 || alpha_y < 18 || alpha_y > 43)
                sparse_case.rgba[(alpha_y * 48 + alpha_x) * 4 + 3] = 0;
    std::uint64_t sample_hash = 14695981039346656037ull;
    for (const Atlas *sample_case : {&reference_case, &sparse_case}) {
        for (int output_scale : {1, 2, 4, 8}) {
            for (int i = 0; i < 60; ++i) {
                GlyphPaint reference_paint;
                reference_paint.glyph_index = 0;
                reference_paint.logical_width = 9;
                reference_paint.logical_height = 12;
                reference_paint.x = (i % 17) * .35 - 2;
                reference_paint.y = (i % 11) * .3 - 1;
                reference_paint.red = static_cast<unsigned char>(i * 7);
                reference_paint.green = static_cast<unsigned char>(i * 13);
                reference_paint.blue = 197;
                reference_paint.alpha = static_cast<unsigned char>(i * 17);
                reference_paint.clip_left = i % 3;
                reference_paint.clip_top = i % 2;
                reference_paint.clip_right = 8 + i % 4;
                reference_paint.clip_bottom = 11;
                std::vector<unsigned char> pixels(128 * 128 * 4);
                for (std::size_t k = 0; k < pixels.size(); ++k)
                    pixels[k] = static_cast<unsigned char>((k * 23 + i * 41) % 256);
                CHECK(draw_glyph(*sample_case, reference_paint, pixels.data(), 128, 128, 512, output_scale));
                for (auto value : pixels)
                    sample_hash = (sample_hash ^ value) * 1099511628211ull;
            }
        }
        if (sample_case == &reference_case)
            CHECK(sample_hash == 910704758645301201ull);
    }
    CHECK(sample_hash == 11684764914301227737ull);
    // White-mask edges keep exact alpha and straight RGB for every source alpha,
    // including transparent destinations whose unused RGB is nonzero.
    Atlas white_masks;
    white_masks.width = 256;
    white_masks.height = 1;
    white_masks.rgba.assign(256 * 4, 255);
    for (int source_alpha = 0; source_alpha < 256; ++source_alpha) {
        white_masks.rgba[source_alpha * 4 + 3] = static_cast<unsigned char>(source_alpha);
        Glyph white_glyph;
        white_glyph.image_x = source_alpha;
        white_glyph.image_w = white_glyph.image_h = 1;
        white_masks.glyphs.push_back(white_glyph);
    }
    for (int opacity : {0, 1, 17, 127, 254, 255}) {
        for (int source_alpha = 0; source_alpha < 256; ++source_alpha) {
            GlyphPaint white_paint;
            white_paint.glyph_index = source_alpha;
            white_paint.logical_width = white_paint.logical_height = 1;
            white_paint.alpha = static_cast<unsigned char>(opacity);
            unsigned char pixel[4] = {37, 83, 19, 0};
            CHECK(draw_glyph(white_masks, white_paint, pixel, 1, 1, 4, 1));
            const int expected_alpha = (source_alpha * opacity + 127) / 255;
            CHECK(pixel[3] == expected_alpha);
            if (expected_alpha)
                CHECK(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255);
            else
                CHECK(pixel[0] == 37 && pixel[1] == 83 && pixel[2] == 19);
        }
    }
    Atlas fitted;
    fitted.width = fitted.cell_width = 40;
    fitted.height = fitted.cell_height = 8;
    fitted.scale = 1;
    fitted.sized = true;
    fitted.rgba.assign(40 * 8 * 4, 0);
    for (int y = 0; y < 8; ++y)
        for (int x = 5; x < 35; ++x)
            for (int ch = 0; ch < 4; ++ch)
                fitted.rgba[(y * 40 + x) * 4 + ch] = 255;
    Glyph fitted_glyph;
    fitted_glyph.w = fitted_glyph.width = 40;
    fitted_glyph.image_w = 40;
    fitted_glyph.image_h = 8;
    fitted.glyphs.push_back(fitted_glyph);
    double bounds[2];
    CHECK(glyph_ink(fitted, 0, 8, 8, 4, bounds));
    CHECK(bounds[0] == 4.5 && bounds[1] == 35.5 && fitted.glyphs[0].ink_ready);
    Atlas doubled = fitted;
    doubled.width = 80;
    doubled.height = 16;
    doubled.glyphs[0].image_w = 80;
    doubled.glyphs[0].image_h = 16;
    doubled.glyphs[0].ink_ready = false;
    doubled.rgba.assign(80 * 16 * 4, 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 80; ++x)
            for (int ch = 0; ch < 4; ++ch)
                doubled.rgba[(y * 80 + x) * 4 + ch] = fitted.rgba[((y / 2) * 40 + x / 2) * 4 + ch];
    double doubled_bounds[2];
    CHECK(glyph_ink(doubled, 0, 8, 8, 4, doubled_bounds));
    CHECK(bounds[0] == doubled_bounds[0] && bounds[1] == doubled_bounds[1]);
    GlyphPaint fitted_paint;
    fitted_paint.glyph_index = 0;
    fitted_paint.x = 1.125;
    fitted_paint.y = 2;
    fitted_paint.x_scale = .73125;
    fitted_paint.logical_width = fitted_paint.logical_height = 8;
    std::vector<unsigned char> fitted_pixels(256 * 64 * 4);
    CHECK(draw_glyph(fitted, fitted_paint, fitted_pixels.data(), 256, 64, 256 * 4, 4));
    int lit = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 256; ++x)
            if (fitted_pixels[(y * 256 + x) * 4 + 3]) {
                ++lit;
                CHECK(y >= 8 && y < 40);
                CHECK(x < (fitted_paint.x + bounds[1] * fitted_paint.x_scale) * 4 + 1);
            }
    CHECK(lit > 0);
    // A whole synthetic name fills the requested range, rather than having
    // its pen positions or artwork compressed twice. PNG density is independent.
    for (const Atlas *density : {&fitted, &doubled}) {
        for (int target : {53, 61}) {
            for (int output_scale : {1, 4, 8}) {
                constexpr int count = 4, advance = 35;
                const double run_right = (count - 1) * advance + bounds[1] + 1;
                const double factor = target / run_right;
                const int row_width = 64 * output_scale, rows = 16 * output_scale;
                std::vector<unsigned char> run(static_cast<std::size_t>(row_width) * rows * 4);
                for (int pass = 0; pass < 2; ++pass) {
                    for (int letter = 0; letter < count; ++letter) {
                        GlyphPaint run_paint;
                        run_paint.glyph_index = 0;
                        run_paint.x = (letter * advance + (pass == 0 ? 1 : 0)) * factor;
                        run_paint.y = pass == 0 ? 1 : 0;
                        run_paint.x_scale = factor;
                        run_paint.clip_right = target;
                        run_paint.logical_width = run_paint.logical_height = 8;
                        CHECK(
                            draw_glyph(*density, run_paint, run.data(), row_width, rows, row_width * 4, output_scale));
                    }
                }
                int last = -1;
                for (int y = 0; y < rows; ++y)
                    for (int x = 0; x < row_width; ++x)
                        if (run[(y * row_width + x) * 4 + 3])
                            last = std::max(last, x);
                CHECK(last >= (target - 1) * output_scale && last < target * output_scale);
            }
        }
    }
    fitted_paint.x_scale = 0;
    CHECK(!draw_glyph(fitted, fitted_paint, fitted_pixels.data(), 256, 64, 256 * 4, 4));
    std::printf("HD font raster: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
