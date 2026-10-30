/* game_surface.h: the surface's coming and going from the UI thread (GameSurface.kt), the frames from
 * the window's thread; one lock between them. */
#include "game_surface.h"

#include <android/log.h>
#include <jni.h>
#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int ready;
static int went;    /* the surface went since it first came */
static int renewed; /* it came again after going: the swapchain is made again before a frame is drawn */

int khdays_game_surface_frame_begin(void)
{
    pthread_mutex_lock(&lock);
    if (!ready || renewed) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    return 1;
}

void khdays_game_surface_frame_end(void)
{
    pthread_mutex_unlock(&lock);
}

int khdays_game_surface_renew_begin(void)
{
    pthread_mutex_lock(&lock);
    if (!ready || !renewed) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    renewed = 0;
    return 1;
}

void khdays_game_surface_renew_again(void)
{
    pthread_mutex_lock(&lock);
    renewed = 1;
    pthread_mutex_unlock(&lock);
}

/* GameSurface.ready: the surface came (after SDL took it) or is going (before SDL lets it go; a frame
 * being presented ends first). */
JNIEXPORT void JNICALL Java_com_sonofskywalker3_khdays358_GameSurface_nativeReady(JNIEnv *env, jclass type,
                                                                                  jboolean there)
{
    (void)env;
    (void)type;
    pthread_mutex_lock(&lock);
    ready = there == JNI_TRUE;
    if (!ready) {
        went = 1;
    } else if (went) {
        went = 0;
        renewed = 1;
    }
    pthread_mutex_unlock(&lock);
    __android_log_print(ANDROID_LOG_INFO, "khdays", "display: the game's surface %s", there ? "came" : "went");
}
