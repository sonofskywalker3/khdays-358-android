package com.sonofskywalker3.khdays358.touch

import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * The JNI seam to port/host/android/touch_bridge.c. The game says what is to be shown ([show], on its own thread),
 * the view says what the fingers hold (the game hears of it through SDL's event queue).
 */
object TouchOverlay {
    private const val TAG = "khdays"

    /** Numbered as port/host/touch/touch_needs.h's KHDAYS_TOUCH_MODE_*. */
    const val MODE_NONE = 0
    const val MODE_FIELD = 1
    const val MODE_MENU = 2
    const val MODE_TALK = 3
    const val MODE_EVENT = 4

    /** Numbered as port/host/touch/touch_layout.h's KHDAYS_TOUCH_*. */
    const val STICK = 0
    const val DPAD = 1
    const val L = 2
    const val R = 3
    const val FACE = 4
    const val LOCK = 5
    const val SELECT = 6
    const val START = 7
    const val GEAR = 8
    const val CONTROLS = 9
    const val DECK_ROWS = 3

    private val main = Handler(Looper.getMainLooper())

    @Volatile
    var view: TouchOverlayView? = null

    @JvmStatic
    external fun nativeHeld(bits: Int, stickHeld: Boolean, stickX: Float, stickY: Float, lockOn: Boolean)

    @JvmStatic
    external fun nativeGear()

    @JvmStatic
    external fun nativeControlBox(control: Int, x0: Float, y0: Float, x1: Float, y1: Float)

    /** What the game wants shown: called by native code. `edit`: 0 play, 1 moving, 2 resizing. */
    @JvmStatic
    fun show(
        mode: Int, edit: Int, style: Int, opacity: Int, vibration: Boolean, gear: Boolean, width: Float, height: Float,
        boxes: FloatArray,
    ) {
        main.post { view?.show(mode, edit, style, opacity, vibration, gear, width, height, boxes) }
    }

    /** The words Select and Start in the game's font: white pixels, for the view to colour. */
    @JvmStatic
    fun labels(select: IntArray, selectWidth: Int, start: IntArray, startWidth: Int, height: Int) {
        try {
            val selectPicture = Bitmap.createBitmap(select, selectWidth, height, Bitmap.Config.ARGB_8888)
            val startPicture = Bitmap.createBitmap(start, startWidth, height, Bitmap.Config.ARGB_8888)
            main.post { view?.labels(selectPicture, startPicture) }
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "controls: labels not made: ${e.message}")
        }
    }
}
