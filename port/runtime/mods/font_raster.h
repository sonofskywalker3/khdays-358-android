// PC-side glyph rasterizer. Independent of where DS glyph draw events originate.
#ifndef KHDAYS_HD_FONT_RASTER_H
#define KHDAYS_HD_FONT_RASTER_H

#include "fonts.h"
#include <cstdint>

namespace khdays::hd_fonts {

struct GlyphGeometry {
    int width, height, clip_width;
};
GlyphGeometry glyph_geometry(const Atlas &atlas, int index, int days_width, int days_height, int days_ink_width);
int layout_units(const Atlas &atlas, int value);

struct GlyphPaint {
    double x_scale = 1;                        // Optional run fitting; independent of the uniform font size.
    int glyph_index = -1;                      // resolved by the original game, or use find_glyph() for Unicode
    double x = 0, y = 0;                       // destination glyph cell top-left, in original DS pixel coordinates
    int logical_width = 0, logical_height = 0; // DS pixel dimensions of the drawn cell
    std::uint8_t red = 255, green = 255, blue = 255, alpha = 255;
    // Optional glyph-local scissor, in original DS pixel coordinates.
    int clip_left = -16384, clip_top = -16384;
    int clip_right = 16384, clip_bottom = 16384;
};

// Return -1 when absent. Always use mapping data instead of relying on glyph order.
int find_glyph(const Atlas &atlas, std::uint32_t codepoint);
bool glyph_ink(const Atlas &atlas, int index, int days_width, int days_height, int days_ink_width, double *bounds);

// Render to an RGBA8 PC canvas. `scale` converts DS coordinates to destination pixels.
// The caller owns the buffer; dimensions and stride are in PHYSICAL pixels/bytes.
// Returns false only if no valid glyph / geometry was supplied. If the glyph is
// present but entirely clipped, it still returns true.
// `logical_width/height` are legacy fallback dimensions. Explicit font sizing
// uses the source cell and font scale instead. PNG resolution affects sampling only.
bool draw_glyph(const Atlas &atlas, const GlyphPaint &paint, std::uint8_t *rgba, int width, int height, int stride,
                int scale);

} // namespace khdays::hd_fonts
#endif
