/* host/android/app_class.h. */
#include "app_class.h"

#include <SDL3/SDL.h>

jclass khdays_app_class(JNIEnv *env, const char *name)
{
    jobject activity = (jobject)SDL_GetAndroidActivity();
    jclass found = NULL;
    if (activity != NULL) {
        jclass activity_class = (*env)->GetObjectClass(env, activity);
        jmethodID get_loader = (*env)->GetMethodID(env, activity_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject loader = get_loader != NULL ? (*env)->CallObjectMethod(env, activity, get_loader) : NULL;
        if (loader != NULL) {
            jclass loader_class = (*env)->FindClass(env, "java/lang/ClassLoader");
            jmethodID load = (*env)->GetMethodID(env, loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
            jstring text = (*env)->NewStringUTF(env, name);
            found = (jclass)(*env)->CallObjectMethod(env, loader, load, text);
            (*env)->DeleteLocalRef(env, text);
            (*env)->DeleteLocalRef(env, loader_class);
            (*env)->DeleteLocalRef(env, loader);
        }
        (*env)->DeleteLocalRef(env, activity_class);
        (*env)->DeleteLocalRef(env, activity);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        found = NULL;
    }
    return found;
}
