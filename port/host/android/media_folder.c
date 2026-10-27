/* media_folder.h: the files kept in the app's private storage, and MediaFolder.kt called through JNI. */
#include "media_folder.h"

#include "app_class.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define MEDIA_FOLDER_CLASS "com.sonofskywalker3.khdays358.setup.MediaFolder"
#define MODS_KEPT_IN "mods_folder.txt" /* MediaFolder.MODS_KEPT_IN */
#define HD_KEPT_IN "hd_folder.txt"     /* MediaFolder.HD_KEPT_IN */
#define OLD_KEPT_IN "media_folder.txt" /* one folder for both, before 0.1.4 beta 7 (MediaFolder.OLD_KEPT_IN) */

/* The one line of a file in the app's private storage, into `folder`; "" when there is none. */
static void read_line(const char *name, char *folder, size_t size)
{
    /* named whole: SDL takes a relative path as the private storage's, fopen as the game folder's */
    const char *inside = SDL_GetAndroidInternalStoragePath();
    char path[1024];
    FILE *f;
    folder[0] = '\0';
    if (inside != NULL && snprintf(path, sizeof(path), "%s/%s", inside, name) < (int)sizeof(path) &&
        (f = fopen(path, "r")) != NULL) {
        if (fgets(folder, (int)size, f) == NULL) {
            folder[0] = '\0';
        }
        folder[strcspn(folder, "\r\n")] = '\0';
        fclose(f);
    }
}

const char *khdays_media_folder_mods(void)
{
    static char folder[1024];
    read_line(MODS_KEPT_IN, folder, sizeof(folder));
    if (folder[0] == '\0') { /* the one folder of before: its mods/ (MediaFolder.kt moves it over too) */
        read_line(OLD_KEPT_IN, folder, sizeof(folder));
        if (folder[0] != '\0' && strlen(folder) + 5 < sizeof(folder)) {
            strcat(folder, "/mods");
        }
    }
    return folder;
}

const char *khdays_media_folder_hd(void)
{
    static char folder[1024];
    read_line(HD_KEPT_IN, folder, sizeof(folder));
    if (folder[0] == '\0') {
        read_line(OLD_KEPT_IN, folder, sizeof(folder));
    }
    return folder;
}

/* MediaFolder's static method `name` with the signature `signature`, called with `argument` (a Java
 * string, or NULL for none). A boolean's result. */
static int call_boolean(const char *name, const char *signature, const char *argument)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass media = env != NULL ? khdays_app_class(env, MEDIA_FOLDER_CLASS) : NULL;
    jmethodID method = media != NULL ? (*env)->GetStaticMethodID(env, media, name, signature) : NULL;
    jboolean result = JNI_FALSE;
    if (method != NULL) {
        if (argument != NULL) {
            jstring text = (*env)->NewStringUTF(env, argument);
            result = (*env)->CallStaticBooleanMethod(env, media, method, text);
            if (text != NULL) {
                (*env)->DeleteLocalRef(env, text);
            }
        } else {
            result = (*env)->CallStaticBooleanMethod(env, media, method);
        }
    }
    if (env != NULL && (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = JNI_FALSE;
    }
    if (media != NULL) {
        (*env)->DeleteLocalRef(env, media);
    }
    return result == JNI_TRUE;
}

int khdays_media_folder_open_setup(void)
{
    const int opened = call_boolean("openSetup", "()Z", NULL);
    fprintf(stderr, "settings: the setup screen %s\n", opened ? "opened" : "could not be opened");
    return opened;
}

int khdays_media_folder_act(const char *action)
{
    const int opened = call_boolean("act", "(Ljava/lang/String;)Z", action);
    fprintf(stderr, "settings: the setup screen's %s %s\n", action, opened ? "opened" : "could not be opened");
    return opened;
}

int khdays_media_folder_status(char *text, int size)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass media = env != NULL ? khdays_app_class(env, MEDIA_FOLDER_CLASS) : NULL;
    jmethodID method = media != NULL ? (*env)->GetStaticMethodID(env, media, "status", "()Ljava/lang/String;") : NULL;
    jstring result = NULL;
    int ok = 0;
    text[0] = '\0';
    if (method != NULL) {
        result = (jstring)(*env)->CallStaticObjectMethod(env, media, method);
    }
    if (env != NULL && (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = NULL;
    }
    if (result != NULL) {
        const char *utf = (*env)->GetStringUTFChars(env, result, NULL);
        if (utf != NULL) {
            snprintf(text, (size_t)size, "%s", utf);
            (*env)->ReleaseStringUTFChars(env, result, utf);
            ok = 1;
        }
        (*env)->DeleteLocalRef(env, result);
    }
    if (media != NULL) {
        (*env)->DeleteLocalRef(env, media);
    }
    return ok;
}
