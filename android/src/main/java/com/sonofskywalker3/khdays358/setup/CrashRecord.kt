package com.sonofskywalker3.khdays358.setup

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import java.io.File
import java.io.IOException
import kotlin.math.abs

/**
 * What Android itself recorded of the app's last native crash (its tombstone), added to the end of that session's
 * log: the abort message (what a memory check or an assertion said as it stopped the game), the crashed thread's
 * call chain with each frame's library and offset, and the process's last log lines. The game's own crash line
 * (runtime/core/diag.c) has no abort message and loses its chain where a system library has no frame records.
 * Android 12 and later keep the tombstone in protobuf (debuggerd/proto/tombstone.proto), read here by field number.
 */
object CrashRecord {
    private const val HEADING = "--- Android's record of this crash ---"
    /** Last in a log that has the record: found in its tail however long the record. */
    private const val END = "--- end of Android's record ---"
    /** The exit and the log's last write within this of each other: the same crash. */
    private const val SAME_CRASH_MS = 120_000L
    private const val MOST_FRAMES = 48
    private const val MOST_LOG_LINES = 40

    // tombstone.proto's field numbers
    private const val TOMBSTONE_TID = 6
    private const val TOMBSTONE_SIGNAL = 10
    private const val TOMBSTONE_ABORT_MESSAGE = 14
    private const val TOMBSTONE_CAUSES = 15
    private const val TOMBSTONE_THREADS = 16
    private const val TOMBSTONE_LOG_BUFFERS = 18
    private const val SIGNAL_NUMBER = 1
    private const val SIGNAL_NAME = 2
    private const val SIGNAL_CODE_NAME = 4
    private const val CAUSE_TEXT = 1
    private const val MAP_KEY = 1
    private const val MAP_VALUE = 2
    private const val THREAD_ID = 1
    private const val THREAD_NAME = 2
    private const val THREAD_BACKTRACE = 4
    private const val FRAME_REL_PC = 1
    private const val FRAME_FUNCTION = 4
    private const val FRAME_FUNCTION_OFFSET = 5
    private const val FRAME_FILE = 6
    private const val LOG_BUFFER_LOGS = 2
    private const val LOG_TAG = 5
    private const val LOG_MESSAGE = 6

