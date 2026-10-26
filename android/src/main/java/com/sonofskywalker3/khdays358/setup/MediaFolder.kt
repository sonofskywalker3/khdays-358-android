package com.sonofskywalker3.khdays358.setup

import android.Manifest
import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.Settings
import android.util.Log
import org.libsdl.app.SDL
import java.io.File

/**
 * The player's own folder for the mods and the HD videos, so they can live on a memory card or in a
 * folder a file manager reaches easily, rather than in Android/data. Kept as a path in the app's private
 * storage ([KEPT_IN]); port/host/android/media_folder.c reads it as the game starts, and
 * port/runtime/core/folders.c and [GameFolder] lay the folder out the same way. A folder outside the
 * app's own needs the access to all files (Android 11 on) or to the storage (Android 10).
 */
object MediaFolder {
    const val KEPT_IN = "media_folder.txt" // media_folder.c's KEPT_IN
    private const val TAG = "khdays"
    private const val EXTERNAL_STORAGE_AUTHORITY = "com.android.externalstorage.documents"
    private const val PRIMARY_VOLUME = "primary"

    /** The folder chosen, or null for the app's own. */
    fun chosen(context: Context): File? {
        val kept = File(context.filesDir, KEPT_IN)
        val path = if (kept.isFile) kept.readText().trim() else ""
        return if (path.isEmpty()) null else File(path)
    }

    fun keep(context: Context, folder: File) {
        File(context.filesDir, KEPT_IN).writeText(folder.absolutePath + "\n")
        Log.i(TAG, "setup: mods and HD videos from ${folder.absolutePath} from the next start")
    }

    fun forget(context: Context) {
        File(context.filesDir, KEPT_IN).delete()
        Log.i(TAG, "setup: mods and HD videos back in the app's folder from the next start")
    }

    /**
     * A folder Android's picker gave, as a path native code can open: only a folder of the device's own
     * storage or of a memory card (the external storage's documents) has one.
     */
    fun pathOf(tree: Uri): File? {
        if (tree.authority != EXTERNAL_STORAGE_AUTHORITY) {
            return null
        }
        val id = DocumentsContract.getTreeDocumentId(tree)
        val volume = id.substringBefore(':')
        val inside = id.substringAfter(':', "")
        val root = if (volume.equals(PRIMARY_VOLUME, ignoreCase = true)) {
            @Suppress("DEPRECATION") Environment.getExternalStorageDirectory()
        } else {
            File("/storage", volume)
        }
        return if (inside.isEmpty()) root else File(root, inside)
    }

    /** Whether the app may read and write folders outside its own. */
    fun hasAccess(context: Context): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else {
            context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
        }

    /** Asks for that access: Android's settings page for it (11 on), else the permission's own question. */
    fun askAccess(activity: Activity, requestCode: Int) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val page = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:${activity.packageName}"))
            try {
                activity.startActivity(page)
            } catch (e: ActivityNotFoundException) {
                activity.startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) // the list of apps
            }
        } else {
            activity.requestPermissions(
                arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE),
                requestCode,
            )
        }
    }

    /** The setup screen over the game, from its settings menu (media_folder.c). */
    @JvmStatic
    fun openSetup(): Boolean {
        val context = SDL.getContext() ?: return false
        return try {
            context.startActivity(
                Intent(context, SetupActivity::class.java)
                    .putExtra(SetupActivity.EXTRA_SETUP, true)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
            true
        } catch (e: RuntimeException) {
            Log.w(TAG, "setup: not opened from the game: $e")
            false
        }
    }
}
