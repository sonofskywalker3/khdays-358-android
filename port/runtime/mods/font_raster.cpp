#include "font_raster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace khdays::hd_fonts {

int find_glyph(const Atlas &atlas, std::uint32_t codepoint)
{
    for (const Mapping &m : atlas.charmap) {
        if (m.codepoint == codepoint && m.glyph < atlas.glyphs.size())
            return static_cast<int>(m.glyph);
    }
    return -1;
}

namespace {

// An untinted, premultiplied RGBA glyph at the PC's output resolution.
// The atlas is immutable during the session. Tinting happens when it is painted.
struct Key {
    const Atlas *atlas;
    int glyph, w, h;
    bool native;
    double draw_w = 0, draw_h = 0, phase_x = 0, phase_y = 0;
    bool operator==(const Key &other) const noexcept
    {
        return atlas == other.atlas && glyph == other.glyph && w == other.w && h == other.h && native == other.native &&
               draw_w == other.draw_w && draw_h == other.draw_h && phase_x == other.phase_x && phase_y == other.phase_y;
    }
};
struct KeyHash {
    std::size_t operator()(const Key &k) const noexcept
    {
        std::size_t h = reinterpret_cast<std::size_t>(k.atlas) >> 4;
        for (const std::uint32_t v : {static_cast<std::uint32_t>(k.glyph), static_cast<std::uint32_t>(k.w),
                                      static_cast<std::uint32_t>(k.h), static_cast<std::uint32_t>(k.native)}) {
            h ^= static_cast<std::size_t>(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
        }
        for (const double v : {k.draw_w, k.draw_h, k.phase_x, k.phase_y})
            h ^= std::hash<double>{}(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};
struct Cached {
    Key key;
    struct Tile {
        std::vector<std::uint8_t> pixels;
        int left, top, right, bottom; // Nonzero PNG alpha in the cached tile, never original NFTR ink.
        bool grayscale = true, white_mask = true;
    };
    std::shared_ptr<const Tile> tile;
};
constexpr std::size_t CACHE_LIMIT = 32u * 1024u * 1024u;
constexpr std::size_t CACHE_ENTRIES = 8192;
std::mutex cache_mutex;
std::list<Cached> cache_order;
std::unordered_map<Key, std::list<Cached>::iterator, KeyHash> cache_index;
std::size_t cached_bytes;

struct Sample {
    double r, g, b, a;
}; // premultiplied RGB in [0,1]

static Sample at(const Atlas &atlas, int x, int y)
{
    const auto *p = &atlas.pixels()[(static_cast<std::size_t>(y) * atlas.width + x) * 4];
    const double a = p[3] / 255.0;
    return {p[0] / 255.0 * a, p[1] / 255.0 * a, p[2] / 255.0 * a, a};
}

struct SampleBounds {
    int x_min, y_min, x_max, y_max;
};

static Sample sample(const Atlas &atlas, const Glyph &g, const SampleBounds &bounds, double u, double v,
                     bool native_pixels)
{
    const auto [x_min, y_min, x_max, y_max] = bounds;
    if (native_pixels) {
        // Native-resolution NFTR dumps retain exact texels with nearest-neighbor sampling.
        const int x = std::clamp(static_cast<int>(std::floor(g.image_x + u * g.image_w)), x_min, x_max);
        const int y = std::clamp(static_cast<int>(std::floor(g.image_y + v * g.image_h)), y_min, y_max);
        return at(atlas, x, y);
    }
    // HD atlases are linearly filtered *within* the glyph to keep clean edges.
    const double sx = g.image_x + u * g.image_w - .5;
    const double sy = g.image_y + v * g.image_h - .5;
    const double bx = std::floor(sx), by = std::floor(sy);
    const int x0 = std::clamp(static_cast<int>(bx), x_min, x_max);
    const int y0 = std::clamp(static_cast<int>(by), y_min, y_max);
    const int x1 = std::clamp(static_cast<int>(bx) + 1, x_min, x_max);
    const int y1 = std::clamp(static_cast<int>(by) + 1, y_min, y_max);
    const double fx = std::clamp(sx - bx, 0.0, 1.0), fy = std::clamp(sy - by, 0.0, 1.0);
    const auto a = at(atlas, x0, y0), b = at(atlas, x1, y0);
    const auto c = at(atlas, x0, y1), d = at(atlas, x1, y1);
    auto mix = [](double x, double y, double t) { return x + (y - x) * t; };
    return {mix(mix(a.r, b.r, fx), mix(c.r, d.r, fx), fy), mix(mix(a.g, b.g, fx), mix(c.g, d.g, fx), fy),
            mix(mix(a.b, b.b, fx), mix(c.b, d.b, fx), fy), mix(mix(a.a, b.a, fx), mix(c.a, d.a, fx), fy)};
}

static std::uint8_t byte(double value)
{
    value *= 255.0;
    const int whole = static_cast<int>(value);
    return static_cast<std::uint8_t>(std::clamp(whole + (value >= whole + .5), 0, 255));
}

static std::shared_ptr<const Cached::Tile> render_tile(const Atlas &atlas, const Glyph &g, int width, int height,
                                                       bool native_pixels)
{
    SampleBounds bounds;
    bounds.x_min = std::clamp(static_cast<int>(std::floor(g.image_x)), 0, atlas.width - 1);
    bounds.y_min = std::clamp(static_cast<int>(std::floor(g.image_y)), 0, atlas.height - 1);
    bounds.x_max = std::clamp(static_cast<int>(std::ceil(g.image_x + g.image_w)) - 1, bounds.x_min, atlas.width - 1);
    bounds.y_max = std::clamp(static_cast<int>(std::ceil(g.image_y + g.image_h)) - 1, bounds.y_min, atlas.height - 1);
    auto tile = std::make_shared<Cached::Tile>();
    tile->pixels.resize(static_cast<std::size_t>(width) * height * 4);
    tile->left = width;
    tile->top = height;
    tile->right = tile->bottom = 0;
    for (int y = 0; y < height; ++y) {
        const double v = (y + .5) / height;
        for (int x = 0; x < width; ++x) {
            const auto s = sample(atlas, g, bounds, (x + .5) / width, v, native_pixels);
            auto *p = tile->pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            p[0] = byte(s.r);
            p[1] = byte(s.g);
            p[2] = byte(s.b);
            p[3] = byte(s.a);
            tile->grayscale = tile->grayscale && p[0] == p[1] && p[0] == p[2];
            tile->white_mask = tile->white_mask && p[0] == p[3] && p[1] == p[3] && p[2] == p[3];
            if (p[3]) {
                tile->left = std::min(tile->left, x);
                tile->top = std::min(tile->top, y);
                tile->right = std::max(tile->right, x + 1);
                tile->bottom = std::max(tile->bottom, y + 1);
            }
        }
    }
    return tile;
}

static std::shared_ptr<const Cached::Tile> cached_tile(const Key &key)
{
    std::lock_guard<std::mutex> guard(cache_mutex);
    auto it = cache_index.find(key);
    if (it != cache_index.end()) {
        cache_order.splice(cache_order.begin(), cache_order, it->second);
        return it->second->tile;
    }
    return {};
}

static std::shared_ptr<const Cached::Tile> keep_tile(const Key &key, std::shared_ptr<const Cached::Tile> tile)
{
    const std::size_t size = tile->pixels.size();
    if (size > CACHE_LIMIT)
        return tile;
    std::lock_guard<std::mutex> guard(cache_mutex);
    auto previous = cache_index.find(key);
    if (previous != cache_index.end())
        return previous->second->tile;
    while ((cached_bytes + size > CACHE_LIMIT || cache_order.size() >= CACHE_ENTRIES) && !cache_order.empty()) {
        cached_bytes -= cache_order.back().tile->pixels.size();
        cache_index.erase(cache_order.back().key);
        cache_order.pop_back();
    }
    cache_order.push_front({key, tile});
    cache_index.emplace(key, cache_order.begin());
    cached_bytes += size;
    return tile;
}

static std::shared_ptr<const Cached::Tile> glyph_tile(const Atlas &atlas, int index, int width, int height,
                                                      int logical_w, int logical_h)
{
    const Glyph &g = atlas.glyphs[index];
    const bool native = g.image_w <= logical_w + 1e-8 && g.image_h <= logical_h + 1e-8;
    const Key key{atlas.artwork ? atlas.artwork.get() : &atlas, index, width, height, native};
    if (auto tile = cached_tile(key))
        return tile;
    return keep_tile(key, render_tile(atlas, g, width, height, native));
}

// Exact fractional phases share the same bounded LRU as normalized artwork.
// Paint position, clipping and tint do not change the cached samples.
static std::shared_ptr<const Cached::Tile> phased_tile(const Key &key, const Cached::Tile &source)
{
    if (static_cast<std::size_t>(key.w) * key.h * 4 > CACHE_LIMIT)
        return {};
    if (auto tile = cached_tile(key))
        return tile;
    auto tile = std::make_shared<Cached::Tile>();
    tile->pixels.resize(static_cast<std::size_t>(key.w) * key.h * 4);
    tile->left = tile->top = 0;
    tile->right = key.w;
    tile->bottom = key.h;
    tile->grayscale = source.grayscale;
    tile->white_mask = source.white_mask;
    const int width = key.w - 1, height = key.h - 1;
    const auto *source_pixels = source.pixels.data();
    auto *pixels = tile->pixels.data();
    const std::uint8_t transparent[4] = {};
    auto texel = [&](int tx, int ty) {
        return tx < 0 || ty < 0 || tx >= width || ty >= height
                   ? transparent
                   : source_pixels + (static_cast<std::size_t>(ty) * width + tx) * 4;
    };
    for (int py = 0; py < key.h; ++py) {
        const double sy = (py + .5 - key.phase_y) * height / key.draw_h;
        const double at_y = sy - .5;
        const int ty = static_cast<int>(std::floor(at_y));
        const double fy = at_y - ty;
        for (int px = 0; px < key.w; ++px) {
            const double sx = (px + .5 - key.phase_x) * width / key.draw_w - .5;
            const int tx = static_cast<int>(std::floor(sx));
            const double fx = sx - tx;
            const auto *a0 = texel(tx, ty), *a1 = texel(tx + 1, ty);
            const auto *b0 = texel(tx, ty + 1), *b1 = texel(tx + 1, ty + 1);
            auto interpolate = [&](int channel) {
                const double a = a0[channel] * (1 - fx) + a1[channel] * fx;
                const double b = b0[channel] * (1 - fx) + b1[channel] * fx;
                const double value = a * (1 - fy) + b * fy;
                const int whole = static_cast<int>(value);
                return static_cast<std::uint8_t>(whole + (value >= whole + .5));
            };
            auto *p = pixels + (static_cast<std::size_t>(py) * key.w + px) * 4;
            p[3] = interpolate(3);
            p[0] = source.white_mask ? p[3] : interpolate(0);
            p[1] = source.grayscale ? p[0] : interpolate(1);
            p[2] = source.grayscale ? p[0] : interpolate(2);
        }
    }
    return keep_tile(key, tile);
}

static int div255(int value)
{
    return (value + 127) / 255;
}

} // namespace

int layout_units(const Atlas &atlas, int value)
{
    return static_cast<int>(std::lround(value * (atlas.sized ? atlas.scale : 1)));
}

GlyphGeometry glyph_geometry(const Atlas &atlas, int index, int days_width, int days_height, int days_ink_width)
{
    const auto &g = atlas.glyphs[index];
    if (g.original || g.secondary)
        return {0, 0, 0};
    const double ratio = std::min((atlas.sized ? atlas.cell_width * atlas.scale : days_width) / g.image_w,
                                  (atlas.sized ? atlas.cell_height * atlas.scale : days_height) / g.image_h);
    const int w = std::max(1, static_cast<int>(std::lround(g.image_w * ratio)));
    const int h = std::max(1, static_cast<int>(std::lround(g.image_h * ratio)));
    const int clip = !atlas.sized && g.width == days_ink_width ? w : layout_units(atlas, g.width);
    return {w, h, std::min(w, clip)};
}

bool glyph_ink(const Atlas &atlas, int index, int days_width, int days_height, int days_ink_width, double *bounds)
{
    if (index < 0 || static_cast<std::size_t>(index) >= atlas.glyphs.size())
        return false;
    const auto &g = atlas.glyphs[index];
    if (g.image_w <= 0 || g.image_h <= 0 || atlas.width <= 0 || atlas.height <= 0 ||
        atlas.pixels().size() != static_cast<std::size_t>(atlas.width) * atlas.height * 4)
        return false;
    std::lock_guard<std::mutex> guard(cache_mutex);
    if (!g.ink_ready) {
        const int left = std::clamp(static_cast<int>(std::floor(g.image_x)), 0, atlas.width);
        const int right = std::clamp(static_cast<int>(std::ceil(g.image_x + g.image_w)), left, atlas.width);
        const int top = std::clamp(static_cast<int>(std::floor(g.image_y)), 0, atlas.height);
        const int bottom = std::clamp(static_cast<int>(std::ceil(g.image_y + g.image_h)), top, atlas.height);
        g.ink_left = right;
        g.ink_right = left;
        if (static_cast<std::int64_t>(right - left) * (bottom - top) > 1048576) {
            g.ink_left = left;
            g.ink_right = right;
        } else {
            for (int y = top; y < bottom; ++y)
                for (int x = left; x < right; ++x)
                    if (atlas.pixels()[(static_cast<std::size_t>(y) * atlas.width + x) * 4 + 3]) {
                        g.ink_left = std::min(g.ink_left, x);
                        g.ink_right = std::max(g.ink_right, x + 1);
                    }
        }
        g.ink_ready = true;
    }
    if (g.ink_left >= g.ink_right) {
        bounds[0] = bounds[1] = 0;
        return true;
    }
    double w = days_width, h = days_height;
    if (atlas.sized) {
        const double ratio =
            std::min(atlas.cell_width * atlas.scale / g.image_w, atlas.cell_height * atlas.scale / g.image_h);
        w = g.image_w * ratio;
    } else if (w * g.image_h > h * g.image_w)
        w = std::max(1.0, std::round(h * g.image_w / g.image_h));
    const double clip = atlas.native_metrics || (!atlas.sized && g.width == days_ink_width)
                            ? w
                            : g.width * (atlas.sized ? atlas.scale : 1);
    const double padding = g.w > 0 ? .5 * g.image_w / g.w : .5;
    bounds[0] = std::max(0.0, g.ink_left - g.image_x - padding) * w / g.image_w;
    bounds[1] = std::min({w, clip, (g.ink_right - g.image_x + padding) * w / g.image_w});
    return true;
}

bool draw_glyph(const Atlas &atlas, const GlyphPaint &paint, std::uint8_t *rgba, int width, int height, int stride,
                int scale)
{
    if (!rgba || width <= 0 || height <= 0 || scale <= 0 || stride < static_cast<std::int64_t>(width) * 4 ||
        paint.glyph_index < 0 || static_cast<std::size_t>(paint.glyph_index) >= atlas.glyphs.size() ||
        paint.logical_width <= 0 || paint.logical_height <= 0 || atlas.width <= 0 || atlas.height <= 0 ||
        atlas.pixels().size() != static_cast<std::size_t>(atlas.width) * atlas.height * 4)
        return false;
    const Glyph &g = atlas.glyphs[paint.glyph_index];
    if (g.original || g.secondary)
        return false;
    if (g.image_w <= 0 || g.image_h <= 0 || g.image_x < 0 || g.image_y < 0 ||
        g.image_x + g.image_w > atlas.width + 1e-6 || g.image_y + g.image_h > atlas.height + 1e-6)
        return false;
    const double x = paint.x * scale, y = paint.y * scale;
    double w = static_cast<double>(paint.logical_width) * scale;
    double h = static_cast<double>(paint.logical_height) * scale;
    double ink_clip = 16384;
    if (atlas.sized) {
        const double ratio =
            std::min(atlas.cell_width * atlas.scale / g.image_w, atlas.cell_height * atlas.scale / g.image_h);
        w = g.image_w * ratio * scale;
        h = g.image_h * ratio * scale;
        ink_clip = g.width * atlas.scale;
    } else if (w * g.image_h > h * g.image_w) {
        w = std::max(1.0, std::round(h * g.image_w / g.image_h));
    } else {
        h = std::max(1.0, std::round(w * g.image_h / g.image_w));
    }
    w *= paint.x_scale;
    ink_clip *= paint.x_scale;
    if (!std::isfinite(paint.x_scale) || paint.x_scale <= 0 || paint.x_scale > 1 || !std::isfinite(x) ||
        !std::isfinite(y) || w <= 0 || h <= 0 || w > 16384 || h > 16384)
        return false;
    const int tile_w = static_cast<int>(std::ceil(w)), tile_h = static_cast<int>(std::ceil(h));
    if (static_cast<std::int64_t>(tile_w) * tile_h * 4 > static_cast<std::int64_t>(CACHE_LIMIT))
        return false;
    const double right = std::min(
        {static_cast<double>(width), x + w, x + ink_clip * scale, static_cast<double>(paint.clip_right) * scale});
    const double bottom =
        std::min({static_cast<double>(height), y + h, static_cast<double>(paint.clip_bottom) * scale});
    const double left = std::max({0.0, x, static_cast<double>(paint.clip_left) * scale});
    const double top = std::max({0.0, y, static_cast<double>(paint.clip_top) * scale});
    if (left >= right || top >= bottom)
        return true;
    int x0 = static_cast<int>(std::floor(left)), y0 = static_cast<int>(std::floor(top));
    int x1 = static_cast<int>(std::ceil(right)), y1 = static_cast<int>(std::ceil(bottom));
    // Cache normalized artwork independently of the pen's fractional phase.
    const auto tile = glyph_tile(atlas, paint.glyph_index, tile_w, tile_h, paint.logical_width, paint.logical_height);
    if (tile->left >= tile->right || tile->top >= tile->bottom)
        return true;
    const bool aligned = x == std::floor(x) && y == std::floor(y) && w == tile_w && h == tile_h;
    const int origin_x = static_cast<int>(std::floor(x)), origin_y = static_cast<int>(std::floor(y));
    const Key phase_key{atlas.artwork ? atlas.artwork.get() : &atlas,
                        paint.glyph_index,
                        tile_w + 1,
                        tile_h + 1,
                        g.image_w <= paint.logical_width + 1e-8 && g.image_h <= paint.logical_height + 1e-8,
                        w,
                        h,
                        x - origin_x,
                        y - origin_y};
    const auto phased = aligned ? std::shared_ptr<const Cached::Tile>{} : phased_tile(phase_key, *tile);
    const bool sampled = aligned || phased;
    const auto *samples = phased ? phased->pixels.data() : tile->pixels.data();
    const int sample_width = phased ? tile_w + 1 : tile_w;
    const bool white_mask = tile->white_mask, grayscale = tile->grayscale;
    const int spread = aligned ? 0 : 1; // Bilinear neighbors can extend cached alpha by one texel.
    x0 = std::max(x0, static_cast<int>(std::floor(x + (tile->left - spread) * w / tile_w)));
    y0 = std::max(y0, static_cast<int>(std::floor(y + (tile->top - spread) * h / tile_h)));
    x1 = std::min(x1, static_cast<int>(std::ceil(x + (tile->right + spread) * w / tile_w)));
    y1 = std::min(y1, static_cast<int>(std::ceil(y + (tile->bottom + spread) * h / tile_h)));
    const std::uint8_t transparent[4] = {};
    auto texel = [&](int tx, int ty) -> const std::uint8_t * {
        if (tx < 0 || ty < 0 || tx >= tile_w || ty >= tile_h)
            return transparent;
        return tile->pixels.data() + (static_cast<std::size_t>(ty) * tile_w + tx) * 4;
    };
    const int tint_r = paint.red, tint_g = paint.green, tint_b = paint.blue, tint_a = paint.alpha;
    const bool white = white_mask && tint_r == 255 && tint_g == 255 && tint_b == 255;
    for (int py = y0; py < y1; ++py) {
        const double sy = (py + .5 - y) * tile_h / h - .5;
        const int ty = static_cast<int>(std::floor(sy));
        const double fy = sy - ty;
        for (int px = x0; px < x1; ++px) {
            int s[4];
            if (sampled) {
                const auto *source =
                    samples + (static_cast<std::size_t>(py - origin_y) * sample_width + px - origin_x) * 4;
                for (int channel = 0; channel < 4; ++channel)
                    s[channel] = source[channel];
            } else {
                const double sx = (px + .5 - x) * tile_w / w - .5;
                const int tx = static_cast<int>(std::floor(sx));
                const double fx = sx - tx;
                const auto *a0 = texel(tx, ty), *a1 = texel(tx + 1, ty);
                const auto *b0 = texel(tx, ty + 1), *b1 = texel(tx + 1, ty + 1);
                auto interpolate = [&](int channel) {
                    const double a = a0[channel] * (1 - fx) + a1[channel] * fx;
                    const double b = b0[channel] * (1 - fx) + b1[channel] * fx;
                    // Compare with the exact half-byte boundary, avoiding a CRT call per channel.
                    const double value = a * (1 - fy) + b * fy;
                    const int whole = static_cast<int>(value);
                    return whole + (value >= whole + .5);
                };
                s[3] = interpolate(3);
                s[0] = white_mask ? s[3] : interpolate(0);
                s[1] = grayscale ? s[0] : interpolate(1);
                s[2] = grayscale ? s[0] : interpolate(2);
            }
            const int sa = div255(s[3] * tint_a);
            if (sa == 0)
                continue;
            auto *p = rgba + static_cast<std::size_t>(py) * stride + static_cast<std::size_t>(px) * 4;
            if (sa == 255) {
                p[0] = static_cast<std::uint8_t>(div255(s[0] * tint_r));
                p[1] = static_cast<std::uint8_t>(div255(s[1] * tint_g));
                p[2] = static_cast<std::uint8_t>(div255(s[2] * tint_b));
                p[3] = 255;
                continue;
            }
            const int da = p[3];
            if (white && da == 0) {
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<std::uint8_t>(sa);
                continue;
            }
            const int oa = sa + div255(da * (255 - sa));
            if (oa == 0)
                continue;
            if (tint_r == 0 && tint_g == 0 && tint_b == 0 && (da == 0 || (p[0] == 0 && p[1] == 0 && p[2] == 0))) {
                p[0] = p[1] = p[2] = 0;
                p[3] = static_cast<std::uint8_t>(oa);
                continue;
            }
            const int src[3] = {s[0], s[1], s[2]};
            const int tint[3] = {tint_r, tint_g, tint_b};
            const bool gray =
                grayscale && tint_r == tint_g && tint_g == tint_b && (da == 0 || (p[0] == p[1] && p[1] == p[2]));
            for (int c = 0; c < (gray ? 1 : 3); ++c) {
                // Source is premultiplied; destination is straight-alpha.
                const int sp = (src[c] * tint[c] * tint_a + 32512) / 65025;
                const int dp = (p[c] * da * (255 - sa) + 32512) / 65025;
                p[c] = static_cast<std::uint8_t>(std::min(255, (sp + dp) * 255 / oa));
            }
            if (gray)
                p[1] = p[2] = p[0];
            p[3] = static_cast<std::uint8_t>(oa);
        }
    }
    return true;
}

} // namespace khdays::hd_fonts
