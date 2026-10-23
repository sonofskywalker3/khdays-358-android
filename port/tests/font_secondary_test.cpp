// Synthetic atlas pairs exercise the real loader, glyph resolver, metrics and rasterizer.
#ifdef NDEBUG
#undef NDEBUG // Assertions must exercise their calls in Release builds too.
#endif
#include "../runtime/mods/fonts.cpp"
#include "../runtime/mods/font_runtime.cpp"
#include <cassert>
#include <cstdlib>
#include <cstring>

static bool primary_present = true, secondary_present = true, bad_png;
static int resolution = 1, probes;
static bool source_secondary_present;
extern "C" int khdays_mods_find(const char *relative, char *path, int size)
{
    ++probes;
    const char *file = nullptr;
    if (primary_present && std::strcmp(relative, "fonts/text/secondary.nftr.png") == 0)
        file = "build/tests/font-secondary.nftr.png";
    if (secondary_present && std::strcmp(relative, "fonts/text/secondary.nftr.fallback.png") == 0)
        file = "build/tests/font-secondary.nftr.fallback.png";
    if (source_secondary_present && std::strcmp(relative, "fonts/text/requested.nftr.fallback.png") == 0)
        file = "build/tests/font-requested.nftr.fallback.png";
    if (source_secondary_present && std::strcmp(relative, "fonts/text/other.nftr.fallback.png") == 0)
        file = "build/tests/font-other.nftr.fallback.png";
    if (!file)
        return 0;
    return std::snprintf(path, static_cast<std::size_t>(size), "%s", file) < size;
}
extern "C" std::uint8_t *khdays_png_read(const char *path, int *w, int *h, const char **error)
{
    const bool secondary = std::strstr(path, ".fallback.") != nullptr;
    if (secondary && bad_png) {
        *error = "synthetic decode failure";
        return nullptr;
    }
    *w = 16 * resolution;
    *h = 8 * resolution;
    auto *pixels = static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(*w) * *h * 4));
    for (int y = 0; y < *h; ++y)
        for (int x = 0; x < *w; ++x) {
            auto *pixel = pixels + (y * *w + x) * 4;
            pixel[0] = secondary ? 0 : 255;
            pixel[1] = secondary ? 255 : 0;
            pixel[2] = 0;
            pixel[3] = !secondary && x >= 8 * resolution ? 0 : 255;
        }
    return pixels;
}
extern "C" void khdays_png_free(std::uint8_t *pixels)
{
    std::free(pixels);
}

static constexpr const char *resource = "text/secondary.nftr";
static constexpr const char *primary_json = "build/tests/font-secondary.nftr.json";
static constexpr const char *secondary_json = "build/tests/font-secondary.nftr.fallback.json";
static constexpr std::uint32_t codes[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', ' '};
static const std::string header = R"({"source":"text/secondary.nftr","format":"NFTR",
    "atlas":{"width":16,"height":8},"cell_width":8,"cell_height":8,
    "baseline":6,"line_feed":10,"max_width":8,)";
static const std::string primary = header + R"("scale":0.5,
    "glyphs":[{"index":0,"x":0,"y":0,"w":8,"h":8,"left":-2,"width":8,"advance":12},
    {"index":1,"fallback":"secondary"},{"index":2,"fallback":"original"},
    {"index":3,"x":8,"y":0,"w":8,"h":8,"left":0,"width":8,"advance":9}],
    "charmap":[{"codepoint":65,"glyph":0},{"codepoint":66,"glyph":1},
    {"codepoint":67,"glyph":2},{"codepoint":32,"glyph":3}],
    "kerning":[{"left":65,"right":66,"adjust":-8},{"left":68,"right":69,"adjust":-8}]})";
