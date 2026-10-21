/* The window's picture (gpu_present.h): the GPU device with every shader format the build carries,
 * the window's renderer on it; both screens' 2D composed by gfx_render.c, engine A's 3D drawn by
 * gpu_raster3d.c, one screen decided by onescreen.c. A graphics card is required. Testing knobs
 * (KHDAYS_SHOTS_HD, KHDAYS_GFX=reference|check, KHDAYS_PROFILE): docs/RENDERING.md. */
#include "gpu_present.h"
#include "../app/devices.h"
#include "gfx_render.h"
#include "gpu_raster3d.h"
#include "../movies/movie_hd.h"
#include "../movies/movie_subtitles.h"
#include "../../runtime/menus/newgame.h"
#include "onescreen.h"
#include "png_io.h"
#include "../../ui/composition.h"
#include "../screens/ui_text.h"
#include "../../runtime/core/clock.h"
#include "../../runtime/core/events.h"
#include "../../runtime/display/frame_package.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/core/session_log.h"
#include "../../runtime/mods/textures.h"
#include "../../gfx/g3d_interp.h"
#if defined(__ANDROID__)
#include "../android/bottom_screen.h"
#endif

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W KHDAYS_FRAME_W
#define H KHDAYS_FRAME_H

#define SHADER_FORMATS (SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL)

static SDL_Window *window;
static SDL_GPUDevice *device; /* NULL before the window is made, and while it is remade */
static SDL_Renderer *renderer;
static SDL_Texture *screens;      /* the two screens, upper above lower */
static uint32_t shown[2 * W * H]; /* what it holds, to upload it again after a reset */
static KhdaysVideo video;
static KhdaysNotice notice;
static int said_backend;

/* The older HD path: engine A's screen at N x, and which screen it goes on. */
static int raster_ready;      /* gpu_raster3d.c opened on this device */
static SDL_Texture *hd;       /* this frame's, or NULL to show the 1x picture as it is */
static SDL_Texture *hd_movie; /* an HD video's picture, NV12 */
static int hd_movie_w, hd_movie_h;
static double hd_movie_time, hd_movie_fps; /* the picture's time and its file's rate, for its subtitles */
static int hd_upper;                       /* engine A is the upper screen */
static int hd_columns;                     /* its width in DS pixels (256 is the DS's) */
static int columns;                        /* the picture's width now, in DS pixels (the logical presentation's) */
static int refused_scale;                  /* a scale the device could not make (reported once) */

/* The graphics card stopped answering (a driver reset: D3D12's device removed, hung or reset, Vulkan's
 * lost). SDL's renderer does not notice on D3D12, so the port notices from its own calls' errors and
 * draws nothing until the window makes the renderer again. */
static int device_lost;
static int lost_scale, lost_times; /* the internal resolution it was lost at, and how often */
static int backend_change;         /* the Video section chose another graphics API: remake the renderer */

static int lost_error(void)
{
    const char *e = SDL_GetError();
    return e != NULL && (SDL_strstr(e, "0x887A0005") != NULL || SDL_strstr(e, "0x887A0006") != NULL ||
                         SDL_strstr(e, "0x887A0007") != NULL || SDL_strstr(e, "0x887A0020") != NULL ||
                         SDL_strstr(e, "0x887a0005") != NULL || SDL_strstr(e, "0x887a0006") != NULL ||
                         SDL_strstr(e, "0x887a0007") != NULL || SDL_strstr(e, "0x887a0020") != NULL ||
                         SDL_strstr(e, "VK_ERROR_DEVICE_LOST") != NULL);
}

static int scale_now(void);

static void check_lost(void)
{
    char last[2048];
    if (device_lost || !lost_error()) {
        return;
    }
    device_lost = 1;
    khdays_raster3d_recent(last, sizeof(last));
    fprintf(stderr, "host: the graphics card stopped answering (%s); the last 3D it was sent:\n%s", SDL_GetError(),
            last);
    fflush(stderr);
    khdays_log("graphics: the card stopped answering at internal resolution %d (%s); the last 3D:\n%s", scale_now(),
               SDL_GetError(), last);
}

int khdays_present_lost(void)
{
    return device_lost || backend_change;
}

static void layout(void); /* where the screens go, for the picture's width */

/* KHDAYS_GFX: the port's own 2D */
static int port2d_mode = -1;         /* 0 off, 1 port, 2 check */
static int port2d_ready;             /* gfx_render.c opened on this device */
static int port2d_drawn;             /* this frame's screens were drawn by it */
static int one_drawn;                /* and the one-screen picture made from them */
static int one_made;                 /* a one-screen picture has been made (the last one stays shown until the next) */
static int one_kind;                 /* how the frame's screens are shown on one screen (KHDAYS_SCREENS_*) */
static int presented_w, presented_h; /* the logical presentation set now */
static int port2d_upper;             /* the engine on the upper screen */
#if defined(__ANDROID__)
static KhdaysVideo given_video; /* the settings as given, before the second display's say */
static int two_displays;        /* they were applied with the lower screen on a display of its own */
/* With the lower screen on a display of its own, the title (its boot logos, its menu, the new game's
 * dialogs) is made as on one screen: a mod's HD title then shows on the upper display, the lower one
 * black (asked for on 2026-10-21: the menu's options on one screen only). Without such a mod the two
 * screens show as the DS's. */
static int title_one;

static int title_screens(const KhdaysFramePackage *p)
{
    const KhdaysSituationInputs *in = &p->situation;
    return in->taken && in->title_valid &&
           (in->title_shown || in->title_logo || in->title_newgame || in->title_passing);
}

static int bottom_held; /* the second display shows the settings menu (khdays_present_hold_bottom) */

static void send_black_bottom(void)
{
    static uint32_t black[16 * 12];
    if (bottom_held) {
        return;
    }
    if (black[0] == 0) {
        for (int i = 0; i < 16 * 12; ++i) {
            black[i] = 0xff000000u; /* ABGR8888: opaque black */
        }
    }
    khdays_bottom_present((const uint8_t *)black, 16, 12, 16 * 4);
}
#endif
static struct {
    int frames;
    unsigned long long pixels;
} port2d_check;

static int port2d_wanted(void)
{
    if (port2d_mode < 0) {
        const char *v = SDL_getenv("KHDAYS_GFX");
        port2d_mode = v != NULL && SDL_strcmp(v, "reference") == 0 ? 0
                      : v != NULL && SDL_strcmp(v, "check") == 0   ? 2
                                                                   : 1;
    }
    return port2d_mode;
}

/* KHDAYS_PROFILE */
static int profile;
static uint32_t last_frame;
static struct {
    int frames, hd_frames, not_drawn;
    double hd_ms, hd_max_ms;
} stats;

/* KHDAYS_SHOTS_HD */
static const char *shot_dir;
static uint32_t shot_frames[64];
static int shot_count;

/* Wine or Proton (a Steam Deck, Linux): its ntdll names its version. */
static int under_wine(void)
{
    SDL_SharedObject *ntdll = SDL_LoadObject("ntdll.dll");
    int wine = 0;
    if (ntdll != NULL) {
        wine = SDL_LoadFunction(ntdll, "wine_get_version") != NULL;
        SDL_UnloadObject(ntdll);
    }
    SDL_ClearError();
    return wine;
}

