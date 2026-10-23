/* game_surface.h: the surface's coming and going from the UI thread (GameSurface.kt), the frames from
 * the window's thread; one lock between them. */
#include "game_surface.h"

#include <android/log.h>
#include <jni.h>
#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int ready;

int khdays_game_surface_frame_begin(void)
{
    pthread_mutex_lock(&lock);
    if (!ready) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    return 1;
}

void khdays_game_surface_frame_end(void)
{
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
    pthread_mutex_unlock(&lock);
    __android_log_print(ANDROID_LOG_INFO, "khdays", "display: the game's surface %s", there ? "came" : "went");
}
