/* The app's own Java/Kotlin classes from native code on any thread (Android). */
#ifndef KHDAYS_HOST_ANDROID_APP_CLASS_H
#define KHDAYS_HOST_ANDROID_APP_CLASS_H

#include <jni.h>

/* The class `name` ("com.sonofskywalker3.khdays358.Updates") through the activity's class loader: a
 * native thread's FindClass sees only the system's classes. A local reference, or NULL with the
 * exception cleared when it cannot be had. */
jclass khdays_app_class(JNIEnv *env, const char *name);

#endif