/* Makes a renderer on a new GPU device. Returns NULL with the reason in `why`. */
static SDL_Renderer *gpu_renderer(char *why, size_t size)
{
    static const char *const names[KHDAYS_BACKEND_COUNT] = {NULL, "direct3d12", "vulkan"};
    const int debug = SDL_getenv("KHDAYS_GPU_DEBUG") != NULL;
    const char *name = video.backend > 0 && video.backend < KHDAYS_BACKEND_COUNT ? names[video.backend] : NULL;
    SDL_GPUDevice *made = NULL;
    SDL_Renderer *r;
    if (name == NULL && SDL_GetHint(SDL_HINT_GPU_DRIVER) == NULL && under_wine()) {
        /* Automatic under Wine: its Direct3D 12 is made of Vulkan (vkd3d), so Vulkan straight away */
        made = SDL_CreateGPUDevice(SHADER_FORMATS, debug, "vulkan");
    }
    device = made != NULL ? made : SDL_CreateGPUDevice(SHADER_FORMATS, debug, name);
    if (device == NULL && name != NULL) {
        /* the graphics API asked for is not available: use the system's (the settings menu says so) */
        char text[480];
        SDL_snprintf(text, sizeof(text), khdays_text(TXT_BACKEND_REFUSED), name, SDL_GetError());
        fprintf(stderr, "host: %s\n", text);
        if (notice != NULL) {
            notice(text);
        }
        device = SDL_CreateGPUDevice(SHADER_FORMATS, debug, NULL);
    }
    if (device == NULL) {
        SDL_snprintf(why, size, "no GPU device: %s", SDL_GetError());
        return NULL;
    }
    r = SDL_CreateGPURenderer(device, window);
    if (r == NULL) {
        SDL_snprintf(why, size, "no renderer on the %s device: %s", SDL_GetGPUDeviceDriver(device), SDL_GetError());
        SDL_DestroyGPUDevice(device);
        device = NULL;
        return NULL;
    }
    /* One frame in flight: each picture is drawn for the refresh that shows it, so an interpolated 3D is
     * seen where it was placed. */
    if (!SDL_SetGPUAllowedFramesInFlight(device, 1)) {
        fprintf(stderr, "host: the graphics card keeps more than one frame in flight: %s\n", SDL_GetError());
    }
    {
        /* reported at the start, and again when it changes (the Video section, a lost device) */
        static char said[32];
        const char *now = SDL_GetGPUDeviceDriver(device);
        if (!said_backend || SDL_strcmp(said, now) != 0) {
            fprintf(stderr, "host: the picture goes through the graphics card (%s)\n", now);
            if (said_backend) {
                khdays_log("graphics: the picture goes through %s", now);
            }
            SDL_strlcpy(said, now, sizeof(said));
            said_backend = 1;
        }
    }
    raster_ready = khdays_raster3d_open(device);
    if (!raster_ready) {
        fprintf(stderr, "host: no higher internal resolution on this device: %s\n", SDL_GetError());
    }
    if (port2d_wanted()) {
        port2d_ready = khdays_gfx_render_open(device);
        fprintf(stderr,
                port2d_ready ? "host: the 2D is the port's own renderer's\n"
                             : "host: the port's own 2D renderer cannot run here: %s\n",
                SDL_GetError());
    }
    return r;
}

static int make_texture(void)
{
    screens = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, W, 2 * H);
    if (screens == NULL) {
        return 0;
    }
    SDL_SetTextureScaleMode(screens, video.smooth_scaling ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_UpdateTexture(screens, NULL, shown, W * 4);
    return 1;
}

/* Makes the renderer and the screens' texture: on the graphics card if it
 * can be used, else with SDL's own renderer (reported once). */
static int make_renderer(void)
{
    char why[320];
    renderer = gpu_renderer(why, sizeof(why));
    if (renderer == NULL) {
        /* no Direct3D 12 or Vulkan: there is no other way to draw */
        char text[640];
        SDL_snprintf(text, sizeof(text), khdays_text(TXT_NEEDS_GRAPHICS_CARD), why);
        fprintf(stderr, "host: %s\n", text);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kingdom Hearts 358/2 Days (native)", text, window);
        exit(1);
    }
    SDL_SetRenderVSync(renderer, 1);
    layout();
    return make_texture();
}

static void destroy(void)
{
    hd = NULL;
    if (hd_movie != NULL) {
        SDL_DestroyTexture(hd_movie);
        hd_movie = NULL;
    }
    khdays_movie_subtitles_release();
    port2d_drawn = 0;
    if (port2d_ready) {
        khdays_gfx_render_close(); /* close it before the renderer and the device */
        port2d_ready = 0;
        one_made = 0;
    }
    if (raster_ready) {
        khdays_raster3d_close(); /* close it before the renderer and the device */
        raster_ready = 0;
    }
    if (screens != NULL) {
        SDL_DestroyTexture(screens);
        screens = NULL;
    }
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (device != NULL) {
        SDL_DestroyGPUDevice(device);
        device = NULL;
    }
}

SDL_Renderer *khdays_present_open(SDL_Window *w, KhdaysNotice tell)
{
    window = w;
    notice = tell;
    profile = SDL_getenv("KHDAYS_PROFILE") != NULL;
    shot_dir = SDL_getenv("KHDAYS_SHOTS_HD");
    shot_count = 0;
    if (shot_dir != NULL && SDL_getenv("KHDAYS_SHOTS") != NULL) {
        const char *text = SDL_getenv("KHDAYS_SHOTS");
        while (*text != '\0' && shot_count < (int)SDL_arraysize(shot_frames)) {
            char *end;
            const unsigned long frame = SDL_strtoul(text, &end, 10);
            if (end == text) {
                ++text;
                continue;
            }
            shot_frames[shot_count++] = (uint32_t)frame;
            text = end;
        }
        SDL_CreateDirectory(shot_dir);
    }
    if (!make_renderer()) {
        destroy();
        return NULL;
    }
    return renderer;
}

void khdays_present_close(void)
{
    destroy();
    window = NULL;
}

int khdays_present_gpu(void)
{
    return device != NULL;
}

/* The width of engine A's screen for the aspect setting, in DS pixels: 256 at 4:3; 342 at 16:9 and 308
 * at 16:10 (rounded up to even, so the DS's 256 columns sit whole in the middle); for "window", as wide
 * as the window at the screens' height, up to three times the DS's. */
static int columns_now(void)
{
    int pw = 0, ph = 0, n;
    if (device == NULL || !raster_ready) {
        return W;
    }
    switch (video.aspect) {
    case KHDAYS_ASPECT_16_9: return 342;
    case KHDAYS_ASPECT_16_10: return 308;
    case KHDAYS_ASPECT_WINDOW:
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        /* the window's width at the picture's height: 384 lines for two screens, 192 for one */
        n = ph > 0 ? (int)((double)pw * (video.one_screen ? H : 2 * H) / ph) & ~1 : W;
        return n < W ? W : n > 3 * W ? 3 * W : n;
    default: return W;
    }
}

/* The antialiasing: the picture made this many times larger, then shrunk
 * (gfx_render.c); never at the DS's own 1x, kept for comparing with the DS. */
static int antialiasing_now(void)
{
    return video.internal_resolution != 1 && video.antialiasing > 1 ? video.antialiasing : 1;
}

/* The window's pixels a DS pixel the picture is shown at: the presentation
 * (the picture's columns, one screen's rows or two) fitted into the window. */
static float shown_scale(void)
{
    int pw = 0, ph = 0;
    const int rows = video.one_screen ? H : 2 * H;
    float sx, sy;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    sx = (float)pw / (float)columns_now();
    sy = (float)ph / (float)rows;
    return sx < sy ? sx : sy;
}

/* The internal resolution now: the setting, or for "window" the window's own size (smooth scaling:
 * the whole multiple at least as large, shrunk on the card; sharp: the largest that fits with square
 * DS pixels), times the antialiasing. If the card could not make a scale (refused_scale) the next one
 * down is used. */
static int scale_now(void)
{
    int n = video.internal_resolution;
    if (n == KHDAYS_RESOLUTION_WINDOW) {
        const float s = shown_scale();
        n = video.smooth_scaling ? (int)SDL_ceilf(s * (float)antialiasing_now() - 0.01f)
                                 : (int)SDL_floorf(s) * antialiasing_now();
        n = n < 1 ? 1 : n;
    } else {
        n *= antialiasing_now();
    }
    if (refused_scale > 1 && n >= refused_scale) {
        n = refused_scale - 1;
    }
    return n;
}

/* Whether engine A's 3D is drawn on the graphics card: at an internal
 * resolution above 1, or wider than the DS's. */
static int drawing_3d(void)
{
    /* (on one screen: always, with its 3D as wide as the picture) */
    return device != NULL && raster_ready && (scale_now() > 1 || columns_now() > W || video.one_screen);
}

