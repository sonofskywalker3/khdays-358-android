package com.sonofskywalker3.khdays358

import android.app.Activity
import android.app.ActivityOptions
import android.app.Presentation
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Display
import android.view.SurfaceView
import android.view.Window
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager

/**
 * Finds the device's secondary presentation display (the AYN Thor's lower
 * panel) and keeps a SurfaceView on it, handing the surface to native code
 * through [BottomScreenBridge].
 *
 * Two ways to put a window on that display, tried in order:
 *
 * 1. A [Presentation] (a dialog bound to a display) holding the SurfaceView.
 *    Published Android 14 results (ScummVM PR 7500) use this.
 * 2. A second activity, [BottomScreenActivity], launched on the display with
 *    [ActivityOptions.setLaunchDisplayId]. Android 13 firmware has refused a
 *    Presentation on an internal display (OpenNOW PR 1075), so this is the
 *    fallback when show() throws or the Presentation never gets a surface.
 *
 * The choice is made at runtime by what succeeds; nothing is configured.
 * Display hot-plug (added/removed) re-runs the search or drops the host, and
 * the native side follows the surface coming and going without stopping.
 */
class BottomScreenHost(private val activity: Activity) {

    private enum class Mode { NONE, PRESENTATION, ACTIVITY }

    private val displayManager = activity.getSystemService(DisplayManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private var presentation: BottomScreenPresentation? = null
    private var mode = Mode.NONE
    private var displayId = Display.INVALID_DISPLAY

    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(id: Int) {
            if (mode == Mode.NONE) {
                Log.i(TAG, "bottom screen: display $id added")
                attach()
            }
        }

        override fun onDisplayRemoved(id: Int) {
            if (id == displayId) {
                Log.i(TAG, "bottom screen: display $id removed")
                detach()
            }
        }

        override fun onDisplayChanged(id: Int) {}
    }

    /** The Presentation got a surface in time, or the activity takes over. */
    private val presentationSurfaceCheck = Runnable {
        if (mode == Mode.PRESENTATION && !BottomScreenBridge.hasSurface) {
            Log.w(TAG, "bottom screen: Presentation shown but no surface after ${PRESENTATION_SURFACE_TIMEOUT_MS} ms; trying an activity")
            val display = displayManager.getDisplay(displayId)
            dismissPresentation()
            if (display != null && display.isValid) {
                launchActivity(display)
            } else {
                mode = Mode.NONE
            }
        }
    }

    fun start() {
        displayManager.registerDisplayListener(displayListener, handler)
        attach()
    }

    fun stop() {
        displayManager.unregisterDisplayListener(displayListener)
        detach()
    }

    private fun attach() {
        val display = findBottomDisplay()
        if (display == null) {
            Log.i(TAG, "bottom screen: no secondary presentation display; one-display layout")
            return
        }
        displayId = display.displayId
        val mode = display.mode
        Log.i(TAG, "bottom screen: display ${display.displayId} '${display.name}' ${mode.physicalWidth}x${mode.physicalHeight} @ ${mode.refreshRate} Hz")
        if (showPresentation(display)) {
            this.mode = Mode.PRESENTATION
            handler.postDelayed(presentationSurfaceCheck, PRESENTATION_SURFACE_TIMEOUT_MS)
        } else {
            launchActivity(display)
        }
    }

    private fun detach() {
        handler.removeCallbacks(presentationSurfaceCheck)
        dismissPresentation()
        BottomScreenActivity.finishCurrent()
        BottomScreenBridge.clearSurface()
        mode = Mode.NONE
        displayId = Display.INVALID_DISPLAY
    }

    private fun findBottomDisplay(): Display? {
        val candidates = displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
        val chosen = candidates.firstOrNull { it.displayId != Display.DEFAULT_DISPLAY && it.isValid }
        if (chosen == null) {
            for (display in displayManager.displays) {
                if (display.displayId != Display.DEFAULT_DISPLAY) {
                    Log.i(TAG, "bottom screen: display ${display.displayId} '${display.name}' is not in the presentation category (flags 0x${Integer.toHexString(display.flags)}); ignored")
                }
            }
        }
        return chosen
    }

    private fun showPresentation(display: Display): Boolean {
        return try {
            val p = BottomScreenPresentation(activity, display)
            p.setOnDismissListener {
                if (presentation === p) {
                    Log.i(TAG, "bottom screen: Presentation dismissed")
                    presentation = null
                    if (mode == Mode.PRESENTATION) {
                        mode = Mode.NONE
                    }
                }
            }
            p.show()
            presentation = p
            Log.i(TAG, "bottom screen: Presentation shown on display ${display.displayId}")
            true
        } catch (e: WindowManager.InvalidDisplayException) {
            Log.w(TAG, "bottom screen: Presentation refused on display ${display.displayId}: ${e.message}")
            false
        } catch (e: RuntimeException) {
            // BadTokenException and the rest of WindowManager's refusals.
            Log.w(TAG, "bottom screen: Presentation failed on display ${display.displayId}: $e")
            false
        }
    }

    private fun dismissPresentation() {
        val p = presentation ?: return
        presentation = null
        try {
            p.dismiss()
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "bottom screen: Presentation already gone: ${e.message}")
        }
    }

    private fun launchActivity(display: Display) {
        val options = ActivityOptions.makeBasic().setLaunchDisplayId(display.displayId)
        val intent = Intent(activity, BottomScreenActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        try {
            activity.startActivity(intent, options.toBundle())
            mode = Mode.ACTIVITY
            Log.i(TAG, "bottom screen: BottomScreenActivity launched on display ${display.displayId}")
        } catch (e: ActivityNotFoundException) {
            Log.e(TAG, "bottom screen: BottomScreenActivity missing from the manifest: ${e.message}")
            mode = Mode.NONE
        } catch (e: SecurityException) {
            Log.w(TAG, "bottom screen: not allowed to launch on display ${display.displayId}: ${e.message}")
            mode = Mode.NONE
        }
    }

    /** A full-screen, black, title-less window on the bottom display. */
    private class BottomScreenPresentation(context: Context, display: Display) :
        Presentation(context, display, android.R.style.Theme_Black_NoTitleBar_Fullscreen) {

        override fun onCreate(savedInstanceState: Bundle?) {
            super.onCreate(savedInstanceState)
            requestWindowFeature(Window.FEATURE_NO_TITLE)
            // Back (the pad's B) must not close it, and a touch on it must not take the keys'
            // focus from the game's window: unfocusable, it still gets its touches.
            setCancelable(false)
            window?.let {
                configureBottomWindow(it)
                it.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
            }
            val view = SurfaceView(context)
            BottomScreenBridge.bind(view)
            setContentView(view)
        }
    }

    companion object {
        private const val TAG = "khdays"
        private const val PRESENTATION_SURFACE_TIMEOUT_MS = 2000L

        /** Shared by the Presentation and the fallback activity: black, edge to edge, bars hidden. */
        fun configureBottomWindow(window: Window) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            window.setBackgroundDrawableResource(android.R.color.black)
            window.setLayout(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT
            )
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                // Install the decor first: the Thor's PhoneWindow.getInsetsController
                // dereferences it unchecked and throws before setContentView.
                window.decorView
                window.insetsController?.let {
                    it.hide(WindowInsets.Type.systemBars())
                    it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                }
            } else {
                @Suppress("DEPRECATION")
                window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
            }
        }
    }
}
