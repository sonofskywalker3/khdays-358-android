// The notice of an autosave (autosave_notice.h): the port's words in the game's 8-dot font
// (text/font_eu_08.nftr), white with a black edge right of and below each letter, over everything at
// the upper screen's top left; gone by itself, no input taken.
#include "autosave_notice.h"

extern "C" {
#include "elements.h"
#include "parts.h"
#include "../host/screens/ui_text.h"
#include "../runtime/menus/autosave.h"
extern int GetLanguage(void);
unsigned int khdays_events_vblank_count(void);
}

#include "khdays/assets/font.h"
#include "khdays/vfs/filesystem.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace {

constexpr unsigned kShown = 180;                                    // VBlanks: three seconds
constexpr int kLeft = 4, kTop = 4;                                  // DS dots
constexpr std::uint32_t kWhite = 0xffffffffu, kBlack = 0xff000000u; // RGBA, red in bits 0-7

std::optional<khdays::assets::Font> font;
bool font_tried;

struct Shown {
    int file = -1, ok = 0;
    unsigned since = 0;
    int image = -1, w = 0, h = 0, scale = 0, language = 0;
} shown;

const khdays::assets::Font *the_font()
{
    if (!font_tried) {
        font_tried = true;
        const auto path = khdays::vfs::resolve("text/font_eu_08.nftr");
        try {
            if (path) {
                font = khdays::assets::decode_nftr(*path);
            }
        } catch (const std::exception &e) {
            std::fprintf(stderr, "ui: the autosave notice's font could not be read (%s)\n", e.what());
        }
    }
    return font ? &*font : nullptr;
}

std::u16string utf16(const char *s)
{
    std::u16string out;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p != 0;) {
        unsigned c = *p++;
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        c &= more == 3 ? 0x07u : more == 2 ? 0x0fu : more == 1 ? 0x1fu : 0x7fu;
        for (; more > 0 && (*p & 0xc0) == 0x80; --more) {
            c = (c << 6) | (*p++ & 0x3fu);
        }
        out.push_back(static_cast<char16_t>(c));
    }
    return out;
}

void drop_image()
{
    if (shown.image >= 0) {
        khdays_ui_image_put(shown.image);
        shown.image = -1;
    }
}

int notice_image(int language, int scale)
{
    const auto *f = the_font();
    if (f == nullptr) {
        return -1;
    }
    if (shown.image >= 0 && shown.scale == scale && shown.language == language) {
        return shown.image;
    }
    drop_image();
    char words[160];
    std::snprintf(words, sizeof(words), khdays_text_in(shown.ok ? TXT_AUTOSAVE_DONE : TXT_AUTOSAVE_FAILED, language),
                  shown.file + 1);
    const auto text = khdays::assets::render_text(*f, utf16(words));
    shown.w = text.width + 1;
    shown.h = text.height + 1;
    shown.scale = scale;
    shown.language = language;
    shown.image = khdays_ui_image_get(shown.w * scale, shown.h * scale);
    std::uint32_t *texels = shown.image >= 0 ? khdays_ui_image_begin(shown.image) : nullptr;
    if (texels == nullptr) {
        return -1;
    }
    auto ink = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < text.width && y < text.height &&
               text.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(text.width) +
                          static_cast<std::size_t>(x)) *
                             4u +
                         3u] >= 128;
    };
    for (int y = 0; y < shown.h * scale; ++y) {
        for (int x = 0; x < shown.w * scale; ++x) {
            const int dx = x / scale, dy = y / scale;
            texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(shown.w * scale) +
                   static_cast<std::size_t>(x)] = ink(dx, dy)                                                 ? kWhite
                                                  : ink(dx - 1, dy - 1) || ink(dx - 1, dy) || ink(dx, dy - 1) ? kBlack
                                                                                                              : 0u;
        }
    }
    khdays_ui_image_end(shown.image);
    return shown.image;
}

} // namespace

extern "C" void khdays_ui_autosave_notice_emit(void)
{
    const unsigned now = khdays_events_vblank_count();
    int file, ok;
    while (khdays_autosave_take_notice(&file, &ok)) {
        drop_image();
        shown.file = file;
        shown.ok = ok;
        shown.since = now;
    }
    if (shown.file < 0 || now - shown.since >= kShown) {
        return;
    }
    const int image = notice_image(GetLanguage(), khdays_ui_scale());
    if (image < 0) {
        return;
    }
    KhdaysUiElement e;
    e.id = "hud.autosave";
    e.screen = 0;
    e.slot = KES_SPRITES | KES_DIRECT | 0u; // over everything
    e.x = static_cast<float>(kLeft);
    e.y = static_cast<float>(kTop);
    e.w = static_cast<float>(shown.w);
    e.h = static_cast<float>(shown.h);
    e.image = image;
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
}
