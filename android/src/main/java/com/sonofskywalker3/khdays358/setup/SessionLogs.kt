package com.sonofskywalker3.khdays358.setup

import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.Environment
import android.provider.MediaStore
import android.util.Log
import android.widget.Toast
import com.sonofskywalker3.khdays358.R
import java.io.File
import java.io.IOException

/**
 * The game's session logs (saves/logs, always written on Android: runtime/core/session_log.c) where a player can
 * reach them: Android/data is hidden from most file managers, so they are copied to Download/358-2 Days logs
 * (MediaStore: no permission needed), where any file manager or chat app finds them. As the app opens, the last
 * session's log only if that session crashed (its crash line: a game that stops as it starts leaves one to send);
 * the newest [PUBLISHED] whenever the player asks (Settings > General, [copyNewestInBackground]). Old sessions are
 * let go in the app's folder: their log, its recorded input and the save copies beside it (the newest
 * [KEPT_SESSIONS] stay).
 */
object SessionLogs {
    private const val TAG = "khdays"
    private const val PREFIX = "session-"
    private const val LOG_SUFFIX = ".log"
    private const val TEXT_SUFFIX = ".txt"
    private const val KEPT_SESSIONS = 50
    private const val PUBLISHED = 3
    private val DOWNLOAD_FOLDER = Environment.DIRECTORY_DOWNLOADS + "/358-2 Days logs/"
    /** What only a session that crashed writes (runtime/core/diag.c's crash line; a lost graphics card is not one:
     * the game makes its renderer again and goes on). */
    private val CRASH_MARKS = listOf("crash: signal")
    /** Read from a log's end for those: a crash's line is among its last. */
    private const val TAIL_BYTES = 64 * 1024L

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

    /** The player's: the newest logs to Download, then a word on where (or that they could not be). */
    fun copyNewestInBackground(context: Context) {
        val app = context.applicationContext
        Thread({
            var copied = 0
            try {
                val logs = newestLogs(app)
                for (log in logs.take(PUBLISHED)) {
                    if (copyToDownload(app, log)) {
                        ++copied
                    }
                }
            } catch (e: IOException) {
                Log.w(TAG, "session logs: not copied to Download: $e")
            } catch (e: SecurityException) {
                Log.w(TAG, "session logs: not copied to Download: $e")
            } catch (e: IllegalArgumentException) {
                Log.w(TAG, "session logs: not copied to Download: $e")
            }
            val said = if (copied > 0) app.getString(R.string.logs_copied, DOWNLOAD_FOLDER.trimEnd('/'))
            else app.getString(R.string.logs_not_copied)
            Handler(Looper.getMainLooper()).post { Toast.makeText(app, said, Toast.LENGTH_LONG).show() }
        }, "session-logs").start()
    }

    private fun logsFolder(context: Context): File? = context.getExternalFilesDir(null)?.let { File(it, "saves/logs") }

    private fun newestLogs(context: Context): List<File> =
        logsFolder(context)?.listFiles { f -> f.isFile && f.name.startsWith(PREFIX) && f.name.endsWith(LOG_SUFFIX) }
            ?.sortedByDescending { it.name } ?: emptyList()

    /** As the app opens: the last session's log, if it crashed; the old sessions let go. */
    private fun publish(context: Context) {
        val last = newestLogs(context).firstOrNull()
        if (last != null && crashed(last)) {
            try {
                CrashRecord.appendTo(context, last)
            } catch (e: IOException) {
                Log.w(TAG, "session logs: Android's crash record not added: $e")
            } catch (e: SecurityException) {
                Log.w(TAG, "session logs: Android's crash record not added: $e")
            }
            copyToDownload(context, last)
        }
        logsFolder(context)?.let { prune(it) }
    }

    private fun crashed(log: File): Boolean {
        val tail = log.inputStream().use { input ->
            input.skip(maxOf(0L, log.length() - TAIL_BYTES))
            input.readBytes().toString(Charsets.UTF_8)
        }
        return CRASH_MARKS.any { tail.contains(it) }
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

    /** Whether the copy was written. */
    private fun copyToDownload(context: Context, log: File): Boolean {
        // named as Android names a text file it is given (it adds .txt to a .log): found by that name next time
        val name = log.name + TEXT_SUFFIX
        val resolver = context.contentResolver
        val collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI
        val existing: Uri? = resolver.query(
            collection,
            arrayOf(MediaStore.Downloads._ID),
            "${MediaStore.Downloads.DISPLAY_NAME} = ? AND ${MediaStore.Downloads.RELATIVE_PATH} = ?",
            arrayOf(name, DOWNLOAD_FOLDER),
            null,
        )?.use { c -> if (c.moveToFirst()) Uri.withAppendedPath(collection, c.getLong(0).toString()) else null }
        val target = existing ?: resolver.insert(
            collection,
            ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, name)
                put(MediaStore.Downloads.MIME_TYPE, "text/plain")
                put(MediaStore.Downloads.RELATIVE_PATH, DOWNLOAD_FOLDER)
            },
        ) ?: return false
        val out = resolver.openOutputStream(target, "wt") ?: return false
        out.use { log.inputStream().use { input -> input.copyTo(it) } }
        return true
    }
}