int khdays_present_wants(void)
{
    /* not at a scale the device refused: then the 1x picture is shown */
    int wants = drawing_3d() && scale_now() != refused_scale ? KHDAYS_WANT_3D | KHDAYS_WANT_COMPOSITION : 0;
    if (port2d_ready) {
        /* the 3D for engine A's screen; the 2D engines' records only for checking against the old way */
        wants = KHDAYS_WANT_2D | KHDAYS_WANT_3D | (port2d_mode == 2 ? wants : 0);
    }
    if (video.one_screen) {
        wants |= KHDAYS_WANT_SITUATION; /* what the one-screen picture needs to tell the situation */
    }
#if defined(__ANDROID__)
    if (two_displays) {
        wants |= KHDAYS_WANT_SITUATION; /* whether the title shows (title_one) */
    }
#endif
    return wants;
}

int khdays_present_columns(void)
{
    return drawing_3d() ? columns_now() : W;
}

int khdays_present_scale(void)
{
    return port2d_ready && device != NULL ? scale_now() : 1;
}

/* Lays out the picture for its width now: the two screens one above the
 * other, the DS's 256 columns of each in the middle, fitted to the window. */
static void present_size(int w, int h);

static void layout(void)
{
    columns = khdays_present_columns();
    /* one screen: each frame sets what it shows (khdays_present_draw); until then, leave it */
    presented_w = presented_h = 0;
    present_size(columns, video.one_screen ? H : 2 * H);
}

void khdays_present_resized(void)
{
    layout();
}

/* Saves one screen as a PNG; `pixels` are 0x00RRGGBB. */
static void save_png(uint32_t frame, const char *which, const uint32_t *pixels, int w, int h)
{
    char path[512];
    uint8_t *rgba = (uint8_t *)SDL_malloc((size_t)w * (size_t)h * 4);
    const char *error;
    if (rgba == NULL) {
        return;
    }
    for (int i = 0; i < w * h; ++i) {
        rgba[4 * i] = (uint8_t)(pixels[i] >> 16);
        rgba[4 * i + 1] = (uint8_t)(pixels[i] >> 8);
        rgba[4 * i + 2] = (uint8_t)pixels[i];
        rgba[4 * i + 3] = 255;
    }
    SDL_snprintf(path, sizeof(path), "%s/frame_%05u_%s.png", shot_dir, frame, which);
    error = khdays_png_write(path, rgba, w, h);
    if (error != NULL) {
        fprintf(stderr, "host: cannot write %s: %s\n", path, error);
    } else {
        fprintf(stderr, "host: frame saved to %s (%dx%d)\n", path, w, h);
    }
    SDL_free(rgba);
}

/* The texture and width in DS columns a screen shows: engine `engine`'s
 * picture, or for engine B the capture of engine A's kept (the 3D on both
 * screens, gfx_render.c). */
static SDL_Texture *screen_texture(int engine, int *width)
{
    SDL_Texture *captured = engine == 1 ? khdays_gfx_render_captured(renderer, width) : NULL;
    if (captured != NULL) {
        return captured;
    }
    *width = khdays_gfx_render_columns(engine);
    return khdays_gfx_render_texture(renderer, engine);
}

/* The part of a screen's texture, `width` DS columns wide, that holds the
 * DS's own 256 columns: all of it, or the middle of engine A drawn wider. */
static SDL_FRect ds_columns(SDL_Texture *texture, int width)
{
    float pw = 0.0f, ph = 0.0f;
    SDL_FRect part;
    SDL_GetTextureSize(texture, &pw, &ph);
    if (width <= W) {
        part.x = 0.0f;
        part.w = pw;
    } else {
        const float per_column = pw / (float)width;
        part.x = (float)(width - W) / 2.0f * per_column;
        part.w = (float)W * per_column;
    }
    part.y = 0.0f;
    part.h = ph;
    return part;
}

/* KHDAYS_SHOTS_HD: the screens of this frame, as presented. */
static void save_shots(const KhdaysFramePackage *p)
{
    for (int n = 0; n < shot_count; ++n) {
        if (shot_frames[n] == p->frame && one_drawn) {
            /* the one-screen picture */
            int w = 0, h = 0;
            uint32_t *pixels = khdays_gfx_render_read(2, &w, &h);
            if (pixels != NULL) {
                save_png(p->frame, "one", pixels, w, h);
                SDL_free(pixels);
            }
        } else if (shot_frames[n] == p->frame && port2d_drawn) {
            /* the port's own screens, as they are presented */
            for (int e = 0; e < 2; ++e) {
                int w = 0, h = 0;
                uint32_t *pixels = khdays_gfx_render_read(e == 1 && khdays_gfx_render_captured_shown() ? 3 : e, &w, &h);
                if (pixels != NULL) {
                    save_png(p->frame, (e == 0) == (p->upper == 0) ? "upper" : "lower", pixels, w, h);
                    SDL_free(pixels);
                }
            }
        } else if (shot_frames[n] == p->frame) {
            int w = 0, h = 0;
            uint32_t *a = hd != NULL ? khdays_raster3d_read(&w, &h) : NULL;
            const char *engine_a = p->upper == 0 ? "upper" : "lower";
            if (a != NULL) {
                save_png(p->frame, engine_a, a, w, h);
                SDL_free(a);
            } else {
                save_png(p->frame, engine_a, p->screens[0], W, H);
            }
            save_png(p->frame, p->upper == 0 ? "lower" : "upper", p->screens[1], W, H);
        }
    }
}

/* KHDAYS_PROFILE: counts one frame drawn; `hd_ms` is how long its 3D at N x took (negative: none). */
static void count_frame(const KhdaysFramePackage *p, double hd_ms)
{
    if (last_frame != 0 && p->frame > last_frame + 1 + p->alike) {
        stats.not_drawn += (int)(p->frame - last_frame - 1 - p->alike); /* not counting frames that showed the same */
    }
    last_frame = p->frame;
    if (hd_ms >= 0) {
        stats.hd_frames++;
        stats.hd_ms += hd_ms;
        stats.hd_max_ms = hd_ms > stats.hd_max_ms ? hd_ms : stats.hd_max_ms;
    }
    if (++stats.frames == 300) {
        fprintf(stderr,
                "host: 300 frames drawn, %d frames the game showed not drawn; %d at internal resolution %d, "
                "prepared in %.2f ms on average, %.2f at most\n",
                stats.not_drawn, stats.hd_frames, scale_now(), stats.hd_frames ? stats.hd_ms / stats.hd_frames : 0.0,
                stats.hd_max_ms);
        SDL_zero(stats);
    }
}

/* KHDAYS_GFX=check at N x: compares engine A as the port composed it with the
 * same 3D composed the old way (gpu_raster3d.c's records). */
static void port2d_compare_hd(const KhdaysFramePackage *p, int scale)
{
    int w = 0, h = 0, w2 = 0, h2 = 0;
    uint32_t *mine = khdays_gfx_render_read(0, &w, &h), *old = NULL;
    unsigned long long differing = 0;
    khdays_raster3d_set_fine(0);
    if (mine != NULL && khdays_raster3d_draw(p, scale, video.texture_smoothing)) {
        old = khdays_raster3d_read(&w2, &h2);
    }
    if (mine != NULL && old != NULL && w == w2 && h == h2) {
        for (int i = 0; i < w * h; ++i) {
            differing += (mine[i] & 0xffffff) != (old[i] & 0xffffff);
        }
        if (differing != 0 && port2d_check.pixels == 0) {
            fprintf(stderr,
                    "host: the port's 2D at %dx: frame %u, engine A differs from the old composition in %llu "
                    "pixels\n",
                    scale, p->frame, differing);
            if (SDL_getenv("KHDAYS_SHOT_DIR") != NULL) {
                /* save both pictures, to look at */
                const char *saved = shot_dir;
                shot_dir = SDL_getenv("KHDAYS_SHOT_DIR");
                save_png(p->frame, "gfxcheck_port", mine, w, h);
                save_png(p->frame, "gfxcheck_old", old, w, h);
                shot_dir = saved;
            }
        }
        port2d_check.pixels += differing;
    }
    SDL_free(mine);
    SDL_free(old);
    if (++port2d_check.frames == 300) {
        fprintf(stderr, "host: the port's 2D at %dx against the old composition: %llu pixels differed in 300 frames\n",
                scale, port2d_check.pixels);
        port2d_check.frames = 0;
        port2d_check.pixels = 0;
    }
}

