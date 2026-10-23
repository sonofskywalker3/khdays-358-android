// The settings menu in the HD collection's own menu's pieces (settings_skin.h): settings.txt, which
// tools/hd/theater_settings.py writes (its lines: docs/HD_SCREENS.md, "The settings menu").
#include "settings_skin.h"

#include "imgui_internal.h" // ImFontLoader: the font is the pieces' own pictures

extern "C" {
#include "../render/png_io.h"
#include "../screens/ui_text.h"
#include "../../runtime/core/rom.h"
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// The font's spacing, the port's (the font has no space glyph): in its cells' texels.
constexpr float kSpace = 9.0f, kTracking = 1.0f;

struct Part {
    std::string piece, state, role;
    float x, y, w, h, u, v;
};

struct Glyph {
    int x, y, w;
};

struct Skin {
    int language = -1; // the one loaded (KHDAYS_LANGUAGE_*), -1 none tried
    bool on = false;
    float pw = 1920, ph = 1080;
    std::string folder, atlas_file, font_file;
    Part screen{};
    std::vector<Part> parts;
    SDL_Texture *atlas = nullptr;
    int aw = 0, ah = 0;
    // the font: its glyphs' grey, kept for ImGui's loader
    std::unordered_map<unsigned, Glyph> glyphs;
    std::vector<unsigned char> font_grey;
    int fw = 0, fh = 0, cell = 32;
    ImFont *font = nullptr;
};

Skin s;

bool read_file(const char *path)
{
    FILE *f = std::fopen(path, "r");
    char line[512];
    if (f == nullptr) {
        return false;
    }
    s.parts.clear();
    s.glyphs.clear();
    s.screen = Part{};
    s.atlas_file.clear();
    s.font_file.clear();
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        char a[64] = "", b[64] = "", c[64] = "", file[256] = "";
        Part p{};
        int code, x, y, w, cell;
        if (std::sscanf(line, "picture %f %f", &s.pw, &s.ph) == 2) {
            continue;
        }
        if (std::sscanf(line, "atlas %255s", file) == 1) {
            s.atlas_file = file;
        } else if (std::sscanf(line, "font %255s %d", file, &cell) == 2) {
            s.font_file = file;
            s.cell = cell;
        } else if (std::sscanf(line, "screen %f %f %f %f %f %f", &p.x, &p.y, &p.w, &p.h, &p.u, &p.v) == 6) {
            s.screen = p;
        } else if (std::sscanf(line, "part %63s %63s %63s %f %f %f %f %f %f", a, b, c, &p.x, &p.y, &p.w, &p.h, &p.u,
                               &p.v) == 9) {
            p.piece = a;
            p.state = b;
            p.role = c;
            s.parts.push_back(p);
        } else if (std::sscanf(line, "glyph %d %d %d %d", &code, &x, &y, &w) == 4) {
            s.glyphs[(unsigned)code] = Glyph{x, y, w};
        }
    }
    std::fclose(f);
    return !s.parts.empty() && !s.atlas_file.empty() && s.screen.w > 0;
}

std::vector<const Part *> parts_of(const char *piece, const char *state)
{
    std::vector<const Part *> out;
    for (const Part &p : s.parts) {
        if (p.piece == piece && p.state == state) {
            out.push_back(&p);
        }
    }
    return out;
}

void image(ImDrawList *dl, const Part &p, ImVec2 a, ImVec2 b)
{
    const ImVec2 uv0(p.u / (float)s.aw, p.v / (float)s.ah), uv1((p.u + p.w) / (float)s.aw, (p.v + p.h) / (float)s.ah);
    dl->AddImage((ImTextureID)(intptr_t)s.atlas, a, b, uv0, uv1);
}

// ---- The font: ImGui's loader over the font's picture --------------------------------------

bool font_contains(ImFontAtlas *, ImFontConfig *, ImWchar code)
{
    return code == ' ' || s.glyphs.count((unsigned)code) != 0;
}

bool font_baked_init(ImFontAtlas *, ImFontConfig *src, ImFontBaked *baked, void *)
{
    if (!src->MergeMode) {
        baked->Ascent = std::ceil(baked->Size * 0.8f);
        baked->Descent = std::floor(-baked->Size * 0.2f);
    }
    return true;
}