static const std::string secondary_body = R"("scale":0.5,"baseline_offset":-1,
    "glyphs":[{"index":0,"x":0,"y":0,"w":8,"h":8,"left":-2,"width":8,"advance":11},
    {"index":1,"x":0,"y":0,"w":8,"h":8,"left":2,"width":6,"advance":13},
    {"index":2,"fallback":"original"}],
    "charmap":[{"codepoint":66,"glyph":1},{"codepoint":67,"glyph":0},
    {"codepoint":68,"glyph":0},{"codepoint":69,"glyph":1},{"codepoint":70,"glyph":2},
    {"codepoint":32,"glyph":0}],
    "kerning":[{"left":68,"right":69,"adjust":-3}]})";
static void bind(const char *path = resource, const std::uint32_t *map = codes, int count = 8)
{
    assert(!khdays_hd_font_map_ready(path));
    khdays_hd_font_bind_charmap(path, map, count);
    assert(khdays_hd_font_map_ready(path));
}
static std::uint32_t painted(int index, const char *path = resource)
{
    std::uint32_t pixels[64] = {};
    const bool available = khdays_hd_font_glyph_available(path, index) != 0;
    assert(khdays_hd_font_paint(path, index, 0, 0, 8, 8, 0xffffffff, pixels, 8, 8, 1) == static_cast<int>(available));
    return pixels[0];
}
int main()
{
    using namespace khdays::hd_fonts;
    // Keep immutable atlases alive while resetting metadata caches between synthetic fixtures.
    std::vector<std::shared_ptr<const Atlas>> keep;
    for (const bool custom : {false, true}) {
        for (resolution = 1; resolution <= 2; ++resolution) {
            std::ofstream(primary_json) << primary;
            std::ofstream(secondary_json) << header + (custom ? "\"metrics\":\"custom\"," : "") + secondary_body;
            cache.clear();
            keep.push_back(load(resource));
            bind();
            for (int i = 0; i < 8; ++i)
                assert(khdays_hd_font_glyph_available(resource, i) == (i == 0 || i == 1 || i == 3 || i == 4 || i == 7));
            assert(painted(0) == 0xff0000ff); // Primary red artwork.
            assert(painted(1) == 0xff00ff00 && painted(3) == 0xff00ff00 && painted(4) == 0xff00ff00);
            assert(painted(2) == 0 && painted(5) == 0 && painted(6) == 0); // Forced DS, secondary DS, absent both.
            assert(painted(7) == 0); // Transparent primary is valid; never use the green secondary space.
            assert(khdays_hd_font_metric_f(resource, 0, KHDF_ADVANCE, 7) == 6);
            assert(khdays_hd_font_metric_f(resource, 1, KHDF_ADVANCE, 7) == (custom ? 6.5 : 7));
            assert(khdays_hd_font_metric_f(resource, 1, KHDF_LEFT, -3) == (custom ? 1 : -3));
            assert(khdays_hd_font_metric_f(resource, 3, KHDF_LEFT, -3) == (custom ? -1 : -3));
            assert(khdays_hd_font_metric_f(resource, 2, KHDF_ADVANCE, 7) == 7);
            assert(khdays_hd_font_metric_f(resource, -1, KHDF_LINE_FEED, 12) == 5);
            assert(khdays_hd_font_metric_f(resource, 1, KHDF_SIZED, 0) == (custom ? 1 : 0));
            double ink[2];
            assert(khdays_hd_font_ink(resource, 1, 8, 8, 4, ink));
            assert(ink[0] == 0 && ink[1] == (custom ? 3 : 8)); // Native metrics ignore the secondary's width.
            assert(khdays_hd_font_baseline_f(resource, 1, 6) == (custom ? 2 : 0));
            assert(khdays_hd_font_extent_f(resource, 1, -3, 4, 7) == (custom ? 4 : 1));
            assert(khdays_hd_font_kerning_f(resource, 'A', 'B') == 0);
            assert(khdays_hd_font_kerning_f(resource, 'D', 'E') == (custom ? -1.5 : 0));
            assert(khdays_hd_font_kerning_f(resource, 'B', 'C') == 0);
            int geometry[3];
            assert(khdays_hd_font_geometry(resource, 1, 7, 8, 4, geometry));
            assert(geometry[0] == (custom ? 4 : 7) && geometry[1] == (custom ? 4 : 8));
            std::uint32_t clipped[64] = {};
            assert(khdays_hd_font_paint_clipped(resource, 1, 0, 0, 8, 8, 0xffffffff, clipped, 8, 8, 1, 2, 1, 3, 3));
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    assert(clipped[y * 8 + x] == (x == 2 && y >= 1 && y < 3 ? 0xff00ff00u : 0u));
            const int before = probes;
            for (int n = 0; n < 50; ++n) {
                assert(khdays_hd_font_glyph_available(resource, 4));
                assert(painted(4) == 0xff00ff00);
            }
            assert(probes == before);
            // Scoped targets keep separate original CMAPs, while sharing both atlases.
            auto view = routed_view(load(resource));
            cache["@font-route/test"] = {view, true, false};
            const std::uint32_t reordered[] = {'E', 'A', 'B', 'C'};
            bind("@font-route/test", reordered, 4);
            assert(painted(0, "@font-route/test") == 0xff00ff00);
            assert(painted(1, "@font-route/test") == 0xff0000ff);
            assert(painted(2, "@font-route/test") == 0xff00ff00);
            assert(!khdays_hd_font_glyph_available("@font-route/test", 3));
            assert(view->secondary == keep.back()->secondary && view->artwork == keep.back()->artwork);
            keep.push_back(view);
        }
    }
    // Legacy primary atlases without a charmap still address slots directly.
    std::string indexed = primary;
    const auto map_begin = indexed.find("\"charmap\"");
    const auto map_end = indexed.find(']', map_begin);
    indexed.replace(map_begin, map_end - map_begin + 1, "\"charmap\":[]");
    std::ofstream(primary_json) << indexed;
    cache.clear();
    keep.push_back(load(resource));
    bind();
    assert(painted(0) == 0xff0000ff && painted(1) == 0xff00ff00 && painted(2) == 0);
    // Native slot 3 is transparent primary artwork, despite its code also being in the secondary.
    assert(painted(3) == 0 && khdays_hd_font_glyph_available(resource, 3));
    std::ofstream(primary_json) << primary;
    // Secondary-only packs work, including original/default metrics and custom sizing.
    primary_present = false;
    for (const bool custom : {false, true}) {
        cache.clear();
        std::ofstream(secondary_json) << header + (custom ? "\"metrics\":\"custom\"," : "") + secondary_body;
        keep.push_back(load(resource));
        bind();
        assert(khdays_hd_font_glyph_available(resource, 1));
        assert(!khdays_hd_font_glyph_available(resource, 0));
        assert(khdays_hd_font_metric_f(resource, -1, KHDF_LINE_FEED, 12) == (custom ? 5 : 12));
    }
    // Actual scoped selection also supports a secondary-only target and independent CMAP binding.
    rules_read = true;
    rules["camp"]["text/requested.nftr"] = resource;
    const std::string selected = select("text/requested.nftr", "camp.story.screen.1");
    assert(!selected.empty());
    bind(selected.c_str());
    assert(painted(1, selected.c_str()) == 0xff00ff00);
    assert(khdays_hd_font_metric_f(selected.c_str(), 0, KHDF_ADVANCE, 17) == 17);
    keep.push_back(load(selected));
    // A mapped primary without its own secondary inherits the requested resource's pair.
    const char *source_json = "build/tests/font-requested.nftr.fallback.json";
    const char *other_json = "build/tests/font-other.nftr.fallback.json";
    std::string source_font = header + "\"metrics\":\"custom\"," + secondary_body;
    source_font.replace(source_font.find(resource), std::strlen(resource), "text/requested.nftr");
    source_font.replace(source_font.find("\"advance\":11"), std::strlen("\"advance\":11"), "\"advance\":21");
    std::ofstream(source_json) << source_font;
    std::string other_font = header + secondary_body;
    other_font.replace(other_font.find(resource), std::strlen(resource), "text/other.nftr");
    std::ofstream(other_json) << other_font;
    primary_present = source_secondary_present = true;
    secondary_present = false;
    cache.clear();
    routed.clear();
    rules["camp"]["text/other.nftr"] = resource;
    const std::string inherited = select("text/requested.nftr", "camp");
    const std::string other = select("text/other.nftr", "camp");
    assert(!inherited.empty() && !other.empty() && inherited != other);
    bind(inherited.c_str());
    const std::uint32_t other_codes[] = {'D', 'A', 'B', 'C'};
    bind(other.c_str(), other_codes, 4);
    assert(painted(0, inherited.c_str()) == 0xff0000ff);
    assert(painted(1, inherited.c_str()) == 0xff00ff00);
    assert(painted(3, inherited.c_str()) == 0xff00ff00);
    assert(painted(4, inherited.c_str()) == 0xff00ff00);
    assert(!khdays_hd_font_glyph_available(inherited.c_str(), 2)); // Forced original still bypasses PNGs.
    assert(khdays_hd_font_metric_f(inherited.c_str(), 3, KHDF_ADVANCE, 7) == 10.5);
    assert(khdays_hd_font_metric_f(other.c_str(), 0, KHDF_ADVANCE, 7) == 7);
    assert(painted(0, other.c_str()) == 0xff00ff00 && painted(1, other.c_str()) == 0xff0000ff);
    assert(!load(resource)->secondary); // Inheritance must not mutate the shared target.
    keep.push_back(load(inherited));
    keep.push_back(load(other));
    // A target-specific secondary wins when both exist, and all warm route lookups stay cached.
    secondary_present = true;
    cache.clear();
    routed.clear();
    const std::string preferred = select("text/requested.nftr", "camp");
    bind(preferred.c_str());
    assert(khdays_hd_font_metric_f(preferred.c_str(), 3, KHDF_ADVANCE, 7) == 5.5);
    const int before_route = probes;
    assert(select("text/requested.nftr", "camp") == preferred);
    assert(probes == before_route);
    keep.push_back(load(preferred));
    source_secondary_present = false;
    std::remove(source_json);
    std::remove(other_json);
    // A missing or rejected secondary cannot disable a valid primary or create another fallback chain.
    primary_present = true;
    for (int failure = 0; failure < 8; ++failure) {
        cache.clear();
        secondary_present = failure != 0;
        bad_png = failure == 1;
        if (failure == 2)
            std::remove(secondary_json);
        else if (failure == 3)
            std::ofstream(secondary_json) << "{}";
        else if (failure == 4)
            std::ofstream(secondary_json) << header + R"("glyphs":[{"index":0,"fallback":"secondary"}],"charmap":[]})";
        else if (failure == 5)
            std::ofstream(secondary_json) << header + R"("glyphs":[{"index":0,"fallback":"original"}],"charmap":[]})";
        else if (failure == 6) {
            std::string wrong = header + secondary_body;
            wrong.replace(wrong.find(resource), std::strlen(resource), "text/wrong.nftr");
            std::ofstream(secondary_json) << wrong;
        } else if (failure == 7)
            std::ofstream(secondary_json) << header + "\"metrics\":\"invalid\"," + secondary_body;
        else
            std::ofstream(secondary_json) << header + secondary_body;
        keep.push_back(load(resource));
        bind();
        assert(painted(0) == 0xff0000ff);
        assert(!khdays_hd_font_glyph_available(resource, 1));
        assert(!khdays_hd_font_glyph_available(resource, 3));
    }
    primary_present = secondary_present = false;
    cache.clear();
    assert(!khdays_hd_font_has(resource));
    std::remove(primary_json);
    std::remove(secondary_json);
    std::puts("HD secondary font fallback tests passed");
}
