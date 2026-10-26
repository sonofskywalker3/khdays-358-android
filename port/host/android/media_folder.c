/* media_folder.h: the file kept in the app's private storage, and MediaFolder.kt called through JNI. */
#include "media_folder.h"

#include "app_class.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define MEDIA_FOLDER_CLASS "com.sonofskywalker3.khdays358.setup.MediaFolder"
#define KEPT_IN "media_folder.txt" /* MediaFolder.KEPT_IN */

const char *khdays_media_folder_chosen(void)
{
    /* named whole: SDL takes a relative path as the private storage's, fopen as the game folder's */
    static char folder[1024];
    const char *inside = SDL_GetAndroidInternalStoragePath();
    char path[1024];
    FILE *f;
    folder[0] = '\0';
    if (inside != NULL && snprintf(path, sizeof(path), "%s/%s", inside, KEPT_IN) < (int)sizeof(path) &&
        (f = fopen(path, "r")) != NULL) {
        if (fgets(folder, sizeof(folder), f) == NULL) {
            folder[0] = '\0';
        }
        folder[strcspn(folder, "\r\n")] = '\0';
        fclose(f);
    }
    return folder;
}

int khdays_media_folder_open_setup(void)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass media = env != NULL ? khdays_app_class(env, MEDIA_FOLDER_CLASS) : NULL;
    jmethodID open = media != NULL ? (*env)->GetStaticMethodID(env, media, "openSetup", "()Z") : NULL;
    jboolean opened = JNI_FALSE;
    if (open != NULL) {
        opened = (*env)->CallStaticBooleanMethod(env, media, open);
    }
    if (env != NULL && (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        opened = JNI_FALSE;
    }
    if (media != NULL) {
        (*env)->DeleteLocalRef(env, media);
    }
    fprintf(stderr, "settings: the setup screen %s\n", opened ? "opened" : "could not be opened");
    return opened == JNI_TRUE;
}