// The cell's grey at `scale` times its size: each pixel the mean of the texels it covers.
void resample(const Glyph &g, float scale, int w, int h, unsigned char *out)
{
    for (int y = 0; y < h; ++y) {
        const float sy0 = (float)y / scale, sy1 = std::min((float)s.cell, (float)(y + 1) / scale);
        for (int x = 0; x < w; ++x) {
            const float sx0 = (float)x / scale, sx1 = std::min((float)g.w, (float)(x + 1) / scale);
            float sum = 0, area = 0;
            for (int ty = (int)sy0; ty < (int)std::ceil(sy1); ++ty) {
                const float cy = std::min(sy1, (float)ty + 1) - std::max(sy0, (float)ty);
                for (int tx = (int)sx0; tx < (int)std::ceil(sx1); ++tx) {
                    const float cx = std::min(sx1, (float)tx + 1) - std::max(sx0, (float)tx);
                    const int px = g.x + tx, py = g.y + ty;
                    if (cx > 0 && cy > 0 && px >= 0 && py >= 0 && px < s.fw && py < s.fh) {
                        sum += cx * cy * s.font_grey[(size_t)py * (size_t)s.fw + (size_t)px];
                        area += cx * cy;
                    }
                }
            }
            out[(size_t)y * (size_t)w + (size_t)x] = area > 0 ? (unsigned char)std::lround(sum / area) : 0;
        }
    }
}

bool font_load_glyph(ImFontAtlas *atlas, ImFontConfig *src, ImFontBaked *baked, void *, ImWchar code,
                     ImFontGlyph *out_glyph, float *out_advance_x)
{
    const float k = baked->Size / (float)s.cell;
    const auto it = s.glyphs.find((unsigned)code);
    if (code != ' ' && it == s.glyphs.end()) {
        return false;
    }
    const float advance = code == ' ' ? kSpace * k : ((float)it->second.w + kTracking) * k;
    if (out_advance_x != nullptr) {
        *out_advance_x = advance;
        return true;
    }
    out_glyph->Codepoint = code;
    out_glyph->AdvanceX = advance;
    if (code == ' ') {
        return true;
    }
    const float density = src->RasterizerDensity * baked->RasterizerDensity;
    const int w = std::max(1, (int)std::ceil((float)it->second.w * k * density));
    const int h = std::max(1, (int)std::ceil((float)s.cell * k * density));
    const ImFontAtlasRectId id = ImFontAtlasPackAddRect(atlas, w, h);
    if (id == ImFontAtlasRectId_Invalid) {
        return false;
    }
    ImTextureRect *r = ImFontAtlasPackGetRect(atlas, id);
    ImFontAtlasBuilder *builder = atlas->Builder;
    builder->TempBuffer.resize(w * h);
    resample(it->second, k * density, w, h, builder->TempBuffer.Data);
    out_glyph->X0 = 0;
    out_glyph->Y0 = 0;
    out_glyph->X1 = (float)w / density;
    out_glyph->Y1 = (float)h / density;
    out_glyph->Visible = true;
    out_glyph->PackId = id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out_glyph, r, builder->TempBuffer.Data,
                                       ImTextureFormat_Alpha8, w);
    return true;
}

const ImFontLoader *font_loader()
{
    static ImFontLoader loader;
    loader.Name = "the HD menu's font";
    loader.FontSrcContainsGlyph = font_contains;
    loader.FontBakedInit = font_baked_init;
    loader.FontBakedLoadGlyph = font_load_glyph;
    return &loader;
}

} // namespace

