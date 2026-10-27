/* bottom_screen.h on Android: the surface and the touches arrive from the Java side's UI thread (JNI,
 * BottomScreenBridge.kt), the frames from the window's thread; one lock guards what they share. */
#include "bottom_screen.h"

#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../runtime/core/rom.h"
#include "../render/png_io.h"

#define DS_W 256
#define DS_H 192
#define OPAQUE_BLACK 0xff000000u

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ANativeWindow *window;
static int surface_w, surface_h;
static int geometry_set, lock_failed_logged;
static int touch_down;
static float touch_nx, touch_ny;
static int *columns; /* for each destination column, the source column (nearest) */
static int columns_src, columns_dst;
static int mark_shown; /* the display shows the title's logo, or the crown mark (khdays_bottom_present_mark) */

/* The title's logo (the heart, the crown, KINGDOM HEARTS 358/2 Days): the opening movie's last picture,
 * the logo on black, as the app keeps it (MainActivity.saveTitleFrame), cut to what is not black; else
 * cut from the HD menu's picture (a mod's layouts/title/<lang>/, tools/hd/theater_title.py). RGBA rows. */
static uint8_t *logo;
static int logo_w, logo_h, logo_looked;
#define LOGO_WIDTH_SHARE 0.86f  /* at most this much of the display's width */
#define LOGO_HEIGHT_SHARE 0.80f /* and of its height */
#define TITLE_LAYOUT_WIDTH 1920 /* the main menu's picture (its `picture` line) */
#define TITLE_FRAME "cache/title_frame.png" /* MainActivity.TITLE_FRAME, in the game's folder */
#define FRAME_DARK 24                       /* a frame's pixels this dark or darker are its black */
#define FRAME_MARGIN 0.06f                  /* the logo cut with this much of its size around it */

/* The crown mark of the app's icon (android/.../ic_launcher_foreground.png), its corners in the icon's
 * 432 units; its fill and outline, R G B A bytes as a little-endian word. */
static const float crown[][2] = {{110, 173}, {167, 220}, {216, 132}, {265, 220}, {322, 173},
                                 {288, 278}, {250, 298}, {216, 306}, {180, 298}, {145, 278}};
#define CROWN_CORNERS (int)(sizeof(crown) / sizeof(crown[0]))
#define CROWN_TOP 132.0f
#define CROWN_BOTTOM 306.0f
#define CROWN_MIDDLE 216.0f
#define CROWN_SHARE 0.22f    /* its height, of the display's */
#define CROWN_OUTLINE 4.0f   /* the outline's width, in icon units */
#define CROWN_SAMPLES 4      /* a pixel's samples a side, for smooth edges */
#define CROWN_FILL 0xffe0e4e6u
#define CROWN_EDGE 0xff947c78u

/* Where the screen lands on a surface: the largest 4:3 rectangle that fits, centred. */
static void fit(int w, int h, int *x, int *y, int *fw, int *fh)
{
    if ((long long)w * DS_H <= (long long)h * DS_W) {
        *fw = w;
        *fh = w * DS_H / DS_W;
    } else {
        *fh = h;
        *fw = h * DS_W / DS_H;
    }
    *x = (w - *fw) / 2;
    *y = (h - *fh) / 2;
}

JNIEXPORT void JNICALL Java_com_sonofskywalker3_khdays358_BottomScreenBridge_nativeSetBottomSurface(JNIEnv *env,
                                                                                                    jclass type,
                                                                                                    jobject surface)
{
    ANativeWindow *next = surface != NULL ? ANativeWindow_fromSurface(env, surface) : NULL;
    ANativeWindow *previous;
    (void)type;
    pthread_mutex_lock(&lock);
    previous = window;
    window = next;
    geometry_set = 0;
    lock_failed_logged = 0;
    touch_down = 0;
    mark_shown = 0;
    surface_w = next != NULL ? ANativeWindow_getWidth(next) : 0;
    surface_h = next != NULL ? ANativeWindow_getHeight(next) : 0;
    pthread_mutex_unlock(&lock);
    if (previous != NULL) {
        ANativeWindow_release(previous);
    }
    __android_log_print(ANDROID_LOG_INFO, "khdays", "display: bottom surface %s (%dx%d)",
                        next != NULL ? "attached" : "detached", surface_w, surface_h);
}