/* KHDAYS_GFX=check: reads the port's screens back and compares them with the 2D engines'. */
static void port2d_compare(const KhdaysFramePackage *p)
{
    for (int e = 0; e < 2; ++e) {
        int w = 0, h = 0;
        uint32_t *mine = khdays_gfx_render_read(e, &w, &h);
        unsigned long long differing = 0;
        if (mine == NULL || w != W || h != H) {
            SDL_free(mine);
            continue;
        }
        for (int i = 0; i < W * H; ++i) {
            differing += (mine[i] & 0xffffff) != (p->screens[e][i] & 0xffffff);
        }
        if (differing != 0 && port2d_check.pixels == 0) {
            fprintf(stderr, "host: the port's 2D: frame %u, engine %c differs in %llu pixels\n", p->frame,
                    e ? 'B' : 'A', differing);
        }
        port2d_check.pixels += differing;
        SDL_free(mine);
    }
    if (++port2d_check.frames == 300) {
        fprintf(stderr, "host: the port's 2D against the 2D engines: %llu pixels differed in 300 frames\n",
                port2d_check.pixels);
        port2d_check.frames = 0;
        port2d_check.pixels = 0;
    }
}

/* --- The 3D at the monitor's refresh rate ---------------- */

/* The 3D lists of the two latest game frames (the older first), the VBlanks they were first shown at,
 * when the newer arrived, and whether a cut separates them. The picture moves from the older to the
 * newer over one game frame from the newer's arrival: at most one game frame behind the DS. */
static KhdaysG3dList logic_list[2];
static uint64_t logic_time[2];
static uint64_t logic_arrival; /* the VBlank of the package that brought the newer list */
static uint32_t logic_serial = ~0u;
static int logic_count, logic_cut;
static uint8_t logic_upper;   /* the screen the newer list's 3D is on (the package's `upper`) */
static KhdaysG3dList between; /* the list being drawn, interpolated */
static KhdaysG3dInterp interp;
static const KhdaysFramePackage *latest; /* the package shown now */
static int latest_drawn_by_card;         /* its 3D was drawn by the graphics card */
static int backdrop_moving;              /* its one-screen picture has an animated backdrop */
/* That picture's settings buttons (KhdaysButton rectangles, 4 floats each, in the backdrop's
 * texels), the backdrop's size and anchor, and where the window drew the one picture
 * (khdays_present_draw: its left and width, in the renderer's logical units). */
static float *settings_buttons;
static int settings_button_count, settings_button_room;
static float button_picture_w, button_picture_h, button_ax, button_ay;
static int button_fit;
static float one_at_x, one_at_w;
static uint64_t last_refresh; /* the game's time of the last picture */

static void fps_count(int picture); /* Video: Show FPS (below) */

/* How the camera went from list `a` to list `b` (their view matrices: a world point w is w R + t in
 * the camera's space, which looks down -z): the cosine of the angle it turned, how far its matrix's
 * translation moved, and the point on its view axis it turned about -- `pivot` that point's distance
 * in front, `drift` how far it then is from b's view axis, `depth` its distance in front of b. */
typedef struct CameraStep {
    double cosine, moved, pivot, drift, depth;
} CameraStep;

static int camera_step(const KhdaysG3dList *a, const KhdaysG3dList *b, CameraStep *step);

/* KHDAYS_FRAME_LOG=1 writes a line per picture to saves/logs/frames-<time>.log. */
static FILE *frame_log;
static int frame_log_tried;

static void log_picture(const char *what, double alpha)
{
    if (!frame_log_tried) {
        frame_log_tried = 1;
        if (SDL_getenv("KHDAYS_FRAME_LOG") != NULL) {
            char path[160];
            SDL_Time now;
            SDL_DateTime t;
            SDL_GetCurrentTime(&now);
            SDL_TimeToDateTime(now, &t, true);
            SDL_snprintf(path, sizeof(path), "saves/logs/frames-%04d%02d%02d-%02d%02d%02d.log", t.year, t.month, t.day,
                         t.hour, t.minute, t.second);
            SDL_CreateDirectory("saves/logs");
            frame_log = fopen(path, "w");
            if (frame_log != NULL) {
                fprintf(frame_log, "# ms (the game's time), logic frame, alpha, polygons placed between / all, what, "
                                   "the two lists' times, the camera's step, the newer list's flushes, a cut\n");
            }
        }
    }
    if (frame_log != NULL) {
        /* the camera's step between the two lists (camera_step): the degrees it turned, its move, and
         * the point it turned about */
        CameraStep step = {1.0, 0.0, 0.0, 0.0, 0.0};
        if (logic_count == 2) {
            camera_step(&logic_list[0], &logic_list[1], &step);
        }
        fprintf(frame_log,
                "%.3f %u %.3f %d/%d %s %.3f %.3f moved %.4f turned %.3f pivot %.3f drift %.4f depth %.3f %d %d\n",
                (double)khdays_clock_cycles() * 1000.0 / KHDAYS_CLOCK_HZ, logic_serial, alpha, interp.matched,
                interp.polygons, what, (double)logic_time[0] * 1000.0 / KHDAYS_CLOCK_HZ,
                (double)logic_time[1] * 1000.0 / KHDAYS_CLOCK_HZ, step.moved,
                acos(step.cosine > 1.0    ? 1.0
                     : step.cosine < -1.0 ? -1.0
                                          : step.cosine) *
                    180.0 / 3.14159265358979,
                step.pivot, step.drift, step.depth, logic_list[1].flushes, logic_cut);
    }
}

/* Takes a package's 3D list as the newest game frame's, when its serial changed. */
static void note_logic_frame(const KhdaysFramePackage *p)
{
    KhdaysG3dList older;
    if (!(p->wants & KHDAYS_WANT_3D) || p->list3d_serial == logic_serial) {
        return;
    }
    older = logic_list[0];
    logic_list[0] = logic_list[1];
    logic_list[1] = older; /* reuse its memory for the copy */
    logic_time[0] = logic_time[1];
    if (!khdays_g3d_list_copy(&logic_list[1], &p->list3d)) {
        logic_count = 0; /* out of memory: no interpolation until there are two lists again */
    } else if (logic_count < 2) {
        ++logic_count;
    }
    fps_count(0);
    logic_time[1] = p->list3d_time;
    logic_arrival = p->time; /* the VBlank of the package that brought it */
    logic_serial = p->list3d_serial;
    /* nothing between two lists drawn on different screens either (the 3D on
     * both screens: one VBlank the upper's, the next the lower's) */
    logic_cut = p->list3d_cut || p->upper != logic_upper;
    logic_upper = p->upper;
}

static int camera_step(const KhdaysG3dList *a, const KhdaysG3dList *b, CameraStep *step)
{
    double r[3][3], s[3][3], m[3][3], t1[3], t2[3], c[3], trace = 0.0, moved = 0.0, along;
    if (a->flushes == 0 || b->flushes == 0) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r[i][j] = (double)a->camera[3 * i + j] / 4096.0;
            s[i][j] = (double)b->camera[3 * i + j] / 4096.0;
        }
        t1[i] = (double)a->camera[9 + i] / 4096.0;
        t2[i] = (double)b->camera[9 + i] / 4096.0;
        moved += (t2[i] - t1[i]) * (t2[i] - t1[i]);
    }
    /* m = r^T s takes a's camera space to b's: q2 = (q1 - t1) m + t2 */
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            m[i][j] = r[0][i] * s[0][j] + r[1][i] * s[1][j] + r[2][i] * s[2][j];
        }
        trace += m[i][i];
    }
    for (int j = 0; j < 3; ++j) {
        c[j] = t2[j] - (t1[0] * m[0][j] + t1[1] * m[1][j] + t1[2] * m[2][j]);
    }
    /* a's axis point (0, 0, -d) lands at c - d m[2]: the d that keeps it nearest b's axis */
    along = m[2][0] * m[2][0] + m[2][1] * m[2][1];
    step->cosine = (trace - 1.0) / 2.0; /* trace(r^T s) = 1 + 2 cos(angle) */
    step->moved = sqrt(moved);
    step->pivot = along > 1e-9 ? (m[2][0] * c[0] + m[2][1] * c[1]) / along : 0.0;
    step->drift = hypot(c[0] - step->pivot * m[2][0], c[1] - step->pivot * m[2][1]);
    step->depth = step->pivot * m[2][2] - c[2];
    return 1;
}

