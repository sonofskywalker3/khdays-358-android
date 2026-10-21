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
static int game_touch = 1; /* the touches are the game's (not while the settings menu shows here) */
static int *columns; /* for each destination column, the source column (nearest) */
static int columns_src, columns_dst;

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
    touch_nx = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    touch_ny = y < 0.0f ? 0.0f : y > 1.0f ? 1.0f : y;
    touch_down = action != 2; /* 0 down, 1 move, 2 up (BottomScreenBridge.kt) */
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

int khdays_bottom_size(int *w, int *h)
{
    int known;
    pthread_mutex_lock(&lock);
    known = window != NULL && surface_w > 0 && surface_h > 0;
    *w = surface_w;
    *h = surface_h;
    pthread_mutex_unlock(&lock);
    return known;
}

void khdays_bottom_set_game_touch(int on)
{
    pthread_mutex_lock(&lock);
    game_touch = on;
    pthread_mutex_unlock(&lock);
}

static void present(const uint8_t *rgba, int width, int height, int pitch, int whole);

void khdays_bottom_present(const uint8_t *rgba, int width, int height, int pitch)
{
    present(rgba, width, height, pitch, 0);
}

void khdays_bottom_present_whole(const uint8_t *rgba, int width, int height, int pitch)
{
    present(rgba, width, height, pitch, 1);
}

static void present(const uint8_t *rgba, int width, int height, int pitch, int whole)
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
    if (whole) {
        x = y = 0;
        w = buffer.width;
        h = buffer.height;
    } else {
        fit(buffer.width, buffer.height, &x, &y, &w, &h);
    }
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

int khdays_bottom_touch(int *tx, int *ty)
{
    int x, y, w, h, down;
    pthread_mutex_lock(&lock);
    down = window != NULL && game_touch && touch_down && surface_w > 0 && surface_h > 0;
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

int khdays_bottom_touch(int *x, int *y)
{
    (void)x;
    (void)y;
    return 0;
}

int khdays_bottom_size(int *w, int *h)
{
    *w = *h = 0;
    return 0;
}

void khdays_bottom_set_game_touch(int on)
{
    (void)on;
}

void khdays_bottom_present_whole(const uint8_t *rgba, int width, int height, int pitch)
{
    (void)rgba;
    (void)width;
    (void)height;
    (void)pitch;
}

#endif
