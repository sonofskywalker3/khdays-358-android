package com.sonofskywalker3.khdays358

import android.content.Context
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.WindowManager
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLSurface
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

/**
 * The game's activity. SDLActivity owns the surface, input and the native thread; this subclass names
 * the libraries to load, keeps the screen on, asks the display for its fastest refresh rate, and runs
 * the [BottomScreenHost] that puts the lower DS screen on a second display when there is one, while the
 * game is in view.
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
        getExternalFilesDir(null)?.let {
            Log.i(TAG, "game folder: ${it.absolutePath}")
            Thread({ saveTitleFrame(it) }, "title frame").start()
        }
        preferFastestDisplayMode()
        bottomScreen = BottomScreenHost(this)
    }

    // The lower screen goes with the game: Home (or another app over it) takes it away, coming back
    // brings it again. Started in onCreate, it stayed lit on the lower display after Home.
    override fun onStart() {
        super.onStart()
        bottomScreen?.start()
    }

    override fun onStop() {
        bottomScreen?.stop()
        super.onStop()
    }

    override fun onDestroy() {
        bottomScreen = null
        super.onDestroy()
    }

    /**
     * The opening movie's last picture (the game's logo on black), kept as a PNG for the lower display
     * to show beside the title (bottom_screen.c). Made once from the player's own HD video, again only
     * when the video is newer; nothing when there is none.
     */
    private fun saveTitleFrame(root: File) {
        val video = File(root, OPENING_VIDEO)
        val out = File(root, TITLE_FRAME)
        if (!video.isFile || (out.isFile && out.lastModified() >= video.lastModified())) {
            return
        }
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(video.path)
            val ms = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: return
            val frame = retriever.getFrameAtTime((ms - LAST_FRAME_BACK_MS) * 1000L, MediaMetadataRetriever.OPTION_CLOSEST)
                ?: return
            out.parentFile?.mkdirs()
            val part = File(out.path + ".part")
            FileOutputStream(part).use { frame.compress(Bitmap.CompressFormat.PNG, 100, it) }
            if (!part.renameTo(out)) {
                throw IOException("${out.name} cannot be put in place")
            }
            Log.i(TAG, "title frame: ${out.path} from ${video.name} at ${ms - LAST_FRAME_BACK_MS} ms")
        } catch (e: RuntimeException) {
            Log.w(TAG, "title frame: not made from ${video.name}: $e")
        } catch (e: IOException) {
            Log.w(TAG, "title frame: not made from ${video.name}: $e")
        } finally {
            retriever.release()
        }
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
        private const val OPENING_VIDEO = "data/cutscenes/cinematics/hd802.mp4"
        const val TITLE_FRAME = "cache/title_frame.png" // read by bottom_screen.c (TITLE_FRAME there)
        private const val LAST_FRAME_BACK_MS = 200L // how far before its end the last picture is taken
    }
}