/* Whether the camera cut between two lists: it turned more than the field camera can in one game
 * frame (Ov002_Camera_UpdateFollow caps its turn at 0xa00, 14.06 degrees), or it moved more than 1.0
 * (normal frames move at most about 0.05, a cutscene's cuts 3.2 or more) other than by turning about
 * a point in front of it that stays in the middle of the view (the field camera turned around the
 * player: that point moves no more than 1.0, nearer or aside). */
static int camera_cut(const KhdaysG3dList *a, const KhdaysG3dList *b)
{
    CameraStep step;
    if (!camera_step(a, b, &step)) {
        return 0;
    }
    if (step.cosine < cos(14.0625 * 3.14159265358979 / 180.0)) {
        return 1;
    }
    return step.moved > 1.0 && !(step.pivot > 0.0 && step.drift <= 1.0 && fabs(step.depth - step.pivot) <= 1.0);
}

/* The list to draw for a picture now: one interpolated between the two
 * latest game frames, or NULL to draw the package's own (at the DS's pace,
 * after a cut, without two lists yet, or once the newest is reached). */
static const KhdaysG3dList *picture_list(const KhdaysFramePackage *p, double *alpha)
{
    uint64_t now, period;
    *alpha = 1.0;
    if (video.picture_ds || !(p->wants & KHDAYS_WANT_3D) || logic_count < 2 || logic_cut ||
        p->list3d_serial != logic_serial || logic_time[1] <= logic_time[0]) {
        return NULL;
    }
    if (camera_cut(&logic_list[0], &logic_list[1])) {
        logic_cut = 1; /* until the next game frame */
        return NULL;
    }
    now = khdays_clock_cycles();
    period = logic_time[1] - logic_time[0];
    *alpha = now <= logic_arrival ? 0.0 : (double)(now - logic_arrival) / (double)period;
    if (*alpha >= 1.0) {
        *alpha = 1.0;
        return NULL;
    }
    if (!khdays_g3d_interpolate(&interp, &logic_list[0], &logic_list[1], (float)*alpha, &between)) {
        return NULL;
    }
    return &between;
}

static void present(const KhdaysFramePackage *p, int first);

/* Video: Show FPS -- counts the pictures shown (khdays_present_draw) and the
 * game frames that arrived, over half a second at a time. */
static unsigned fps_pictures, fps_frames;
static int fps_shown[2];
static Uint64 fps_since;

static void fps_count(int picture)
{
    const Uint64 now = SDL_GetTicksNS();
    if (picture) {
        ++fps_pictures;
    } else {
        ++fps_frames;
    }
    if (fps_since == 0) {
        fps_since = now;
    } else if (now - fps_since >= 500000000u) {
        const double seconds = (double)(now - fps_since) / 1e9;
        fps_shown[0] = (int)(fps_pictures / seconds + 0.5);
        fps_shown[1] = (int)(fps_frames / seconds + 0.5);
        fps_pictures = fps_frames = 0;
        fps_since = now;
    }
}

int khdays_present_fps(int *pictures, int *frames)
{
    *pictures = fps_shown[0];
    *frames = fps_shown[1];
    return video.show_fps;
}

/* The game's pause over an HD scene's paused video: the video's picture as RGBA, taken once a pause
 * (-1 when none could be), in two buffers used in turn, so the next pause's picture is a backdrop
 * the card has not kept (gfx_render.c knows a backdrop by its address). */
static uint8_t *paused_rgba[2];
static size_t paused_capacity[2];
static int paused_turn, paused_w, paused_h, paused_got;

/* One screen: the video's picture behind everything, fitted inside the picture as the video shows,
 * and no 3D. The pause's panel is the upper screen's 2D (seen: engine A's BG3 over the scene's 3D,
 * the 3D alone darkened), which the situation's shapes draw over it; the scene the game holds under
 * the pause is its 3D. */
static void under_paused_video(KhdaysOneScreenDraw *one)
{
    if (paused_got == 0) {
        paused_turn ^= 1;
        uint8_t **rgba = &paused_rgba[paused_turn];
        paused_got = khdays_hd_picture_rgba(rgba, &paused_capacity[paused_turn], &paused_w, &paused_h) ? 1 : -1;
    }
    if (paused_got > 0) {
        one->backdrop = paused_rgba[paused_turn];
        one->backdrop_w = paused_w;
        one->backdrop_h = paused_h;
        one->backdrop_ax = one->backdrop_ay = 0.5f;
        one->backdrop_fit = 1;
        one->animated = 0;
        one->picture_w = one->picture_h = 0;
        one->pieces = NULL;
        one->piece_count = 0;
    }
    one->hide3d_all = 1;
}

void khdays_present_package(const KhdaysFramePackage *p)
{
    if (!khdays_movie_hd_scene_paused()) {
        paused_got = 0; /* the next pause takes the video's picture anew */
    }
    note_logic_frame(p);
    latest = p;
    present(p, 1);
}

int khdays_present_smooth(void)
{
    return !video.picture_ds && latest != NULL && latest_drawn_by_card && logic_count == 2;
}

int khdays_present_settings_button(void)
{
    return latest != NULL && one_drawn && settings_button_count > 0;
}

int khdays_present_settings_button_at(float x, float y)
{
    float lx = 0.0f, ly = 0.0f, s, bx, by;
    if (!khdays_present_settings_button() || renderer == NULL || one_at_w <= 0.0f || button_picture_w <= 0.0f ||
        !SDL_RenderCoordinatesFromWindow(renderer, x, y, &lx, &ly)) {
        return 0;
    }
    /* the window's point in the one picture, then in the backdrop that covers it or fits in it
     * (gfx_render.c) */
    s = (one_at_w / button_picture_w > (float)H / button_picture_h) != (button_fit != 0) ? one_at_w / button_picture_w
                                                                                         : (float)H / button_picture_h;
    bx = (lx - one_at_x - (one_at_w - button_picture_w * s) * button_ax) / s;
    by = (ly - ((float)H - button_picture_h * s) * button_ay) / s;
    for (int b = 0; b < settings_button_count; ++b) {
        const float *r = &settings_buttons[4 * b];
        if (bx >= r[0] && bx < r[2] && by >= r[1] && by < r[3]) {
            return 1;
        }
    }
    return 0;
}

int khdays_present_backdrop_moving(void)
{
    return latest != NULL && backdrop_moving;
}

int khdays_present_refresh_backdrop(void)
{
    if (!khdays_present_backdrop_moving()) {
        return 0;
    }
    present(latest, 0);
    return 1;
}

int khdays_present_settling(void)
{
    return latest != NULL && video.one_screen && khdays_onescreen_settling(latest);
}

int khdays_present_refresh_settling(void)
{
    static uint32_t refreshed_at;
    const uint32_t now = khdays_events_vblank_count();
    if (!khdays_present_settling() || now == refreshed_at) {
        return 0; /* (once a VBlank) */
    }
    refreshed_at = now;
    present(latest, 0);
    return 1;
}

int khdays_present_refresh(void)
{
    const uint64_t now = khdays_clock_cycles();
    if (!khdays_present_smooth() || now == last_refresh) {
        return 0; /* the game's time stands still (paused): keep the same picture */
    }
    present(latest, 0);
    return 1;
}

/* Draws package `p`. `first` is set the first time (so its shots and profile
 * are counted only once). */
