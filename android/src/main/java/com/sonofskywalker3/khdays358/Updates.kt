package com.sonofskywalker3.khdays358

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.net.Uri
import android.provider.Settings
import android.util.Log
import org.libsdl.app.SDL
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/**
 * The update's Android side, for port/host/net/https_android.c and port/host/app/update_install_android.c:
 * GitHub asked through Android's own HTTPS, the release's APK downloaded, and that APK handed to
 * PackageInstaller, which asks the player to confirm and replaces the app (same signing key: the saves,
 * settings and game files stay). Called from the game's threads, never the UI thread.
 */
object Updates {
    private const val TAG = "khdays"
    private const val CHUNK = 64 * 1024
    private const val HTTP_OK = 200
    private const val APK_NAME = "update.apk"
    private const val API = "https://api.github.com/"

    /** An APK waiting for the player to allow this app to install updates (Settings), installed on return. */
    @Volatile
    private var waiting: File? = null

    @JvmStatic
    external fun nativeProgress(handle: Long, done: Long): Boolean

    private fun open(url: String, agent: String, timeoutMs: Int): HttpURLConnection {
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.connectTimeout = timeoutMs
        connection.readTimeout = timeoutMs
        connection.instanceFollowRedirects = true // GitHub's files answer from another host, over HTTPS too
        connection.setRequestProperty("User-Agent", agent)
        if (url.startsWith(API)) {
            connection.setRequestProperty("Accept", "application/vnd.github+json")
        }
        return connection
    }

    /** The body of `url` when it answers 200 OK, else null. */
    @JvmStatic
    fun get(url: String, agent: String, timeoutMs: Int): ByteArray? {
        var connection: HttpURLConnection? = null
        return try {
            connection = open(url, agent, timeoutMs)
            if (connection.responseCode != HTTP_OK) {
                Log.w(TAG, "updates: $url answered ${connection.responseCode}")
                null
            } else {
                connection.inputStream.use { it.readBytes() }
            }
        } catch (e: IOException) {
            Log.w(TAG, "updates: $url not reached: $e")
            null
        } finally {
            connection?.disconnect()
        }
    }

    /** `url` into the file `path`, each piece told to native code (`handle`), which can give it up. */
    @JvmStatic
    fun download(url: String, agent: String, path: String, timeoutMs: Int, handle: Long): Boolean {
        var connection: HttpURLConnection? = null
        return try {
            connection = open(url, agent, timeoutMs)
            if (connection.responseCode != HTTP_OK) {
                Log.w(TAG, "updates: $url answered ${connection.responseCode}")
                return false
            }
            var done = 0L
            val buffer = ByteArray(CHUNK)
            connection.inputStream.use { input ->
                FileOutputStream(path).use { output ->
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) {
                            break
                        }
                        output.write(buffer, 0, n)
                        done += n
                        if (nativeProgress(handle, done)) {
                            return false // given up
                        }
                    }
                }
            }
            true
        } catch (e: IOException) {
            Log.w(TAG, "updates: $url not downloaded: $e")
            false
        } finally {
            connection?.disconnect()
        }
    }

    /**
     * Hands the APK `path` (relative to the game's folder, or absolute) to Android's installer: null when
     * it was handed over, or when Settings opened for the player to allow installs first (it is handed
     * over on return, [resume]); else why not, in a few words.
     */
    @JvmStatic
    fun install(path: String): String? {
        val context = SDL.getContext() ?: return "the app is not there"
        var file = File(path)
        if (!file.isAbsolute) {
            file = File(context.getExternalFilesDir(null), path)
        }
        if (!file.isFile) {
            return "the downloaded file is gone"
        }
        if (!context.packageManager.canRequestPackageInstalls()) {
            waiting = file
            val settings = Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${context.packageName}"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(settings)
            Log.i(TAG, "updates: asking the player to allow this app to install updates")
            return null
        }
        return commit(context, file)
    }

    /** Back in the game: the APK waiting for the permission is handed over once it was given. */
    fun resume(context: Context) {
        val file = waiting ?: return
        if (!context.packageManager.canRequestPackageInstalls()) {
            Log.i(TAG, "updates: installing updates was not allowed; the update waits for the next offer")
            waiting = null
            return
        }
        waiting = null
        Thread({ commit(context, file)?.let { Log.w(TAG, "updates: not installed: $it") } }, "update install").start()
    }

    private fun commit(context: Context, file: File): String? {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setAppPackageName(context.packageName)
        params.setSize(file.length())
        return try {
            val id = installer.createSession(params)
            installer.openSession(id).use { session ->
                session.openWrite(APK_NAME, 0, file.length()).use { output ->
                    file.inputStream().use { it.copyTo(output, CHUNK) }
                    session.fsync(output)
                }
                val told = Intent(context, InstallResult::class.java)
                val pending = PendingIntent.getBroadcast(
                    context, id, told, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
                )
                session.commit(pending.intentSender)
            }
            Log.i(TAG, "updates: ${file.name} handed to Android's installer")
            null
        } catch (e: IOException) {
            "Android's installer refused it ($e)"
        } catch (e: SecurityException) {
            "Android's installer refused it ($e)"
        }
    }

    /** What the installer says: the player's confirmation to show, or how it ended (logged). */
    class InstallResult : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
                PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                    @Suppress("DEPRECATION")
                    val confirm = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT) ?: return
                    context.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                }
                PackageInstaller.STATUS_SUCCESS -> Log.i(TAG, "updates: installed")
                else -> Log.w(TAG, "updates: not installed ($status): ${intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)}")
            }
        }
    }
}
