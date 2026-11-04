/* touch_bridge.h. Native code's end of android/.../touch/TouchOverlay.kt. What the view says comes on Android's own
 * thread, so it goes through SDL's event queue, in order with the fingers the view hands to SDL. */
#include "touch_bridge.h"

#include "app_class.h"

#include <jni.h>

#define OVERLAY_CLASS "com.sonofskywalker3.khdays358.touch.TouchOverlay"

enum { CODE_HELD, CODE_GEAR, CODE_BOX };

typedef struct PlacedBox {
    int control;
    float x0, y0, x1, y1;
} PlacedBox;

typedef struct HeldState {
    int bits, stick_held, lock_on;
    float stick_x, stick_y;
} HeldState;

static Uint32 event_type(void)
{
    static SDL_AtomicInt type;
    int now = SDL_GetAtomicInt(&type);
    if (now == 0) {
        const Uint32 made = SDL_RegisterEvents(1);
        if (made != 0) {
            SDL_CompareAndSwapAtomicInt(&type, 0, (int)made); /* another thread's may stand: this number goes unused */
        }
        now = SDL_GetAtomicInt(&type);
    }
    return (Uint32)now;
}

static void push(int code, void *data)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = event_type();
    event.user.code = code;
    event.user.data1 = data;
    if (event.type == 0 || !SDL_PushEvent(&event)) {
        SDL_free(data);
    }
}

JNIEXPORT void JNICALL Java_com_sonofskywalker3_khdays358_touch_TouchOverlay_nativeHeld(JNIEnv *env, jclass type, jint bits,
                                                                                       jboolean stick_held, jfloat stick_x,
                                                                                       jfloat stick_y, jboolean lock_on)
{
    HeldState *held = (HeldState *)SDL_malloc(sizeof(HeldState));
    (void)env;
    (void)type;
    if (held != NULL) {
        held->bits = bits;
        held->stick_held = stick_held;
        held->stick_x = stick_x;
        held->stick_y = stick_y;
        held->lock_on = lock_on;
        push(CODE_HELD, held);
    }
}

JNIEXPORT void JNICALL Java_com_sonofskywalker3_khdays358_touch_TouchOverlay_nativeGear(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    push(CODE_GEAR, NULL);
}

JNIEXPORT void JNICALL Java_com_sonofskywalker3_khdays358_touch_TouchOverlay_nativeControlBox(JNIEnv *env, jclass type,
                                                                                              jint control, jfloat x0,
                                                                                              jfloat y0, jfloat x1, jfloat y1)
{
    PlacedBox *box = (PlacedBox *)SDL_malloc(sizeof(PlacedBox));
    (void)env;
    (void)type;
    if (box != NULL) {
        box->control = control;
        box->x0 = x0;
        box->y0 = y0;
        box->x1 = x1;
        box->y1 = y1;
        push(CODE_BOX, box);
    }
}

int khdays_touch_bridge_take(const SDL_Event *event, KhdaysTouchBridgeEvent *out)
{
    const Uint32 type = event_type();
    if (type == 0 || event->type != type) {
        return 0;
    }
    SDL_zerop(out);
    if (event->user.code == CODE_HELD && event->user.data1 != NULL) {
        const HeldState *held = (const HeldState *)event->user.data1;
        out->kind = KHDAYS_TOUCH_BRIDGE_HELD;
        out->bits = (Uint16)held->bits;
        out->stick_held = held->stick_held;
        out->stick_x = held->stick_x;
        out->stick_y = held->stick_y;
        out->lock_on = held->lock_on;
    } else if (event->user.code == CODE_GEAR) {
        out->kind = KHDAYS_TOUCH_BRIDGE_GEAR;
    } else if (event->user.code == CODE_BOX && event->user.data1 != NULL) {
        const PlacedBox *box = (const PlacedBox *)event->user.data1;
        out->kind = KHDAYS_TOUCH_BRIDGE_BOX;
        out->control = box->control;
        out->x0 = box->x0;
        out->y0 = box->y0;
        out->x1 = box->x1;
        out->y1 = box->y1;
    }
    SDL_free(event->user.data1);
    return 1;
}

/* TouchOverlay's class, kept: a native thread finds it only through the activity's class loader. */
static jclass overlay_class(JNIEnv *env)
{
    static jclass kept;
    if (kept == NULL) {
        jclass local = khdays_app_class(env, OVERLAY_CLASS);
        if (local != NULL) {
            kept = (jclass)(*env)->NewGlobalRef(env, local);
            (*env)->DeleteLocalRef(env, local);
        }
    }
    return kept;
}

static void clear_exception(JNIEnv *env)
{
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

void khdays_touch_bridge_show(const KhdaysTouchShown *shown)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass overlay = env != NULL ? overlay_class(env) : NULL;
    jmethodID method = overlay != NULL ? (*env)->GetStaticMethodID(env, overlay, "show", "(IIIIZFF[F)V") : NULL;
    jfloatArray boxes;
    if (method == NULL) {
        if (env != NULL) {
            clear_exception(env);
        }
        return;
    }
    boxes = (*env)->NewFloatArray(env, KHDAYS_TOUCH_SHOWN_FLOATS);
    if (boxes == NULL) {
        clear_exception(env);
        return;
    }
    (*env)->SetFloatArrayRegion(env, boxes, 0, KHDAYS_TOUCH_SHOWN_FLOATS, shown->boxes);
    (*env)->CallStaticVoidMethod(env, overlay, method, shown->mode, shown->edit, shown->style, shown->opacity,
                                 (jboolean)(shown->vibration != 0), shown->w, shown->h, boxes);
    clear_exception(env);
    (*env)->DeleteLocalRef(env, boxes);
}

static jintArray pixels_array(JNIEnv *env, const Uint32 *pixels, int count)
{
    jintArray array = (*env)->NewIntArray(env, count);
    if (array != NULL) {
        (*env)->SetIntArrayRegion(env, array, 0, count, (const jint *)pixels);
    }
    return array;
}

void khdays_touch_bridge_labels(const Uint32 *select, int select_width, const Uint32 *start, int start_width, int height)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass overlay = env != NULL ? overlay_class(env) : NULL;
    jmethodID method = overlay != NULL ? (*env)->GetStaticMethodID(env, overlay, "labels", "([II[III)V") : NULL;
    if (method != NULL) {
        jintArray a = pixels_array(env, select, select_width * height);
        jintArray b = pixels_array(env, start, start_width * height);
        if (a != NULL && b != NULL) {
            (*env)->CallStaticVoidMethod(env, overlay, method, a, select_width, b, start_width, height);
        }
        (*env)->DeleteLocalRef(env, a);
        (*env)->DeleteLocalRef(env, b);
    }
    if (env != NULL) {
        clear_exception(env);
    }
}
