package com.sonofskywalker3.khdays358

import android.content.Context
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.WindowManager
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLSurface

/**
 * The game's activity. SDLActivity owns the surface, input and the native thread; this subclass names
 * the libraries to load, keeps the screen on, asks the display for its fastest refresh rate, and runs
 * the [BottomScreenHost] that puts the lower DS screen on a second display when there is one.
 */
class MainActivity : SDLActivity() {

    private var bottomScreen: BottomScreenHost? = null

    override fun getLibraries(): Array<String> = arrayOf("SDL3", "main")

    override fun createSDLSurface(context: Context): SDLSurface = FastSurface(context)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (mBrokenLibraries) {
            return  // SDLActivity is showing its error dialog
        }
        // the game's folder on the shared storage (data/, mods/, saves/): made now so it can be filled
        getExternalFilesDir(null)?.let { Log.i(TAG, "game folder: ${it.absolutePath}") }
        preferFastestDisplayMode()
        bottomScreen = BottomScreenHost(this).also { it.start() }
    }

    override fun onDestroy() {
        bottomScreen?.stop()
        bottomScreen = null
        super.onDestroy()
    }

    /** The display mode with the highest refresh rate at the current resolution. */
    private fun preferFastestDisplayMode() {
        val display = (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) display else null)
            ?: @Suppress("DEPRECATION") windowManager.defaultDisplay
            ?: return
        try {
            val current = display.mode
            val fastest = display.supportedModes
                .filter { it.physicalWidth == current.physicalWidth && it.physicalHeight == current.physicalHeight }
                .maxByOrNull { it.refreshRate } ?: return
            if (fastest.refreshRate > current.refreshRate + REFRESH_RATE_EPSILON) {
                window.attributes = window.attributes.apply { preferredDisplayModeId = fastest.modeId }
            }
            Log.i(TAG, "display: ${current.physicalWidth}x${current.physicalHeight}, ${current.refreshRate} Hz now, ${fastest.refreshRate} Hz requested")
        } catch (e: RuntimeException) {
            Log.w(TAG, "display: could not query display modes: $e")
        }
    }

    /** SDL's surface, plus a Surface.setFrameRate request where the API exists. */
    private class FastSurface(context: Context) : SDLSurface(context) {
        override fun surfaceCreated(holder: SurfaceHolder) {
            super.surfaceCreated(holder)
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
                return
            }
            try {
                holder.surface.setFrameRate(FRAME_RATE, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
            } catch (e: IllegalArgumentException) {
                Log.w(TAG, "display: setFrameRate refused: ${e.message}")
            }
        }
    }

    companion object {
        private const val TAG = "khdays"
        private const val FRAME_RATE = 120f
        private const val REFRESH_RATE_EPSILON = 0.5f
    }
}
