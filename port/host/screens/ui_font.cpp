// Text for the port's own screens, in the game's own font (ui_font.h).
#include "ui_font.h"

#include "khdays/assets/font.h"
#include "khdays/vfs/filesystem.h"

#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <unordered_map>

namespace {

constexpr const char *kFontPath = "text/font_eu_10.nftr";
constexpr std::size_t kMaxCached = 512; // labels; a settings menu has far fewer

struct Cached {
    SDL_Texture *texture = nullptr;
    int width = 0;
    int height = 0;
};

std::optional<khdays::assets::Font> g_font;
bool g_tried = false;
std::unordered_map<std::string, Cached> g_cache;

// UTF-8 to UTF-16, enough for the settings menu's English and the key names
// SDL gives (every character the font can draw is in the basic plane).
std::u16string to_utf16(const char *s)
{
    std::u16string out;
    const auto *p = reinterpret_cast<const unsigned char *>(s);
    while (*p != 0) {
        char32_t c = *p++;
        int more = 0;
        if (c >= 0xf0) {
            c &= 0x07;
            more = 3;
        } else if (c >= 0xe0) {
            c &= 0x0f;
            more = 2;
        } else if (c >= 0xc0) {
            c &= 0x1f;
            more = 1;
        }
        for (; more > 0 && (*p & 0xc0) == 0x80; --more) {
            c = (c << 6) | (*p++ & 0x3f);
        }
        out.push_back(c <= 0xffff ? static_cast<char16_t>(c) : u'?');
    }
    return out;
}

} // namespace

extern "C" int khdays_ui_font_load(void)
{
    if (g_tried) {
        return g_font.has_value();
    }
    g_tried = true;
    if (khdays::vfs::data_root().empty()) {
        khdays::vfs::autodetect_data_root(); // the game has done it already; a test may not have
    }
    const auto path = khdays::vfs::resolve(kFontPath);
    if (!path) {
        std::fprintf(stderr, "host: the game's font %s was not found; the settings menu uses plain text\n", kFontPath);
        return 0;
    }
    try {
        g_font = khdays::assets::decode_nftr(*path);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "host: the game's font %s could not be read (%s); the settings menu uses plain text\n",
                     kFontPath, e.what());
        return 0;
    }
    return 1;
}

extern "C" int khdays_ui_font_line_height(void)
{
    return g_font ? (g_font->line_feed > 0 ? g_font->line_feed : g_font->cell_height) : 8;
}

extern "C" int khdays_ui_font_width(const char *utf8)
{
    if (utf8 == nullptr) {
        return 0;
    }
    if (!g_font) {
        return static_cast<int>(std::char_traits<char>::length(utf8)) * 8;
    }
    // The same measure as render_text: each glyph's advance, half a cell for
    // a character the font does not have.
    int width = 0;
    for (const char16_t c : to_utf16(utf8)) {
        const int g = g_font->glyph_of(c);
        width += g >= 0 ? g_font->glyphs[static_cast<std::size_t>(g)].advance : g_font->cell_width / 2;
    }
    return width;
}

extern "C" SDL_Texture *khdays_ui_font_text(SDL_Renderer *renderer, const char *utf8, int *width, int *height)
{
    if (!g_font || utf8 == nullptr) {
        return nullptr;
    }
    if (const auto it = g_cache.find(utf8); it != g_cache.end()) {
        *width = it->second.width;
        *height = it->second.height;
        return it->second.texture;
    }
    if (g_cache.size() >= kMaxCached) {
        khdays_ui_font_forget();
    }
    const auto image = khdays::assets::render_text(*g_font, to_utf16(utf8));
    if (image.width <= 0 || image.height <= 0) {
        return nullptr;
    }
    SDL_Texture *texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    if (texture == nullptr) {
        return nullptr;
    }
    SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.width * 4);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST); // crisp pixels at any size, as on the DS
    g_cache[utf8] = Cached{texture, image.width, image.height};
    *width = image.width;
    *height = image.height;
    return texture;
}

extern "C" Uint32 *khdays_ui_font_pixels(const char *utf8, int *width, int *height)
{
    if (!g_font || utf8 == nullptr) {
        return nullptr;
    }
    const auto image = khdays::assets::render_text(*g_font, to_utf16(utf8));
    if (image.width <= 0 || image.height <= 0) {
        return nullptr;
    }
    const size_t count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    auto *pixels = static_cast<Uint32 *>(SDL_malloc(count * sizeof(Uint32)));
    if (pixels == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *p = &image.rgba[i * 4];
        pixels[i] = (static_cast<Uint32>(p[3]) << 24) | (static_cast<Uint32>(p[0]) << 16) | (static_cast<Uint32>(p[1]) << 8) | p[2];
    }
    *width = image.width;
    *height = image.height;
    return pixels;
}

extern "C" void khdays_ui_font_forget(void)
{
    for (auto &[text, cached] : g_cache) {
        SDL_DestroyTexture(cached.texture);
    }
    g_cache.clear();
}
