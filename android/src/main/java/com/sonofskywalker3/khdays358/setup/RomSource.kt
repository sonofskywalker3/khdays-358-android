package com.sonofskywalker3.khdays358.setup

import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.util.concurrent.CancellationException

/**
 * The ROM, read by position. On Android it is the file the player picked: the storage framework hands
 * over a ParcelFileDescriptor, and `FileInputStream(pfd.fileDescriptor).channel` reads it by position
 * ([of]). On the JVM a `RandomAccessFile(...).channel` does the same. Nothing here closes the channel:
 * its owner does.
 */
interface RomSource {
    /** The ROM's size in bytes. */
    val size: Long

    /**
     * Up to [length] bytes at [position] into [buffer] from [offset]: the count read, fewer than
     * [length] only at the end of the ROM (0 past it).
     */
    fun read(position: Long, buffer: ByteArray, offset: Int, length: Int): Int

    companion object {
        /** The ROM behind [channel], read with positional reads (the channel's own position is left alone). */
        fun of(channel: FileChannel): RomSource = FileChannelRomSource(channel)
    }
}

/** [RomSource] over a [FileChannel]. */
private class FileChannelRomSource(private val channel: FileChannel) : RomSource {
    override val size: Long = channel.size()

    override fun read(position: Long, buffer: ByteArray, offset: Int, length: Int): Int {
        val target = ByteBuffer.wrap(buffer, offset, length)
        var at = position
        while (target.hasRemaining()) {
            val count = channel.read(target, at)
            if (count < 0) {
                break
            }
            at += count
        }
        return length - target.remaining()
    }
}

/** [length] bytes at [position]: a [RomFormatException] naming [label] when the ROM ends first. */
internal fun RomSource.readExact(position: Long, length: Long, label: String): ByteArray {
    if (position < 0 || length < 0) {
        throw RomFormatException("$label: negative offset or size.")
    }
    if (length > Int.MAX_VALUE) {
        throw RomFormatException("$label: 0x${length.toString(16).uppercase()} bytes is too large.")
    }
    val bytes = ByteArray(length.toInt())
    val count = read(position, bytes, 0, bytes.size)
    if (count != bytes.size) {
        throw RomFormatException(
            "$label: expected $length bytes at 0x${position.toString(16).uppercase()}, but only $count could be read."
        )
    }
    return bytes
}

/** The ROM is not one this port can set itself up from (the reason is the message). */
class UnsupportedRomException(message: String) : IOException(message)

/** The ROM's own tables (header, FNT, FAT) or a compressed file in it do not make sense. */
class RomFormatException(message: String) : IOException(message)

/** The player cancelled the check or the extraction; whatever was half made is gone. */
class SetupCancelledException : CancellationException("Cancelled.")
