package com.sonofskywalker3.khdays358.setup

import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import android.util.Log
import java.io.File
import java.io.IOException

/**
 * The game's session logs (saves/logs, always written on Android: runtime/core/session_log.c) where a player can
 * reach them. Android/data is hidden from most file managers, so as the app opens the newest logs are copied to
 * Download/358-2 Days logs (MediaStore: no permission needed), where any file manager or chat app finds them -- a
 * game that stopped as it started included. Old sessions are let go in the app's folder: their log, its recorded
 * input and the save copies beside it (the newest [KEPT_SESSIONS] stay).
 */
object SessionLogs {
    private const val TAG = "khdays"
    private const val PREFIX = "session-"
    private const val LOG_SUFFIX = ".log"
    private const val KEPT_SESSIONS = 50
    private const val PUBLISHED = 3
    private val DOWNLOAD_FOLDER = Environment.DIRECTORY_DOWNLOADS + "/358-2 Days logs/"

    /** On a thread of its own (the screen does not wait), then [after] on the main thread. */
    fun publishThen(activity: Activity, after: () -> Unit) {
        val app = activity.applicationContext
        Thread({
            try {
                publish(app)
            } catch (e: IOException) {
                Log.w(TAG, "session logs: not copied to Download: $e")
            } catch (e: SecurityException) {
                Log.w(TAG, "session logs: not copied to Download: $e")
            } catch (e: IllegalArgumentException) {
                Log.w(TAG, "session logs: not copied to Download: $e")
            } finally {
                activity.runOnUiThread(after) // whatever the copy did: the game starts
            }
        }, "session-logs").start()
    }

    private fun publish(context: Context) {
        val folder = File(context.getExternalFilesDir(null) ?: return, "saves/logs")
        val logs = folder.listFiles { f -> f.isFile && f.name.startsWith(PREFIX) && f.name.endsWith(LOG_SUFFIX) }
            ?.sortedByDescending { it.name } ?: return
        for (log in logs.take(PUBLISHED)) {
            copyToDownload(context, log)
        }
        prune(folder)
    }

    /** The newest [KEPT_SESSIONS] sessions' files kept (session-<date>-<time>.*), the others deleted. */
    private fun prune(folder: File) {
        val entries = folder.listFiles { f -> f.name.startsWith(PREFIX) } ?: return
        val sessions = entries.map { it.name.substringBefore('.') }.distinct().sortedDescending()
        val dropped = sessions.drop(KEPT_SESSIONS).toSet()
        for (entry in entries) {
            if (entry.name.substringBefore('.') in dropped) {
                entry.deleteRecursively()
            }
        }
    }

    private fun copyToDownload(context: Context, log: File) {
        val resolver = context.contentResolver
        val collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI
        val existing: Uri? = resolver.query(
            collection,
            arrayOf(MediaStore.Downloads._ID),
            "${MediaStore.Downloads.DISPLAY_NAME} = ? AND ${MediaStore.Downloads.RELATIVE_PATH} = ?",
            arrayOf(log.name, DOWNLOAD_FOLDER),
            null,
        )?.use { c -> if (c.moveToFirst()) Uri.withAppendedPath(collection, c.getLong(0).toString()) else null }
        val target = existing ?: resolver.insert(
            collection,
            ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, log.name)
                put(MediaStore.Downloads.MIME_TYPE, "text/plain")
                put(MediaStore.Downloads.RELATIVE_PATH, DOWNLOAD_FOLDER)
            },
        ) ?: return
        resolver.openOutputStream(target, "wt")?.use { out -> log.inputStream().use { it.copyTo(out) } }
    }
}
