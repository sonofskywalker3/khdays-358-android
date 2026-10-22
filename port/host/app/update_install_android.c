/* update_install.h on Android: the APK goes into the game folder's cache/update/ and Android's own
 * installer puts it in place (android/.../Updates.kt, PackageInstaller), asking the player first; the
 * game runs on meanwhile and Android closes it when the update is installed. */
#include "update_install.h"

#include "../android/app_class.h"

#include <SDL3/SDL.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>

#define FOLDER "cache/update/" /* in the game's folder (the working directory), as the title frame's */
#define UPDATES_CLASS "com.sonofskywalker3.khdays358.Updates"

int khdays_update_install_possible(void)
{
    return 1;
}

int khdays_update_install_folder(char *out, size_t size)
{
    if (!SDL_CreateDirectory("cache") || !SDL_CreateDirectory(FOLDER) || strlen(FOLDER) + 1 > size) {
        return 0;
    }
    SDL_strlcpy(out, FOLDER, size);
    return 1;
}

static SDL_EnumerationResult SDLCALL remove_one(void *unused, const char *folder, const char *name)
{
    char path[1024];
    (void)unused;
    SDL_snprintf(path, sizeof(path), "%s%s", folder, name);
    SDL_RemovePath(path);
    return SDL_ENUM_CONTINUE;
}

void khdays_update_install_clean(void)
{
    /* the APK an earlier update installed, or a download given up */
    SDL_EnumerateDirectory(FOLDER, remove_one, NULL);
}

int khdays_update_install_run(const char *file, char *why, size_t size)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass updates = env != NULL ? khdays_app_class(env, UPDATES_CLASS) : NULL;
    jmethodID install =
        updates != NULL ? (*env)->GetStaticMethodID(env, updates, "install", "(Ljava/lang/String;)Ljava/lang/String;") : NULL;
    jstring jfile, refused = NULL;
    int asked = 0;
    if (install == NULL) {
        if (env != NULL && (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        if (updates != NULL) {
            (*env)->DeleteLocalRef(env, updates);
        }
        snprintf(why, size, "Android's installer could not be asked");
        return 0;
    }
    jfile = (*env)->NewStringUTF(env, file);
    refused = (jstring)(*env)->CallStaticObjectMethod(env, updates, install, jfile);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        snprintf(why, size, "Android's installer could not be asked");
    } else if (refused != NULL) {
        /* why it was not: Updates.install's own words */
        const char *text = (*env)->GetStringUTFChars(env, refused, NULL);
        snprintf(why, size, "%s", text != NULL ? text : "Android refused it");
        if (text != NULL) {
            (*env)->ReleaseStringUTFChars(env, refused, text);
        }
        (*env)->DeleteLocalRef(env, refused);
    } else {
        asked = 1;
    }
    (*env)->DeleteLocalRef(env, jfile);
    (*env)->DeleteLocalRef(env, updates);
    return asked;
}
