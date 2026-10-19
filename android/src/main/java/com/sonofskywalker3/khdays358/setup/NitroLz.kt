package com.sonofskywalker3.khdays358.setup

/**
 * The DS BIOS's LZ77 streams, LZ10 and LZ11 (tools/decompress_data.py). A stream starts with its type
 * byte and a 24-bit little-endian output size (0 there: a 32-bit size follows), then groups of eight
 * blocks under a flag byte, highest bit first: 0 a literal byte, 1 a back-reference into the output.
 * Malformed streams fail with the same [RomFormatException] messages as the Python tool, which the
 * decompressed manifest records.
 */
object NitroLz {
    const val LZ10 = 0x10
    const val LZ11 = 0x11

    /** The largest output believed; a bigger size field is not a real stream. */
    const val MAX_OUTPUT_SIZE = 512 * 1024 * 1024

    private const val SIZE_FIELD_END = 4
    private const val EXTENDED_SIZE_END = 8
    private const val LZ10_MIN_LENGTH = 3
    private const val LZ11_LONG = 0 // high nibble of an 8-bit length
    private const val LZ11_VERY_LONG = 1 // high nibble of a 16-bit length
    private const val LZ11_LONG_BIAS = 0x11
    private const val LZ11_VERY_LONG_BIAS = 0x111
    private const val FLAG_BITS = 8

    /** A decompressed stream: its kind ("LZ10" or "LZ11") and bytes. */
    class Decompressed(val compression: String, val data: ByteArray)

    /** Whether a file starting with [firstByte] is taken as LZ-compressed. */
    fun isCompressed(firstByte: Int): Boolean = firstByte == LZ10 || firstByte == LZ11

    /** The first [length] bytes of [data] decompressed as the stream type its first byte names. */
    fun decompress(data: ByteArray, length: Int = data.size): Decompressed {
        if (length == 0) throw RomFormatException("Empty input.")
        return when (val type = data[0].toInt() and 0xFF) {
            LZ10 -> Decompressed("LZ10", decompressLz10(data, length))
            LZ11 -> Decompressed("LZ11", decompressLz11(data, length))
            else -> throw RomFormatException("Unsupported compression type 0x${type.toString(16).uppercase().padStart(2, '0')}.")
        }
    }

    private fun expectedSize(data: ByteArray, length: Int): Int {
        if (length < SIZE_FIELD_END) throw RomFormatException("Compressed stream is smaller than the 4-byte Nitro header.")
        var size = u24(data, 1).toLong()
        if (size == 0L) {
            if (length < EXTENDED_SIZE_END) throw RomFormatException("Extended-size Nitro stream is truncated.")
            size = u32(data, SIZE_FIELD_END)
        }
        if (size <= 0L) throw RomFormatException("Decompressed size is zero.")
        if (size > MAX_OUTPUT_SIZE) {
            throw RomFormatException("Refusing to allocate $size bytes; maximum is $MAX_OUTPUT_SIZE.")
        }
        return size.toInt()
    }

    fun decompressLz10(data: ByteArray, length: Int = data.size): ByteArray {
        if (length == 0 || (data[0].toInt() and 0xFF) != LZ10) throw RomFormatException("Not a Nintendo DS LZ10 stream.")
        val size = expectedSize(data, length)
        val out = ByteArray(size)
        var o = 0
        var c = SIZE_FIELD_END
        while (o < size) {
            if (c >= length) throw RomFormatException("LZ10 stream ended before the output was complete.")
            val flags = data[c++].toInt() and 0xFF
            var bit = FLAG_BITS - 1
            while (bit >= 0 && o < size) {
                if ((flags ushr bit) and 1 == 0) {
                    if (c >= length) throw RomFormatException("LZ10 literal byte is truncated.")
                    out[o++] = data[c++]
                } else {
                    if (c + 1 >= length) throw RomFormatException("LZ10 back-reference is truncated.")
                    val first = data[c].toInt() and 0xFF
                    val second = data[c + 1].toInt() and 0xFF
                    c += 2
                    val count = (first ushr 4) + LZ10_MIN_LENGTH
                    val displacement = (((first and 0x0F) shl 8) or second) + 1
                    o = copyBack(out, o, size, displacement, count, "LZ10")
                }
                bit--
            }
        }
        return out
    }

    fun decompressLz11(data: ByteArray, length: Int = data.size): ByteArray {
        if (length == 0 || (data[0].toInt() and 0xFF) != LZ11) throw RomFormatException("Not a Nintendo DS LZ11 stream.")
        val size = expectedSize(data, length)
        val out = ByteArray(size)
        var o = 0
        var c = if (u24(data, 1) != 0) SIZE_FIELD_END else EXTENDED_SIZE_END
        while (o < size) {
            if (c >= length) throw RomFormatException("LZ11 stream ended before the output was complete.")
            val flags = data[c++].toInt() and 0xFF
            var bit = FLAG_BITS - 1
            while (bit >= 0 && o < size) {
                if ((flags ushr bit) and 1 == 0) {
                    if (c >= length) throw RomFormatException("LZ11 literal byte is truncated.")
                    out[o++] = data[c++]
                } else {
                    if (c >= length) throw RomFormatException("LZ11 back-reference is truncated.")
                    val first = data[c].toInt() and 0xFF
                    val count: Int
                    val displacement: Int
                    when (first ushr 4) {
                        LZ11_LONG -> {
                            if (c + 2 >= length) throw RomFormatException("LZ11 long back-reference is truncated.")
                            val second = data[c + 1].toInt() and 0xFF
                            val third = data[c + 2].toInt() and 0xFF
                            count = (((first and 0x0F) shl 4) or (second ushr 4)) + LZ11_LONG_BIAS
                            displacement = (((second and 0x0F) shl 8) or third) + 1
                            c += 3
                        }
                        LZ11_VERY_LONG -> {
                            if (c + 3 >= length) throw RomFormatException("LZ11 very-long back-reference is truncated.")
                            val second = data[c + 1].toInt() and 0xFF
                            val third = data[c + 2].toInt() and 0xFF
                            val fourth = data[c + 3].toInt() and 0xFF
                            count = (((first and 0x0F) shl 12) or (second shl 4) or (third ushr 4)) + LZ11_VERY_LONG_BIAS
                            displacement = (((third and 0x0F) shl 8) or fourth) + 1
                            c += 4
                        }
                        else -> {
                            if (c + 1 >= length) throw RomFormatException("LZ11 short back-reference is truncated.")
                            val second = data[c + 1].toInt() and 0xFF
                            count = (first ushr 4) + 1
                            displacement = (((first and 0x0F) shl 8) or second) + 1
                            c += 2
                        }
                    }
                    o = copyBack(out, o, size, displacement, count, "LZ11")
                }
                bit--
            }
        }
        return out
    }

    /** [count] bytes from [displacement] back, byte by byte (a copy may overlap itself), stopping at [size]: the new end. */
    private fun copyBack(out: ByteArray, at: Int, size: Int, displacement: Int, count: Int, kind: String): Int {
        if (displacement > at) throw RomFormatException("Invalid $kind displacement $displacement at output offset $at.")
        val end = minOf(at + count, size)
        var o = at
        while (o < end) {
            out[o] = out[o - displacement]
            o++
        }
        return end
    }

    private fun u24(bytes: ByteArray, at: Int): Int =
        u16(bytes, at) or ((bytes[at + 2].toInt() and 0xFF) shl 16)
}
