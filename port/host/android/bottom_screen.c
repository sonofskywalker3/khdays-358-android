/* bottom_screen.h on Android: the surface and the touches arrive from the Java side's UI thread (JNI,
 * BottomScreenBridge.kt), the frames from the window's thread; one lock guards what they share. */
#include "bottom_screen.h"

#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

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
static int mark_shown; /* the display shows the crown mark (khdays_bottom_present_mark) */

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

static void fill(uint8_t *bits, int stride, int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; ++y) {
        uint32_t *row = (uint32_t *)(bits + (size_t)y * (size_t)stride * 4u);
        for (int x = x0; x < x0 + w; ++x) {
            row[x] = OPAQUE_BLACK;
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
    fill(bits, buffer.stride, 0, 0, buffer.width, y);
    fill(bits, buffer.stride, 0, y + h, buffer.width, buffer.height - (y + h));
    fill(bits, buffer.stride, 0, y, x, h);
    fill(bits, buffer.stride, x + w, y, buffer.width - (x + w), h);
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
    ANativeWindow_unlockAndPost(window);
    pthread_mutex_unlock(&lock);
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
    {
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
