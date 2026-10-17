package com.sonofskywalker3.khdays358

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.Display
import android.view.SurfaceView

/**
 * The fallback bottom-screen host: an activity launched on the secondary
 * display with ActivityOptions.setLaunchDisplayId when a Presentation is
 * refused there (Android 13). It only owns a SurfaceView bound to
 * [BottomScreenBridge]; the game thread lives in MainActivity. singleTask,
 * no history and excluded from recents (manifest), so it never outlives the
 * main activity in the task switcher.
 */
class BottomScreenActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        BottomScreenHost.configureBottomWindow(window)
        val view = SurfaceView(this)
        BottomScreenBridge.bind(view)
        setContentView(view)
        current = this
        Log.i(TAG, "bottom screen: activity created on display ${currentDisplayId()}")
    }

    private fun currentDisplayId(): Int =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            display?.displayId ?: Display.INVALID_DISPLAY
        } else {
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.displayId
        }

    override fun onDestroy() {
        if (current === this) {
            current = null
        }
        super.onDestroy()
    }

    @Deprecated("Back on the bottom panel is ignored; the top activity owns navigation")
    override fun onBackPressed() {
        // Leave the bottom screen where it is; the overlay toggles on the
        // top display's back button.
    }

    companion object {
        private const val TAG = "khdays"

        @Volatile
        private var current: BottomScreenActivity? = null

        fun finishCurrent() {
            current?.finish()
            current = null
        }
    }
}