bool khdays_skin_use(SDL_Renderer *renderer, int language)
{
    if (language == s.language && (s.atlas != nullptr || !s.on)) {
        return s.on;
    }
    char relative[96], path[1024];
    s.language = language;
    s.on = false;
    if (s.atlas != nullptr) {
        SDL_DestroyTexture(s.atlas);
        s.atlas = nullptr;
    }
    SDL_snprintf(relative, sizeof(relative), "layouts/settings/%s/settings.txt", khdays_language_code(language));
    if (!khdays_mods_find(relative, path, (int)sizeof(path)) || !read_file(path)) {
        return false;
    }
    s.folder = path;
    s.folder.erase(s.folder.find_last_of("/\\") + 1);
    const char *error = nullptr;
    int w = 0, h = 0;
    uint8_t *px = khdays_png_read((s.folder + s.atlas_file).c_str(), &w, &h, &error);
    if (px == nullptr) {
        std::fprintf(stderr, "settings: %s%s: %s\n", s.folder.c_str(), s.atlas_file.c_str(),
                     error ? error : "not read");
        return false;
    }
    s.atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (s.atlas != nullptr) {
        SDL_UpdateTexture(s.atlas, nullptr, px, w * 4);
        SDL_SetTextureBlendMode(s.atlas, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(s.atlas, SDL_SCALEMODE_LINEAR);
    }
    khdays_png_free(px);
    s.aw = w;
    s.ah = h;
    if (s.font_grey.empty() && !s.font_file.empty()) {
        px = khdays_png_read((s.folder + s.font_file).c_str(), &w, &h, &error);
        if (px != nullptr) {
            s.font_grey.resize((size_t)w * (size_t)h);
            for (size_t i = 0; i < s.font_grey.size(); ++i) {
                s.font_grey[i] = px[i * 4]; // a grey picture: its red
            }
            s.fw = w;
            s.fh = h;
            khdays_png_free(px);
        }
    }
    s.on = s.atlas != nullptr;
    return s.on;
}

void khdays_skin_renderer_gone()
{
    if (s.atlas != nullptr) {
        SDL_DestroyTexture(s.atlas);
        s.atlas = nullptr;
    }
    s.language = -1; // loaded again with the next renderer
    s.on = false;
}

ImFont *khdays_skin_font(ImFontAtlas *atlas, const void *fallback_ttf, int fallback_size)
{
    if (s.font != nullptr || s.font_grey.empty() || s.glyphs.empty()) {
        return s.font;
    }
    ImFontConfig config;
    config.FontLoader = font_loader();
    config.SizePixels = 18.0f;
    SDL_strlcpy(config.Name, "HD menu font", sizeof(config.Name));
    s.font = atlas->AddFont(&config);
    if (s.font != nullptr && fallback_ttf != nullptr) {
        ImFontConfig merged;
        merged.MergeMode = true;
        merged.FontDataOwnedByAtlas = false;
        SDL_strlcpy(merged.Name, "Roboto Medium", sizeof(merged.Name));
        atlas->AddFontFromMemoryTTF(const_cast<void *>(fallback_ttf), fallback_size, 18.0f, &merged, nullptr);
    }
    return s.font;
}

ImVec2 khdays_skin_point(ImVec2 size, float x, float y)
{
    return ImVec2(x * size.x / s.pw, y * size.y / s.ph);
}

void khdays_skin_screen(ImDrawList *dl, ImVec2 size)
{
    if (s.on) {
        image(dl, s.screen, ImVec2(0, 0), size);
    }
}

bool khdays_skin_place(ImDrawList *dl, const char *piece, ImVec2 size)
{
    const std::vector<const Part *> parts = parts_of(piece, "shown");
    if (!s.on || parts.empty()) {
        return false;
    }
    for (const Part *p : parts) {
        image(dl, *p, khdays_skin_point(size, p->x, p->y), khdays_skin_point(size, p->x + p->w, p->y + p->h));
    }
    return true;
}

namespace {

// Lays a state's parts over `min`-`max` as the piece's shown parts lie over it: along x (a
// left, a middle and a right part) or along y (top, middle, bottom), or stretched (whole).
bool lay(ImDrawList *dl, const char *piece, const char *state, ImVec2 min, ImVec2 max, bool draw,
         std::vector<std::pair<const Part *, ImRect>> *placed, float title_h = 0)
{
    std::vector<const Part *> ref = parts_of(piece, "shown"), mine = parts_of(piece, state);
    if (ref.empty()) {
        ref = mine;
    }
    if (mine.empty() || !s.on) {
        return false;
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    bool along_x = false, along_y = false;
    for (const Part *p : ref) {
        x0 = std::min(x0, p->x);
        y0 = std::min(y0, p->y);
        x1 = std::max(x1, p->x + p->w);
        y1 = std::max(y1, p->y + p->h);
        along_x |= p->role == "left" || p->role == "right";
        along_y |= p->role == "top" || p->role == "bottom";
    }
    const float kx = (max.x - min.x) / (x1 - x0), ky = (max.y - min.y) / (y1 - y0);
    // the ends keep their shape: scaled with the rectangle's height (or width, upright)
    const float k = along_x ? ky : along_y ? kx : 0;
    float lo = along_x ? min.x : min.y, hi = along_x ? max.x : max.y; // the middle's span
    std::vector<std::pair<const Part *, ImRect>> out;
    for (const Part *p : mine) {
        ImRect r;
        if (p->role == "title" && title_h > 0) {
            // not with the window's height: a tall window's would grow down into its words
            const float kt = title_h / (p->y + p->h - y0);
            r.Min = ImVec2(min.x + (p->x - x0) * kt, min.y + (p->y - y0) * kt);
            r.Max = ImVec2(r.Min.x + p->w * kt, r.Min.y + p->h * kt);
        } else if (along_x) {
            r.Min.y = min.y + (p->y - y0) * k;
            r.Max.y = r.Min.y + p->h * k;
            if (p->role == "right") {
                r.Min.x = max.x - (x1 - p->x) * k;
                r.Max.x = r.Min.x + p->w * k;
                hi = std::min(hi, r.Min.x);
            } else {
                r.Min.x = min.x + (p->x - x0) * k;
                r.Max.x = r.Min.x + p->w * k;
                if (p->role == "left") {
                    lo = std::max(lo, r.Max.x);
                }
            }
        } else if (along_y) {
            r.Min.x = min.x + (p->x - x0) * k;
            r.Max.x = r.Min.x + p->w * k;
            if (p->role == "bottom") {
                r.Min.y = max.y - (y1 - p->y) * k;
                r.Max.y = r.Min.y + p->h * k;
                hi = std::min(hi, r.Min.y);
            } else {
                r.Min.y = min.y + (p->y - y0) * k;
                r.Max.y = r.Min.y + p->h * k;
                if (p->role == "top") {
                    lo = std::max(lo, r.Max.y);
                }
            }
        } else {
            r.Min = ImVec2(min.x + (p->x - x0) * kx, min.y + (p->y - y0) * ky);
            r.Max = ImVec2(r.Min.x + p->w * kx, r.Min.y + p->h * ky);
        }
        out.emplace_back(p, r);
    }
    for (auto &[p, r] : out) {
        if (p->role == "middle" && along_x) {
            r.Min.x = lo;
            r.Max.x = hi;
        } else if (p->role == "middle" && along_y) {
            r.Min.y = lo;
            r.Max.y = hi;
        }
        if (draw && r.Max.x > r.Min.x && r.Max.y > r.Min.y) {
            image(dl, *p, r.Min, r.Max);
        }
    }
    if (placed != nullptr) {
        *placed = out;
    }
    return true;
}

} // namespace

bool khdays_skin_piece(ImDrawList *dl, const char *piece, const char *state, ImVec2 min, ImVec2 max)
{
    return lay(dl, piece, state, min, max, true, nullptr);
}

bool khdays_skin_question(ImDrawList *dl, ImVec2 min, ImVec2 max, float title_h)
{
    return lay(dl, "question", "shown", min, max, true, nullptr, title_h);
}

bool khdays_skin_slider(ImDrawList *dl, const char *state, ImVec2 min, ImVec2 max, float t)
{
    std::vector<const Part *> mine = parts_of("slider", state);
    const Part *track = nullptr;
    for (const Part *p : mine) {
        if (p->role == "track") {
            track = p;
        }
    }
    if (track == nullptr || !s.on) {
        return false;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    const float k = (max.y - min.y) / track->h;
    const float x = min.x + (max.x - min.x) * t;
    for (const Part *p : mine) {
        const float top = min.y + (p->y - track->y) * k, bottom = top + p->h * k;
        if (p->role == "track") {
            image(dl, *p, min, max);
        } else if (p->role == "left") {
            image(dl, *p, ImVec2(min.x - (track->x - p->x) * k, top),
                  ImVec2(min.x - (track->x - p->x - p->w) * k, bottom));
        } else if (p->role == "right") {
            const float right = track->x + track->w;
            image(dl, *p, ImVec2(max.x + (p->x - right) * k, top), ImVec2(max.x + (p->x + p->w - right) * k, bottom));
        } else if (p->role == "fill") {
            image(dl, *p, ImVec2(min.x + (p->x - track->x) * k, top), ImVec2(x, bottom));
        } else if (p->role == "knob") {
            image(dl, *p, ImVec2(x - p->w * k / 2, top), ImVec2(x + p->w * k / 2, bottom));
        }
    }
    return true;
}

bool khdays_skin_bounds(const char *piece, float *x0, float *y0, float *x1, float *y1)
{
    float a = 1e9f, b = 1e9f, c = -1e9f, d = -1e9f;
    for (const Part *p : parts_of(piece, "shown")) {
        a = std::min(a, p->x);
        b = std::min(b, p->y);
        c = std::max(c, p->x + p->w);
        d = std::max(d, p->y + p->h);
    }
    if (c < a) {
        return false;
    }
    *x0 = a;
    *y0 = b;
    *x1 = c;
    *y1 = d;
    return true;
}

bool khdays_skin_size(const char *piece, float *w, float *h)
{
    float x0, y0, x1, y1;
    if (!khdays_skin_bounds(piece, &x0, &y0, &x1, &y1)) {
        return false;
    }
    *w = x1 - x0;
    *h = y1 - y0;
    return true;
}