JNIEXPORT void JNICALL Java_com_sonofskywalker3_khdays358_BottomScreenBridge_nativeBottomTouch(JNIEnv *env, jclass type,
                                                                                               jfloat x, jfloat y,
                                                                                               jint action)
{
    (void)env;
    (void)type;
    pthread_mutex_lock(&lock);
    touch_down = action != 2; /* 0 down, 1 move, 2 up (BottomScreenBridge.kt) */
    touch_nx = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    touch_ny = y < 0.0f ? 0.0f : y > 1.0f ? 1.0f : y;
    pthread_mutex_unlock(&lock);
}

int khdays_bottom_attached(void)
{
    int attached;
    pthread_mutex_lock(&lock);
    attached = window != NULL;
    pthread_mutex_unlock(&lock);
    return attached;
}

int khdays_bottom_fit_size(int *width, int *height)
{
    int x, y, attached;
    pthread_mutex_lock(&lock);
    attached = window != NULL && surface_w > 0 && surface_h > 0;
    if (attached) {
        fit(surface_w, surface_h, &x, &y, width, height);
    }
    pthread_mutex_unlock(&lock);
    return attached;
}

static void fill(uint8_t *bits, int stride, int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; ++y) {
        uint32_t *row = (uint32_t *)(bits + (size_t)y * (size_t)stride * 4u);
        for (int x = x0; x < x0 + w; ++x) {
            row[x] = OPAQUE_BLACK;
        }
    }
}

static int fill_extended; /* khdays_bottom_set_fill */

void khdays_bottom_set_fill(int extend)
{
    fill_extended = extend != 0;
}

/* The bands around the picture (at x, y, w x h in the buffer) filled: black, or the picture's edge rows
 * copied up and down and its edge columns left and right (the corners from the corner pixels), once
 * the picture is in place. */
static void fill_bands(uint8_t *bits, int stride, int bw, int bh, int x, int y, int w, int h)
{
    if (!fill_extended || w <= 0 || h <= 0) {
        fill(bits, stride, 0, 0, bw, y);
        fill(bits, stride, 0, y + h, bw, bh - (y + h));
        fill(bits, stride, 0, y, x, h);
        fill(bits, stride, x + w, y, bw - (x + w), h);
        return;
    }
    for (int dy = y; dy < y + h; ++dy) { /* the columns beside each picture row */
        uint32_t *row = (uint32_t *)(bits + (size_t)dy * (size_t)stride * 4u);
        const uint32_t left = row[x], right = row[x + w - 1];
        for (int dx = 0; dx < x; ++dx) {
            row[dx] = left;
        }
        for (int dx = x + w; dx < bw; ++dx) {
            row[dx] = right;
        }
    }
    {
        const uint32_t *top = (const uint32_t *)(bits + (size_t)y * (size_t)stride * 4u);
        const uint32_t *bottom = (const uint32_t *)(bits + (size_t)(y + h - 1) * (size_t)stride * 4u);
        for (int dy = 0; dy < y; ++dy) {
            memcpy(bits + (size_t)dy * (size_t)stride * 4u, top, (size_t)bw * 4u);
        }
        for (int dy = y + h; dy < bh; ++dy) {
            memcpy(bits + (size_t)dy * (size_t)stride * 4u, bottom, (size_t)bw * 4u);
        }
    }
}