    /** Adds the record to [log] when Android has one for the crash that ended it (once: a log that has it is left). */
    fun appendTo(context: Context, log: File) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return // the tombstone is protobuf from Android 12 on
        }
        val tail = log.inputStream().use { input ->
            input.skip(maxOf(0L, log.length() - END.length - 64L))
            input.readBytes().toString(Charsets.UTF_8)
        }
        if (tail.contains(END)) {
            return
        }
        val manager = context.getSystemService(ActivityManager::class.java) ?: return
        // every exit Android keeps, newest first: the native crash nearest the log's last write is this one's
        // (a later start that died before its own log does not hide it)
        val written = log.lastModified()
        val exit = manager.getHistoricalProcessExitReasons(context.packageName, 0, 0)
            .filter { it.reason == ApplicationExitInfo.REASON_CRASH_NATIVE }
            .minByOrNull { abs(it.timestamp - written) }
            ?.takeIf { abs(it.timestamp - written) <= SAME_CRASH_MS } ?: return
        val bytes = try {
            exit.traceInputStream?.use { it.readBytes() }
        } catch (e: IOException) {
            null
        } ?: return
        log.appendText("\n$HEADING\n" + describe(bytes) + "$END\n")
    }

    private fun describe(tombstone: ByteArray): String {
        val fields = Proto.parse(tombstone)
        val text = StringBuilder()
        Proto.message(fields, TOMBSTONE_SIGNAL)?.let { signal ->
            text.append("signal ${Proto.number(signal, SIGNAL_NUMBER)} ${Proto.text(signal, SIGNAL_NAME)} ")
                .append("(${Proto.text(signal, SIGNAL_CODE_NAME)})\n")
        }
        Proto.text(fields, TOMBSTONE_ABORT_MESSAGE).takeIf { it.isNotEmpty() }?.let {
            text.append("abort message: $it\n")
        }
        for (cause in Proto.messages(fields, TOMBSTONE_CAUSES)) {
            text.append("cause: ${Proto.text(cause, CAUSE_TEXT)}\n")
        }
        val crashed = Proto.number(fields, TOMBSTONE_TID)
        for (entry in Proto.messages(fields, TOMBSTONE_THREADS)) {
            if (Proto.number(entry, MAP_KEY) != crashed) {
                continue
            }
            val thread = Proto.message(entry, MAP_VALUE) ?: continue
            text.append("thread ${Proto.number(thread, THREAD_ID)} \"${Proto.text(thread, THREAD_NAME)}\":\n")
            Proto.messages(thread, THREAD_BACKTRACE).take(MOST_FRAMES).forEachIndexed { i, frame ->
                val function = Proto.text(frame, FRAME_FUNCTION)
                val named = if (function.isEmpty()) "" else " ($function+${Proto.number(frame, FRAME_FUNCTION_OFFSET)})"
                text.append("  #%02d pc %08x %s%s\n".format(i, Proto.number(frame, FRAME_REL_PC),
                    Proto.text(frame, FRAME_FILE), named))
            }
        }
        val lines = Proto.messages(fields, TOMBSTONE_LOG_BUFFERS)
            .flatMap { Proto.messages(it, LOG_BUFFER_LOGS) }
            .map { "${Proto.text(it, LOG_TAG)}: ${Proto.text(it, LOG_MESSAGE).trimEnd()}" }
        if (lines.isNotEmpty()) {
            text.append("last log lines:\n")
            lines.takeLast(MOST_LOG_LINES).forEach { text.append("  $it\n") }
        }
        return text.toString()
    }

    /** A protobuf message's fields, in order: a varint or fixed number, or a length-delimited run of bytes. */
    private class Field(val number: Int, val value: Long, val bytes: ByteArray?)

    private object Proto {
        private const val VARINT = 0
        private const val FIXED64 = 1
        private const val LENGTH = 2
        private const val FIXED32 = 5

        /** The fields up to the first it cannot read (a truncated or unknown wire type): those before are kept. */
        fun parse(b: ByteArray): List<Field> {
            val fields = ArrayList<Field>()
            var i = 0
            fun varint(): Long? {
                var result = 0L
                var shift = 0
                while (i < b.size && shift < 64) {
                    val byte = b[i++].toInt()
                    result = result or ((byte and 0x7f).toLong() shl shift)
                    if (byte and 0x80 == 0) {
                        return result
                    }
                    shift += 7
                }
                return null
            }
            while (i < b.size) {
                val key = varint() ?: break
                val number = (key ushr 3).toInt()
                when ((key and 7).toInt()) {
                    VARINT -> fields.add(Field(number, varint() ?: break, null))
                    FIXED64 -> {
                        if (i + 8 > b.size) break
                        var v = 0L
                        for (k in 0 until 8) v = v or ((b[i + k].toLong() and 0xff) shl (8 * k))
                        i += 8
                        fields.add(Field(number, v, null))
                    }
                    LENGTH -> {
                        val length = varint() ?: break
                        if (length < 0 || length > b.size - i) break
                        fields.add(Field(number, 0, b.copyOfRange(i, i + length.toInt())))
                        i += length.toInt()
                    }
                    FIXED32 -> {
                        if (i + 4 > b.size) break
                        var v = 0L
                        for (k in 0 until 4) v = v or ((b[i + k].toLong() and 0xff) shl (8 * k))
                        i += 4
                        fields.add(Field(number, v, null))
                    }
                    else -> break
                }
            }
            return fields
        }

        fun number(fields: List<Field>, n: Int): Long = fields.lastOrNull { it.number == n && it.bytes == null }?.value ?: 0
        fun text(fields: List<Field>, n: Int): String =
            fields.lastOrNull { it.number == n && it.bytes != null }?.bytes?.toString(Charsets.UTF_8) ?: ""
        fun message(fields: List<Field>, n: Int): List<Field>? =
            fields.lastOrNull { it.number == n && it.bytes != null }?.bytes?.let { parse(it) }
        fun messages(fields: List<Field>, n: Int): List<List<Field>> =
            fields.filter { it.number == n && it.bytes != null }.map { parse(it.bytes!!) }
    }
}
