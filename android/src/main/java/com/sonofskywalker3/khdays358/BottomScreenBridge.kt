package com.sonofskywalker3.khdays358

import android.annotation.SuppressLint
import android.util.Log
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * The JNI seam to port/host/android/bottom_screen.c. Whatever hosts the
 * bottom display's SurfaceView (the Presentation or the fallback activity)
 * calls [bind] once; from then on the surface and every touch on it reach
 * native code. The native side parks the surface until the game loop runs,
 * so binding before the loop starts is fine.
 */
object BottomScreenBridge {
    private const val TAG = "khdays"

    /** Touch phases, numbered as port/host/android/bottom_screen.c's TouchAction. */
    private const val TOUCH_DOWN = 0
    private const val TOUCH_MOVE = 1
    private const val TOUCH_UP = 2

    /** Whether native code currently holds a bottom surface. */
    @Volatile
    var hasSurface: Boolean = false
        private set

    @JvmStatic
    external fun nativeSetBottomSurface(surface: Surface?)

    @JvmStatic
    external fun nativeBottomTouch(x: Float, y: Float, action: Int)

    /** Route `view`'s surface and touches to native code for its lifetime. */
    @SuppressLint("ClickableViewAccessibility")
    fun bind(view: SurfaceView) {
        view.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) = setSurface(holder.surface)

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                // Re-handing the same Surface refreshes the native size and
                // buffer geometry after a resize.
                setSurface(holder.surface)
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) = setSurface(null)
        })
        view.setOnTouchListener { v, event -> forwardTouch(v.width, v.height, event) }
    }

    /** Drop the surface explicitly (the host is going away). */
    fun clearSurface() = setSurface(null)

    private fun setSurface(surface: Surface?) {
        hasSurface = surface != null
        try {
            nativeSetBottomSurface(surface)
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "bottom screen: native library not loaded: ${e.message}")
        }
    }

    private fun forwardTouch(width: Int, height: Int, event: MotionEvent): Boolean {
        if (width <= 0 || height <= 0) {
            return false
        }
        val action = when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> TOUCH_DOWN
            MotionEvent.ACTION_MOVE -> TOUCH_MOVE
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL, MotionEvent.ACTION_POINTER_UP -> TOUCH_UP
            else -> return false
        }
        // One finger: the DS has a single touch point. Follow the first.
        val nx = event.getX(0) / width.toFloat()
        val ny = event.getY(0) / height.toFloat()
        try {
            nativeBottomTouch(nx, ny, action)
        } catch (e: UnsatisfiedLinkError) {
            return false
        }
        return true
    }
}