void khdays_bottom_present(const uint8_t *rgba, int width, int height, int pitch)
{
    ANativeWindow_Buffer buffer;
    int x, y, w, h;
    uint8_t *bits;
    if (rgba == NULL || width <= 0 || height <= 0) {
        return;
    }
    pthread_mutex_lock(&lock);
    if (window == NULL) {
        pthread_mutex_unlock(&lock);
        return;
    }
    mark_shown = 0;
    if (!geometry_set) {
        ANativeWindow_setBuffersGeometry(window, 0, 0, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
        geometry_set = 1;
    }
    if (ANativeWindow_lock(window, &buffer, NULL) != 0) {
        if (!lock_failed_logged) {
            __android_log_print(ANDROID_LOG_WARN, "khdays", "display: the bottom surface cannot be locked");
            lock_failed_logged = 1;
        }
        pthread_mutex_unlock(&lock);
        return;
    }
    surface_w = buffer.width;
    surface_h = buffer.height;
    fit(buffer.width, buffer.height, &x, &y, &w, &h);
    bits = (uint8_t *)buffer.bits;
    if (columns_src != width || columns_dst != w) {
        int *grown = (int *)realloc(columns, sizeof(int) * (size_t)w);
        if (grown == NULL) {
            ANativeWindow_unlockAndPost(window);
            pthread_mutex_unlock(&lock);
            return;
        }
        columns = grown;
        for (int dx = 0; dx < w; ++dx) {
            columns[dx] = dx * width / w < width ? dx * width / w : width - 1;
        }
        columns_src = width;
        columns_dst = w;
    }
    for (int dy = 0; dy < h; ++dy) {
        const int sy = dy * height / h < height ? dy * height / h : height - 1;
        const uint32_t *src = (const uint32_t *)(rgba + (size_t)sy * (size_t)pitch);
        uint32_t *dst = (uint32_t *)(bits + (size_t)(y + dy) * (size_t)buffer.stride * 4u) + x;
        if (width == w) {
            memcpy(dst, src, (size_t)w * 4u);
        } else {
            for (int dx = 0; dx < w; ++dx) {
                dst[dx] = src[columns[dx]];
            }
        }
    }
    fill_bands(bits, buffer.stride, buffer.width, buffer.height, x, y, w, h);
    ANativeWindow_unlockAndPost(window);
    pthread_mutex_unlock(&lock);
}

/* The logo's place in the main menu's layout `text`: of its pieces, the largest whose middle is in the
 * left half (the logo; the characters' picture, from x 934, is the right half's). 0 when there is none. */
static int find_logo(const char *text, char *atlas, size_t size, int *sx, int *sy, int *w, int *h)
{
    int best = 0, px = 0, py = 0, pw = 0, ph = 0, in_piece = 0;
    atlas[0] = '\0';
    for (const char *line = text; *line != '\0';) {
        const char *end = strchr(line, '\n');
        const size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
        char row[256], name[256];
        float x0 = 0, x1 = 0;
        snprintf(row, sizeof(row), "%.*s", (int)(length < sizeof(row) ? length : sizeof(row) - 1), line);
        if (sscanf(row, "atlas %255s", name) == 1) {
            snprintf(atlas, size, "%s", name);
        } else if (sscanf(row, "piece %d %d %d %d", &px, &py, &pw, &ph) == 4) {
            in_piece = 1;
        } else if (in_piece && sscanf(row, "key %*d %f %*f %f", &x0, &x1) == 2) {
            in_piece = 0; /* the piece's first place says which half it is in */
            if (pw > 1 && ph > 1 && (x0 + x1) / 2 < TITLE_LAYOUT_WIDTH / 2 && pw * ph > best) {
                best = pw * ph;
                *sx = px;
                *sy = py;
                *w = pw;
                *h = ph;
            }
        }
        line = end != NULL ? end + 1 : line + length;
    }
    return best > 0 && atlas[0] != '\0';
}

/* The opening's last picture, cut to its logo (what is not black, and a margin): 1 when it is there. */
static int load_frame(void)
{
    int w = 0, h = 0, x0, y0, x1 = -1, y1 = -1, mx, my;
    const char *error = NULL;
    uint8_t *pixels = khdays_png_read(TITLE_FRAME, &w, &h, &error);
    if (pixels == NULL) {
        return 0;
    }
    x0 = w;
    y0 = h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t *p = pixels + ((size_t)y * w + x) * 4;
            if (p[0] > FRAME_DARK || p[1] > FRAME_DARK || p[2] > FRAME_DARK) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    if (x1 < x0 || y1 < y0) {
        khdays_png_free(pixels);
        return 0;
    }
    mx = (int)((float)(x1 - x0 + 1) * FRAME_MARGIN);
    my = (int)((float)(y1 - y0 + 1) * FRAME_MARGIN);
    x0 = x0 - mx > 0 ? x0 - mx : 0;
    y0 = y0 - my > 0 ? y0 - my : 0;
    x1 = x1 + mx < w - 1 ? x1 + mx : w - 1;
    y1 = y1 + my < h - 1 ? y1 + my : h - 1;
    logo_w = x1 - x0 + 1;
    logo_h = y1 - y0 + 1;
    logo = (uint8_t *)malloc((size_t)logo_w * logo_h * 4);
    if (logo != NULL) {
        for (int y = 0; y < logo_h; ++y) {
            memcpy(logo + (size_t)y * logo_w * 4, pixels + ((size_t)(y0 + y) * w + x0) * 4, (size_t)logo_w * 4);
        }
        __android_log_print(ANDROID_LOG_INFO, "khdays", "display: the lower display shows the opening's last picture");
    }
    khdays_png_free(pixels);
    return logo != NULL;
}

/* Reads the logo once (outside the lock: it reads files): the opening's last picture, else the first
 * language's HD menu layout found. */
static void load_logo(void)
{
    static const char *const languages[] = {"en", "fr", "de", "it", "es"};
    logo_looked = 1;
    if (load_frame()) {
        return;
    }
    for (size_t i = 0; i < sizeof(languages) / sizeof(languages[0]) && logo == NULL; ++i) {
        char relative[96], path[1024], atlas[256], *text;
        long length;
        FILE *f;
        int sx = 0, sy = 0, w = 0, h = 0, aw = 0, ah = 0;
        uint8_t *pixels;
        const char *error = NULL;
        snprintf(relative, sizeof(relative), "layouts/title/%s/root_2_0.txt", languages[i]);
        if (!khdays_mods_find(relative, path, (int)sizeof(path)) || (f = fopen(path, "rb")) == NULL) {
            continue;
        }
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        text = length > 0 ? (char *)malloc((size_t)length + 1) : NULL;
        if (text == NULL || fread(text, 1, (size_t)length, f) != (size_t)length) {
            free(text);
            fclose(f);
            continue;
        }
        fclose(f);
        text[length] = '\0';
        if (!find_logo(text, atlas, sizeof(atlas), &sx, &sy, &w, &h)) {
            free(text);
            continue;
        }
        free(text);
        snprintf(relative, sizeof(relative), "layouts/title/%s/%s", languages[i], atlas);
        if (!khdays_mods_find(relative, path, (int)sizeof(path)) ||
            (pixels = khdays_png_read(path, &aw, &ah, &error)) == NULL) {
            continue;
        }
        if (sx >= 0 && sy >= 0 && sx + w <= aw && sy + h <= ah && (logo = (uint8_t *)malloc((size_t)w * h * 4)) != NULL) {
            for (int y = 0; y < h; ++y) {
                memcpy(logo + (size_t)y * w * 4, pixels + ((size_t)(sy + y) * aw + sx) * 4, (size_t)w * 4);
            }
            logo_w = w;
            logo_h = h;
            __android_log_print(ANDROID_LOG_INFO, "khdays", "display: the lower display shows the title's logo (%s)",
                                relative);
        }
        khdays_png_free(pixels);
    }
}

/* Draws the logo, as large as fits, centred over the black, smoothly scaled (bilinear, its alpha over
 * the black). */
static void draw_logo(uint8_t *bits, int stride, int width, int height)
{
    const float scale_w = (float)width * LOGO_WIDTH_SHARE / (float)logo_w;
    const float scale_h = (float)height * LOGO_HEIGHT_SHARE / (float)logo_h;
    const float scale = scale_w < scale_h ? scale_w : scale_h;
    const int w = (int)((float)logo_w * scale), h = (int)((float)logo_h * scale);
    const int x0 = (width - w) / 2, y0 = (height - h) / 2;
    for (int y = 0; y < h; ++y) {
        uint32_t *row = (uint32_t *)(bits + (size_t)(y0 + y) * (size_t)stride * 4u) + x0;
        const float fy = ((float)y + 0.5f) / scale - 0.5f;
        const int iy = fy < 0.0f ? 0 : (int)fy, iy1 = iy + 1 < logo_h ? iy + 1 : iy;
        const float ty = fy < 0.0f ? 0.0f : fy - (float)iy;
        for (int x = 0; x < w; ++x) {
            const float fx = ((float)x + 0.5f) / scale - 0.5f;
            const int ix = fx < 0.0f ? 0 : (int)fx, ix1 = ix + 1 < logo_w ? ix + 1 : ix;
            const float tx = fx < 0.0f ? 0.0f : fx - (float)ix;
            const uint8_t *p00 = logo + ((size_t)iy * logo_w + ix) * 4, *p01 = logo + ((size_t)iy * logo_w + ix1) * 4;
            const uint8_t *p10 = logo + ((size_t)iy1 * logo_w + ix) * 4, *p11 = logo + ((size_t)iy1 * logo_w + ix1) * 4;
            float c[3] = {0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                /* each corner's colour weighted by its alpha (premultiplied), so the clear parts add nothing */
                const uint8_t *q = k == 0 ? p00 : k == 1 ? p01 : k == 2 ? p10 : p11;
                const float wgt = (k & 1 ? tx : 1.0f - tx) * (k & 2 ? ty : 1.0f - ty) * (float)q[3] / 255.0f;
                c[0] += (float)q[0] * wgt;
                c[1] += (float)q[1] * wgt;
                c[2] += (float)q[2] * wgt;
            }
            row[x] = OPAQUE_BLACK | (uint32_t)(c[2] + 0.5f) << 16 | (uint32_t)(c[1] + 0.5f) << 8 | (uint32_t)(c[0] + 0.5f);
        }
    }
}

/* Whether (x, y) in icon units is inside the crown, and how far it is from its outline. */
static int in_crown(float x, float y, float *edge)
{
    int inside = 0;
    float nearest = 1e9f;
    for (int i = 0, j = CROWN_CORNERS - 1; i < CROWN_CORNERS; j = i++) {
        const float ax = crown[j][0], ay = crown[j][1], bx = crown[i][0], by = crown[i][1];
        const float dx = bx - ax, dy = by - ay;
        float t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        {
            const float ex = ax + t * dx - x, ey = ay + t * dy - y, d = ex * ex + ey * ey;
            nearest = d < nearest ? d : nearest;
        }
        if ((ay > y) != (by > y) && x < ax + (y - ay) * dx / dy) {
            inside = !inside;
        }
    }
    *edge = nearest;
    return inside;
}

/* A colour `part` (0-1) of the way from black to `c`. */
static uint32_t toward(uint32_t c, float part)
{
    const uint32_t r = (uint32_t)((float)(c & 255u) * part + 0.5f);
    const uint32_t g = (uint32_t)((float)((c >> 8) & 255u) * part + 0.5f);
    const uint32_t b = (uint32_t)((float)((c >> 16) & 255u) * part + 0.5f);
    return OPAQUE_BLACK | b << 16 | g << 8 | r;
}

void khdays_bottom_present_mark(void)
{
    ANativeWindow_Buffer buffer;
    if (!logo_looked && khdays_bottom_attached()) {
        load_logo();
    }
    pthread_mutex_lock(&lock);
    if (window == NULL || mark_shown) {
        pthread_mutex_unlock(&lock);
        return;
    }
    if (!geometry_set) {
        ANativeWindow_setBuffersGeometry(window, 0, 0, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
        geometry_set = 1;
    }
    if (ANativeWindow_lock(window, &buffer, NULL) != 0) {
        pthread_mutex_unlock(&lock);
        return;
    }
    surface_w = buffer.width;
    surface_h = buffer.height;
    fill((uint8_t *)buffer.bits, buffer.stride, 0, 0, buffer.width, buffer.height);
    if (logo != NULL) {
        draw_logo((uint8_t *)buffer.bits, buffer.stride, buffer.width, buffer.height);
    } else {
        /* the display's pixels an icon unit, the crown centred */
        const float unit = (float)buffer.height * CROWN_SHARE / (CROWN_BOTTOM - CROWN_TOP);
        const float ox = (float)buffer.width / 2.0f - CROWN_MIDDLE * unit;
        const float oy = (float)buffer.height / 2.0f - (CROWN_TOP + CROWN_BOTTOM) / 2.0f * unit;
        const float half = CROWN_OUTLINE / 2.0f, step = 1.0f / (float)CROWN_SAMPLES;
        const int x0 = (int)(ox + 100.0f * unit), x1 = (int)(ox + 332.0f * unit) + 1;
        const int y0 = (int)(oy + 122.0f * unit), y1 = (int)(oy + 316.0f * unit) + 1;
        for (int y = y0 > 0 ? y0 : 0; y < y1 && y < buffer.height; ++y) {
            uint32_t *row = (uint32_t *)((uint8_t *)buffer.bits + (size_t)y * (size_t)buffer.stride * 4u);
            for (int x = x0 > 0 ? x0 : 0; x < x1 && x < buffer.width; ++x) {
                int filled = 0, edged = 0;
                for (int sy = 0; sy < CROWN_SAMPLES; ++sy) {
                    for (int sx = 0; sx < CROWN_SAMPLES; ++sx) {
                        float edge;
                        const float ix = ((float)x + ((float)sx + 0.5f) * step - ox) / unit;
                        const float iy = ((float)y + ((float)sy + 0.5f) * step - oy) / unit;
                        const int inside = in_crown(ix, iy, &edge);
                        if (edge <= half * half) {
                            ++edged;
                        } else if (inside) {
                            ++filled;
                        }
                    }
                }
                if (filled + edged > 0) {
                    /* the samples' colours averaged: the fill's and the outline's over black */
                    const float n = (float)(CROWN_SAMPLES * CROWN_SAMPLES);
                    const uint32_t f = toward(CROWN_FILL, (float)filled / n), e = toward(CROWN_EDGE, (float)edged / n);
                    row[x] = OPAQUE_BLACK | (((f >> 16) & 255u) + ((e >> 16) & 255u)) << 16 |
                             (((f >> 8) & 255u) + ((e >> 8) & 255u)) << 8 | ((f & 255u) + (e & 255u));
                }
            }
        }
    }
    ANativeWindow_unlockAndPost(window);
    mark_shown = 1;
    pthread_mutex_unlock(&lock);
}

int khdays_bottom_touch(int *tx, int *ty)
{
    int x, y, w, h, down;
    pthread_mutex_lock(&lock);
    down = window != NULL && touch_down && surface_w > 0 && surface_h > 0;
    if (down) {
        fit(surface_w, surface_h, &x, &y, &w, &h);
        *tx = (int)((touch_nx * (float)surface_w - (float)x) * DS_W / (float)w);
        *ty = (int)((touch_ny * (float)surface_h - (float)y) * DS_H / (float)h);
        down = *tx >= 0 && *tx < DS_W && *ty >= 0 && *ty < DS_H;
    }
    pthread_mutex_unlock(&lock);
    return down;
}

#else

int khdays_bottom_attached(void)
{
    return 0;
}

void khdays_bottom_present(const uint8_t *rgba, int width, int height, int pitch)
{
    (void)rgba;
    (void)width;
    (void)height;
    (void)pitch;
}

int khdays_bottom_fit_size(int *width, int *height)
{
    (void)width;
    (void)height;
    return 0;
}

void khdays_bottom_set_fill(int extend)
{
    (void)extend;
}

void khdays_bottom_present_mark(void)
{
}

int khdays_bottom_touch(int *x, int *y)
{
    (void)x;
    (void)y;
    return 0;
}

#endif
