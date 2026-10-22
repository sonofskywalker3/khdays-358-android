/* https.h on Android: the requests go through the app's Kotlin side (android/.../Updates.kt, Android's
 * own HttpURLConnection and its certificates), called through JNI from whichever thread asks. */
#include "https.h"

#include "../android/app_class.h"

#include <SDL3/SDL.h>
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define UPDATES_CLASS "com.sonofskywalker3.khdays358.Updates"

/* A download's progress function and its context, handed to Kotlin as a number and back. */
typedef struct Progress {
    KhdaysDownloadProgress progress;
    void *context;
} Progress;

char *khdays_https_get(const char *url, const char *agent, int timeout_ms)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass updates;
    jmethodID get;
    jstring jurl, jagent;
    jbyteArray body;
    char *text = NULL;
    if (env == NULL || (updates = khdays_app_class(env, UPDATES_CLASS)) == NULL) {
        return NULL;
    }
    get = (*env)->GetStaticMethodID(env, updates, "get", "(Ljava/lang/String;Ljava/lang/String;I)[B");
    jurl = (*env)->NewStringUTF(env, url);
    jagent = (*env)->NewStringUTF(env, agent);
    body = get != NULL ? (jbyteArray)(*env)->CallStaticObjectMethod(env, updates, get, jurl, jagent, (jint)timeout_ms)
                       : NULL;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        body = NULL;
    }
    if (body != NULL) {
        const jsize n = (*env)->GetArrayLength(env, body);
        text = (char *)malloc((size_t)n + 1);
        if (text != NULL) {
            (*env)->GetByteArrayRegion(env, body, 0, n, (jbyte *)text);
            text[n] = '\0';
        }
        (*env)->DeleteLocalRef(env, body);
    }
    (*env)->DeleteLocalRef(env, jurl);
    (*env)->DeleteLocalRef(env, jagent);
    (*env)->DeleteLocalRef(env, updates);
    return text;
}

int khdays_https_download(const char *url, const char *agent, const char *path, int timeout_ms,
                          KhdaysDownloadProgress progress, void *context)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    Progress told = {progress, context};
    jclass updates;
    jmethodID download;
    jstring jurl, jagent, jpath;
    jboolean ok = JNI_FALSE;
    if (env == NULL || (updates = khdays_app_class(env, UPDATES_CLASS)) == NULL) {
        return 0;
    }
    download = (*env)->GetStaticMethodID(env, updates, "download", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IJ)Z");
    jurl = (*env)->NewStringUTF(env, url);
    jagent = (*env)->NewStringUTF(env, agent);
    jpath = (*env)->NewStringUTF(env, path);
    if (download != NULL) {
        ok = (*env)->CallStaticBooleanMethod(env, updates, download, jurl, jagent, jpath, (jint)timeout_ms,
                                             (jlong)(intptr_t)&told);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ok = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jurl);
    (*env)->DeleteLocalRef(env, jagent);
    (*env)->DeleteLocalRef(env, jpath);
    (*env)->DeleteLocalRef(env, updates);
    if (!ok) {
        SDL_RemovePath(path);
    }
    return ok == JNI_TRUE;
}

/* Updates.download's report of each piece: JNI_TRUE gives the download up. */
JNIEXPORT jboolean JNICALL Java_com_sonofskywalker3_khdays358_Updates_nativeProgress(JNIEnv *env, jclass type,
                                                                                      jlong handle, jlong done)
{
    const Progress *told = (const Progress *)(intptr_t)handle;
    (void)env;
    (void)type;
    return told != NULL && told->progress != NULL && told->progress(told->context, (long long)done) ? JNI_TRUE
                                                                                                     : JNI_FALSE;
}
