package com.sonofskywalker3.khdays358

import android.util.Log

/**
 * The JNI seam to port/host/android/game_surface.c: the game presents a frame only while its surface is
 * there. MainActivity's surface says when it came (after SDL took it) and when it is going (before SDL
 * lets it go, waiting for a frame being presented).
 */
object GameSurface {
    private const val TAG = "khdays"

    @JvmStatic
    private external fun nativeReady(there: Boolean)

    fun ready(there: Boolean) {
        try {
            nativeReady(there)
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "display: native library not loaded: ${e.message}")
        }
    }
}