static void present(const KhdaysFramePackage *p, int first)
{
    const uint32_t full = KHDAYS_WANT_3D | KHDAYS_WANT_COMPOSITION;
    int scale;
    Uint64 start;
    double alpha;
    const KhdaysG3dList *list = picture_list(p, &alpha);
    {
        /* KHDAYS_TEST_GPU_LOST=FRAME: at that frame, pretend the card was lost, to check the port remakes its
         * renderer and carries on */
        static long lost_at = -2;
        if (lost_at == -2) {
            const char *v = SDL_getenv("KHDAYS_TEST_GPU_LOST");
            lost_at = v != NULL ? SDL_strtol(v, NULL, 10) : -1;
        }
        if (lost_at >= 0 && first && p->frame >= (uint32_t)lost_at) {
            lost_at = -1;
            SDL_SetError("KHDAYS_TEST_GPU_LOST (0x887A0006)");
            check_lost();
            return;
        }
    }
    last_refresh = khdays_clock_cycles();
    log_picture(list != NULL ? "between" : logic_cut ? "cut" : "newest", alpha);
    khdays_raster3d_use_list(list);
    latest_drawn_by_card = 0;
    port2d_drawn = 0;
#if defined(__ANDROID__)
    if (two_displays && first && title_screens(p) != title_one) {
        title_one = !title_one;
        khdays_present_set_video(&given_video); /* the title made as on one screen, or no longer */
    }
#endif
    if (port2d_ready && (p->wants & KHDAYS_WANT_2D)) {
        /* Engine A's 3D is drawn at N x (also at 1x: the software 3D is off
         * while the window draws it), then composed with the 2D. With
         * KHDAYS_GFX=check at 1x the software 3D is used instead, to compare
         * the 2D with the 2D engines' exactly. */
        int n = scale_now();
        /* 8 bits per channel when a mod's HD textures are drawn (not when
         * comparing with the old way, which keeps the DS's 6) */
        khdays_raster3d_set_fine(port2d_mode == 1 && khdays_textures_replacements() > 0);
        KhdaysComposition composition;
        KhdaysOneScreenDraw one;
        int drawn, picture = 0;
        SDL_zero(composition);
        SDL_zero(one);
        one_drawn = 0;
        backdrop_moving = 0;
        if (video.one_screen) {
            /* One screen: first the situation and its composition. A picture of the upper or lower screen is
             * made by the one-screen pass, as wide as the 3D list; "side by side" and "stacked" show the two
             * screens as they are. */
            const int wide = (int)SDL_lroundf(p->list3d.widen * W);
            khdays_onescreen_prepare(p, wide > W ? wide : W, &composition, &one);
            one_kind = composition.screens;
            picture = one_kind == KHDAYS_SCREENS_UPPER || one_kind == KHDAYS_SCREENS_LOWER;
            backdrop_moving = picture && composition.animated;
            settings_button_count = 0;
            for (int b = 0; picture && b < composition.button_count; ++b) {
                if (SDL_strcmp(composition.buttons[b].action, "open_settings") != 0) {
                    continue;
                }
                if (settings_button_count >= settings_button_room) {
                    const int room = settings_button_room != 0 ? 2 * settings_button_room : 4;
                    float *more = (float *)SDL_realloc(settings_buttons, (size_t)room * 4 * sizeof(float));
                    if (more == NULL) {
                        break;
                    }
                    settings_buttons = more;
                    settings_button_room = room;
                }
                SDL_memcpy(&settings_buttons[4 * settings_button_count++], composition.buttons[b].rect,
                           4 * sizeof(float));
            }
            button_picture_w = (float)composition.picture_w;
            button_picture_h = (float)composition.picture_h;
            button_ax = composition.backdrop_ax;
            button_ay = composition.backdrop_ay;
            button_fit = composition.backdrop_fit;
            if (picture && one_made && !khdays_onescreen_makes_picture(p)) {
                /* this VBlank's 3D is the other screen's: keep the previous picture */
                one_drawn = 1;
                port2d_drawn = 1;
                khdays_raster3d_use_list(NULL);
                return;
            }
        }
        /* A picture only (an HD backdrop with `hide3d all` and none of the DS's screens in it: the title):
         * made at the smallest whole scale that covers the window, then shrunk to its pixels. The
         * antialiasing is for the 3D's edges, and neither the DS's 3D nor its 2D shows in it: at the
         * window's 5x times 4x antialiasing the Thor's card took a second a picture. */
        const int picture_only = picture && one.backdrop != NULL && one.hide3d_all && one.shape_count == 0;
        if (picture_only) {
            n = (int)SDL_ceilf(shown_scale() - 0.01f);
            n = n < 1 ? 1 : n;
        }
        khdays_raster3d_set_one_screen(picture);
        khdays_raster3d_set_hud_shapes(picture ? composition.shapes3d : NULL, picture ? composition.shape3d_count : 0);
        /* a screen blend's frame: engine A shows a VRAM bank, drawn from its 3D at this resolution too */
        const int blend_frame = khdays_gfx_render_capture_frame(p);
        const int raster = !picture_only && raster_ready && !(port2d_mode == 2 && n == 1 && columns_now() == W) &&
                           n != refused_scale && (p->wants & KHDAYS_WANT_3D) && (p->info[0].is3d || blend_frame) &&
                           khdays_raster3d_rasterize(p, n, video.texture_smoothing);
        /* shrunk to the size the window shows it at (smooth scaling: its own pixels), or by the
         * antialiasing (sharp: the DS's pixels kept square, then stretched by whole pixels) */
        khdays_gfx_render_set_shown(video.smooth_scaling || picture_only ? shown_scale()
                                                                         : (float)n / (float)antialiasing_now());
        if (picture) {
            SDL_GPUTexture *c3d = NULL, *a3d = NULL;
            int raster_columns = one.columns;
            if (raster && khdays_raster3d_targets(&c3d, &a3d, &raster_columns)) {
                one.columns = raster_columns;
            }
            if (khdays_movie_hd_scene_paused()) {
                under_paused_video(&one);
            }
            /* at the DS's own size without 3D, but for a picture with a backdrop of its own (the
             * camp's Preferences page: an HD picture, no model) */
            drawn = khdays_gfx_render_draw_one(p, raster || one.backdrop != NULL ? n : 1, raster, &one);
            one_drawn = drawn;
            one_made = one_made || drawn;
        } else {
            drawn = khdays_gfx_render_draw(p, raster ? n : 1, raster);
        }
        if (drawn) {
            port2d_drawn = 1;
            port2d_upper = p->upper;
            latest_drawn_by_card = raster;
            khdays_raster3d_use_list(NULL);
            if (port2d_mode == 2 && !blend_frame) { /* a screen blend's frame is not the DS's picture */
                if (raster) {
                    port2d_compare_hd(p, n);
                } else {
                    port2d_compare(p);
                }
            }
            hd = NULL;
            if (first) {
                save_shots(p);
                if (profile) {
                    count_frame(p, -1.0);
                }
            }
            return;
        }
        fprintf(stderr, "host: the port's 2D could not draw frame %u: %s\n", p->frame, SDL_GetError());
        check_lost();
        if (device_lost) {
            return;
        }
    }
    memcpy(shown, p->screens[p->upper], sizeof(p->screens[0]));
    memcpy(shown + W * H, p->screens[!p->upper], sizeof(p->screens[0]));
    if (screens != NULL) {
        SDL_UpdateTexture(screens, NULL, shown, W * 4);
    }
    hd = NULL;
    scale = scale_now();
    if (!drawing_3d() || (p->wants & full) != full || !p->info[0].is3d || scale == refused_scale) {
        khdays_raster3d_use_list(NULL);
        if (first) {
            save_shots(p); /* the 1x picture: no 3D this frame, or none was asked for */
            if (profile) {
                count_frame(p, -1.0);
            }
        }
        return;
    }
    start = SDL_GetPerformanceCounter();
    khdays_raster3d_set_fine(0);
    hd = khdays_raster3d_draw(p, scale, video.texture_smoothing) ? khdays_raster3d_texture(renderer) : NULL;
    if (hd == NULL) {
        check_lost();
        if (device_lost) {
            return;
        }
    }
    khdays_raster3d_use_list(NULL);
    latest_drawn_by_card = hd != NULL;
    if (profile && first) {
        count_frame(p, (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / (double)SDL_GetPerformanceFrequency());
    }
    hd_upper = p->upper == 0;
    hd_columns = (int)SDL_lroundf(p->list3d.widen * W);
    if (hd == NULL) {
        char text[480];
        SDL_snprintf(text, sizeof(text), khdays_text(TXT_SCALE_REFUSED), scale, SDL_GetError());
        if (notice != NULL) {
            notice(text);
        }
        refused_scale = scale;
    } else {
        SDL_SetTextureScaleMode(hd, video.smooth_scaling ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
    if (first) {
        save_shots(p);
    }
}

void khdays_present_screens(const uint32_t *upper_then_lower)
{
    memcpy(shown, upper_then_lower, sizeof(shown));
    if (screens != NULL) {
        SDL_UpdateTexture(screens, NULL, shown, W * 4);
    }
}

/* Read by the game thread (textures.c): two ints, each written in one go. */
static volatile int graphics_flags;

int khdays_host_graphics_flags(void)
{
    return graphics_flags;
}

#if defined(__ANDROID__)
int khdays_present_title_alone(void)
{
    return two_displays && title_one && video.one_screen && one_drawn;
}

void khdays_present_hold_bottom(int held)
{
    bottom_held = held;
}
#endif

void khdays_present_set_video(const KhdaysVideo *given)
{
    KhdaysVideo copy = *given;
    const KhdaysVideo *v = &copy;
#if defined(__ANDROID__)
    /* the lower screen on a display of its own: the DS's two screens, each where it belongs, whatever
     * the screens setting says -- but the title, made as on one screen (title_one) */
    given_video = *given;
    two_displays = khdays_bottom_attached();
    if (!two_displays) {
        title_one = 0;
    } else {
        copy.one_screen = title_one;
    }
#endif
    graphics_flags =
        (v->hd_graphics ? KHDAYS_HOST_HD_GRAPHICS : 0) | (v->dump_graphics ? KHDAYS_HOST_DUMP_GRAPHICS : 0) |
        (v->movies == KHDAYS_MOVIES_DS ? KHDAYS_HOST_MOVIES_DS : 0) |
        (v->movies == KHDAYS_MOVIES_HD || v->movies == KHDAYS_MOVIES_HD_SMOOTH ? KHDAYS_HOST_MOVIES_HD : 0) |
        (v->movies == KHDAYS_MOVIES_HD_SMOOTH ? KHDAYS_HOST_MOVIES_SMOOTH : 0) |
        (v->one_screen ? KHDAYS_HOST_ONE_SCREEN : 0);
    khdays_subtitles_set(v->subtitles);
    if (v->internal_resolution != video.internal_resolution || v->antialiasing != video.antialiasing) {
        refused_scale = 0; /* try the new choice */
    }
    if (v->backend != video.backend && renderer != NULL) {
        backend_change = 1; /* the window remakes the renderer with it (khdays_present_lost) */
    }
    video = *v;
    khdays_composition_set_hud_size(video.hud_size);
    if (screens != NULL) {
        SDL_SetTextureScaleMode(screens, video.smooth_scaling ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
    layout();
}

/* The logical presentation `w` x `h`, set when it changes. */
static void present_size(int w, int h)
{
    if (renderer != NULL && (w != presented_w || h != presented_h)) {
        SDL_SetRenderLogicalPresentation(renderer, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        presented_w = w;
        presented_h = h;
    }
}

/* An HD video in a movie's place: its newest picture due, over the whole window, letterboxed; the last
 * one stays while the game finishes its movie. */
static void draw_hd_movie(void)
{
    KhdaysHdPicture p;
    if (khdays_hd_picture_begin(&p)) {
        if (hd_movie == NULL || p.width != hd_movie_w || p.height != hd_movie_h) {
            const SDL_PropertiesID props = SDL_CreateProperties();
            if (hd_movie != NULL) {
                SDL_DestroyTexture(hd_movie);
            }
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_NV12);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, p.width);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, p.height);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_LIMITED);
            hd_movie = SDL_CreateTextureWithProperties(renderer, props);
            SDL_DestroyProperties(props);
            hd_movie_w = p.width;
            hd_movie_h = p.height;
            if (hd_movie == NULL) {
                static int said;
                if (!said) {
                    said = 1;
                    fprintf(stderr, "host: no NV12 texture for the HD video: %s\n", SDL_GetError());
                    khdays_log("graphics: no NV12 texture for the HD video: %s", SDL_GetError());
                }
            } else {
                SDL_SetTextureScaleMode(hd_movie, SDL_SCALEMODE_LINEAR);
            }
        }
        if (hd_movie != NULL) {
            SDL_UpdateNVTexture(hd_movie, NULL, p.luma, p.pitch, p.chroma, p.pitch);
        }
        hd_movie_time = p.time;
        hd_movie_fps = p.fps;
        khdays_hd_picture_done();
    }
    if (hd_movie != NULL) {
        present_size(hd_movie_w, hd_movie_h);
        SDL_RenderTexture(renderer, hd_movie, NULL, NULL);
        if (khdays_subtitles_shown()) {
            khdays_movie_subtitles_draw(renderer, hd_movie_time, hd_movie_fps, hd_movie_w, hd_movie_h);
        }
    }
}

int khdays_present_one_picture(void)
{
    return one_drawn && (one_kind == KHDAYS_SCREENS_UPPER || one_kind == KHDAYS_SCREENS_LOWER);
}

/* The scale mode for texture `t` shown as `w` x `h` DS pixels: nearest when it already has the
 * window's pixels for them (shrunk to the size shown, gfx_render.c) -- drawn one to one, a filter
 * would only blur it -- else the setting's. */
static SDL_ScaleMode mode_for(SDL_Texture *t, float w, float h)
{
    float tw = 0.0f, th = 0.0f;
    const float s = shown_scale();
    if (SDL_GetTextureSize(t, &tw, &th) && SDL_fabsf(tw - w * s) <= 1.0f && SDL_fabsf(th - h * s) <= 1.0f) {
        return SDL_SCALEMODE_NEAREST;
    }
    return video.smooth_scaling ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
}

#if defined(__ANDROID__)
/* The lower screen on the second display (host/android/bottom_screen.h): drawn into a texture of its
 * own size, read back and shown there. */
static SDL_Texture *bottom_target;
static int bottom_w, bottom_h;

static void send_bottom(SDL_Texture *lower, const SDL_FRect *from)
{
    const int w = (int)from->w, h = (int)from->h;
    SDL_Texture *previous;
    SDL_Surface *read, *rgba;
    if (w <= 0 || h <= 0 || bottom_held) {
        return;
    }
    if (bottom_target == NULL || bottom_w != w || bottom_h != h) {
        if (bottom_target != NULL) {
            SDL_DestroyTexture(bottom_target);
        }
        bottom_target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
        bottom_w = w;
        bottom_h = h;
        if (bottom_target == NULL) {
            return;
        }
    }
    previous = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, bottom_target);
    SDL_SetTextureScaleMode(lower, SDL_SCALEMODE_NEAREST);
    SDL_RenderTexture(renderer, lower, from, NULL);
    read = SDL_RenderReadPixels(renderer, NULL);
    SDL_SetRenderTarget(renderer, previous);
    if (read == NULL) {
        return;
    }
    rgba = read->format == SDL_PIXELFORMAT_ABGR8888 ? read : SDL_ConvertSurface(read, SDL_PIXELFORMAT_ABGR8888);
    if (rgba != NULL) {
        khdays_bottom_present((const uint8_t *)rgba->pixels, rgba->w, rgba->h, rgba->pitch);
        if (rgba != read) {
            SDL_DestroySurface(rgba);
        }
    }
    SDL_DestroySurface(read);
}

/* With the lower screen on the second display: the window shows the upper screen alone, as wide as it
 * was drawn. Returns 0 when there is nothing to show that way yet. */
static int draw_two_displays(void)
{
    SDL_Texture *upper, *lower;
    SDL_FRect upper_from, lower_from;
    float wu;
    if (port2d_drawn) {
        int cu = 0, cl = 0;
        float pw = 0.0f, ph = 0.0f;
        upper = screen_texture(port2d_upper, &cu);
        lower = screen_texture(!port2d_upper, &cl);
        if (upper == NULL || lower == NULL) {
            return 0;
        }
        SDL_GetTextureSize(upper, &pw, &ph);
        upper_from = (SDL_FRect){0.0f, 0.0f, pw, ph};
        lower_from = ds_columns(lower, cl);
        wu = (float)cu;
    } else if (screens != NULL) {
        upper = lower = screens;
        upper_from = (SDL_FRect){0.0f, 0.0f, W, H};
        lower_from = (SDL_FRect){0.0f, H, W, H};
        wu = W;
    } else {
        return 0;
    }
    present_size((int)wu, H);
    {
        const SDL_FRect at = {0.0f, 0.0f, wu, H};
        SDL_SetTextureScaleMode(upper, mode_for(upper, wu, H));
        SDL_RenderTexture(renderer, upper, &upper_from, &at);
    }
    if (hd != NULL && hd_upper) {
        const SDL_FRect place = {(wu - (float)hd_columns) / 2, 0.0f, (float)hd_columns, H};
        SDL_RenderTexture(renderer, hd, NULL, &place);
    }
    send_bottom(lower, &lower_from);
    return 1;
}
#endif

void khdays_present_draw(void)
{
    const float left = (float)(columns - W) / 2;
    if (device_lost) {
        return;
    }
    fps_count(1);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    /* the game's pause over a scene's paused video: on one screen its panel over the video's picture
     * (under_paused_video), on two the game's screens as they are (the panel is engine A's 2D, over the
     * scene's 3D: no picture of it apart) */
    const int paused = khdays_movie_hd_scene_paused();
    if (khdays_movie_hd_showing() && !paused) {
        draw_hd_movie();
        return;
    }
    if (hd_movie != NULL && !paused) {
        SDL_DestroyTexture(hd_movie); /* the video is over */
        hd_movie = NULL;
        khdays_movie_subtitles_release();
    }
#if defined(__ANDROID__)
    if (khdays_bottom_attached() != two_displays) {
        khdays_present_set_video(&given_video); /* the second display came or went */
    }
    if (two_displays && video.one_screen && one_drawn) {
        send_black_bottom(); /* the HD title on the upper display (below), the lower one black */
    } else if (two_displays && draw_two_displays()) {
        return;
    }
#endif
    if (one_drawn) {
        /* the one picture, as wide as it was made, in the middle; nothing touched */
        int made = 0;
        SDL_Texture *picture = khdays_gfx_render_picture(renderer, &made);
        present_size(columns, H);
        if (picture != NULL) {
            const SDL_FRect at = {((float)columns - (float)made) / 2, 0, (float)made, H};
            one_at_x = at.x;
            one_at_w = at.w;
            SDL_SetTextureScaleMode(picture, mode_for(picture, at.w, at.h));
            SDL_RenderTexture(renderer, picture, NULL, &at);
        }
    } else if (port2d_drawn && video.one_screen && one_kind == KHDAYS_SCREENS_SIDE_BY_SIDE) {
        /* Both screens whole, side by side, each as the DS's 256 columns, touching: the upper on the left,
         * the lower at its right. Engine A drawn wider keeps only its middle 256 (its extra columns showed
         * as a band between the two). */
        int wu = 0, wl = 0;
        SDL_Texture *upper = screen_texture(port2d_upper, &wu);
        SDL_Texture *lower = screen_texture(!port2d_upper, &wl);
        present_size(2 * W, H);
        if (upper != NULL && lower != NULL) {
            const SDL_ScaleMode mode = video.smooth_scaling ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
            const SDL_FRect upper_from = ds_columns(upper, wu), lower_from = ds_columns(lower, wl);
            const SDL_FRect upper_at = {0, 0, W, H}, lower_at = {W, 0, W, H};
            SDL_SetTextureScaleMode(upper, mode);
            SDL_SetTextureScaleMode(lower, mode);
            SDL_RenderTexture(renderer, upper, &upper_from, &upper_at);
            SDL_RenderTexture(renderer, lower, &lower_from, &lower_at);
        }
    } else if (port2d_drawn) {
        /* stacked: the two-screen picture. Set each frame, as every picture sets its own: an HD video
         * left its size behind (seen: after hd818's 1280 x 720, the two screens letterboxed into it at
         * 0.4 of their size) */
        present_size(columns, 2 * H);
        /* the port's own screens, the DS's 256 columns in the middle (engine A as wide as it was drawn) */
        int cu = 0, cl = 0;
        SDL_Texture *upper = screen_texture(port2d_upper, &cu);
        SDL_Texture *lower = screen_texture(!port2d_upper, &cl);
        const float wu = (float)cu, wl = (float)cl;
        const SDL_FRect upper_at = {((float)columns - wu) / 2, 0, wu, H},
                        lower_at = {((float)columns - wl) / 2, H, wl, H};
        if (upper != NULL && lower != NULL) {
            SDL_SetTextureScaleMode(upper, mode_for(upper, wu, H));
            SDL_SetTextureScaleMode(lower, mode_for(lower, wl, H));
            SDL_RenderTexture(renderer, upper, NULL, &upper_at);
            SDL_RenderTexture(renderer, lower, NULL, &lower_at);
        }
    } else if (screens != NULL) {
        /* the 1x screens, the DS's 256 columns in the middle */
        const SDL_FRect upper = {0, 0, W, H}, lower = {0, H, W, H};
        const SDL_FRect upper_at = {left, 0, W, H}, lower_at = {left, H, W, H};
        present_size(columns, 2 * H);
        SDL_RenderTexture(renderer, screens, &upper, &upper_at);
        SDL_RenderTexture(renderer, screens, &lower, &lower_at);
    }
    if (hd != NULL) {
        /* engine A drawn again, as wide as its list was made */
        const SDL_FRect place = {(float)(columns - hd_columns) / 2, hd_upper ? 0.0f : (float)H, (float)hd_columns,
                                 (float)H};
        SDL_RenderTexture(renderer, hd, NULL, &place);
    }
}

SDL_Renderer *khdays_present_event(const SDL_Event *event, int *renewed)
{
    *renewed = 0;
    if (event->type == SDL_EVENT_RENDER_DEVICE_RESET) {
        /* the textures are gone: the picture is uploaded again (an HD video's
         * with its next picture) */
        if (screens != NULL) {
            SDL_DestroyTexture(screens);
        }
        if (hd_movie != NULL) {
            SDL_DestroyTexture(hd_movie);
            hd_movie = NULL;
        }
        make_texture();
        *renewed = 1;
    } else if (event->type == SDL_EVENT_RENDER_DEVICE_LOST) {
        /* a driver reset, or a laptop switching graphics cards: a new device (or SDL's own renderer), the
         * game carrying on. Lost twice at the same internal resolution above 1, that resolution is refused
         * for the session (said on the settings menu). */
        const int at = scale_now();
        const int lost = device_lost || !backend_change; /* SDL's event, or the port noticed it */
        fprintf(stderr, lost ? "host: the graphics device was lost; making the renderer again\n"
                             : "host: another graphics API chosen; making the renderer again\n");
        if (lost) {
            lost_times = at == lost_scale ? lost_times + 1 : 1;
            lost_scale = at;
        }
        backend_change = 0;
        if (lost && lost_times >= 2 && at > 1) {
            char text[480];
            SDL_snprintf(text, sizeof(text), khdays_text(TXT_SCALE_REFUSED), at,
                         "the graphics card stopped answering twice");
            if (notice != NULL) {
                notice(text);
            }
            khdays_log("graphics: internal resolution %d refused for the session (the card stopped answering twice)",
                       at);
            refused_scale = at;
        }
        device_lost = 0;
        destroy();
        if (!make_renderer()) {
            fprintf(stderr, "host: no renderer after the device was lost: %s\n", SDL_GetError());
        }
        *renewed = 1;
    }
    return renderer;
}
