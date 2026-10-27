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
 * The player's own folders for the mods and for the HD files, so they can live on a memory card or in a
 * folder a file manager reaches easily, rather than in Android/data. Each is kept as a path in the app's
 * private storage ([MODS_KEPT_IN], [HD_KEPT_IN]); port/host/android/media_folder.c reads them as the game
 * starts, and port/runtime/core/folders.c and [GameFolder] read the folders the same way: the mods
 * folder is the one that holds a folder per mod, the HD folder holds data/cutscenes (the PC installer's
 * folder copied whole) or cutscenes. A folder outside the app's own needs the access to all files
 * (Android 11 on) or to the storage (Android 10).
 *
 * The game's settings menu asks here for what the setup screen shows ([status]) and for its actions
 * ([act]), through JNI.
 */
object MediaFolder {
    const val MODS_KEPT_IN = "mods_folder.txt" // media_folder.c's MODS_KEPT_IN
    const val HD_KEPT_IN = "hd_folder.txt" // media_folder.c's HD_KEPT_IN
    const val OLD_KEPT_IN = "media_folder.txt" // one folder for both, before 0.1.4 beta 7
    private const val PENDING_IN = "media_folder.pending" // chosen, waiting for the access to all files
    private const val TAG = "khdays"
    private const val EXTERNAL_STORAGE_AUTHORITY = "com.android.externalstorage.documents"
    private const val PRIMARY_VOLUME = "primary"

    /** Which folder a choice is for. */
    enum class Kind(val keptIn: String) {
        MODS(MODS_KEPT_IN),
        HD(HD_KEPT_IN),
    }

    /** The folder chosen for `kind`, or null for the app's own. */
    fun chosen(context: Context, kind: Kind): File? {
        migrate(context)
        val kept = File(context.filesDir, kind.keptIn)
        val path = if (kept.isFile) kept.readText().trim() else ""
        return if (path.isEmpty()) null else File(path)
    }

    fun keep(context: Context, kind: Kind, folder: File) {
        File(context.filesDir, kind.keptIn).writeText(folder.absolutePath + "\n")
        Log.i(TAG, "setup: ${kind.name.lowercase()} from ${folder.absolutePath} from the next start")
    }

    fun forget(context: Context, kind: Kind) {
        File(context.filesDir, kind.keptIn).delete()
        Log.i(TAG, "setup: ${kind.name.lowercase()} back in the app's folder from the next start")
    }

    /**
     * The one folder of before 0.1.4 beta 7 (its mods/ the mods, its data/cutscenes or cutscenes the
     * HD videos), turned into the two: once, the old file taken away.
     */
    private fun migrate(context: Context) {
        val old = File(context.filesDir, OLD_KEPT_IN)
        if (!old.isFile) {
            return
        }
        val path = old.readText().trim()
        if (path.isNotEmpty()) {
            val folder = File(path)
            if (!File(context.filesDir, HD_KEPT_IN).isFile) {
                keep(context, Kind.HD, folder)
            }
            if (!File(context.filesDir, MODS_KEPT_IN).isFile) {
                keep(context, Kind.MODS, File(folder, "mods"))
            }
        }
        old.delete()
    }

    /**
     * A folder chosen while Android's page for the access to all files is open: kept in a file, since
     * the app may be closed meanwhile (the player turns to another app, Android ends it).
     */
    fun keepPending(context: Context, kind: Kind, folder: File) {
        File(context.filesDir, PENDING_IN).writeText(kind.name + "\n" + folder.absolutePath + "\n")
    }

    fun hasPending(context: Context): Boolean = File(context.filesDir, PENDING_IN).isFile

    /** The folder waiting for the access, with its kind, once (null when none). */
    fun takePending(context: Context): Pair<Kind, File>? {
        val pending = File(context.filesDir, PENDING_IN)
        val lines = if (pending.isFile) pending.readLines().map { it.trim() } else emptyList()
        pending.delete()
        if (lines.size < 2 || lines[1].isEmpty()) {
            return null
        }
        val kind = Kind.values().firstOrNull { it.name == lines[0] } ?: return null
        return kind to File(lines[1])
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

    /** The game's folder with the chosen folders, as the setup screen and the title frame read it. */
    fun gameFolder(context: Context): GameFolder =
        GameFolder(context.getExternalFilesDir(null) ?: context.filesDir, chosen(context, Kind.HD), chosen(context, Kind.MODS))

    /** The setup screen over the game, from its settings menu (media_folder.c). */
    @JvmStatic
    fun openSetup(): Boolean = open(null)

    /**
     * The setup screen on one of its actions, from the settings menu (media_folder.c): it does the one
     * thing and comes back to the game ([SetupActivity.EXTRA_ACTION]).
     */
    @JvmStatic
    fun act(action: String): Boolean = open(action)

    private fun open(action: String?): Boolean {
        val context = SDL.getContext() ?: return false
        return try {
            context.startActivity(
                Intent(context, SetupActivity::class.java)
                    .putExtra(SetupActivity.EXTRA_SETUP, true)
                    .putExtra(SetupActivity.EXTRA_ACTION, action)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
            true
        } catch (e: RuntimeException) {
            Log.w(TAG, "setup: not opened from the game: $e")
            false
        }
    }

    /**
     * What the setup screen says of the game's files, for the settings menu (media_folder.c): a line
     * each, `game=`, `hd=`, `hd_folder=`, `mods_folder=` (the paths chosen, empty for the app's own) and
     * `mods=` (the mods found, each on or off).
     */
    @JvmStatic
    fun status(): String {
        val context = SDL.getContext() ?: return ""
        return try {
            val game = gameFolder(context)
            val supported = context.assets.open(SupportedRoms.ASSET_NAME).use { SupportedRoms.load(it) }
            SetupStatus.of(context, game, supported).lines()
        } catch (e: RuntimeException) {
            Log.w(TAG, "setup: no status for the game: $e")
            ""
        }
    }
}
