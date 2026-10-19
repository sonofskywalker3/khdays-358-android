package com.sonofskywalker3.khdays358.setup

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/** LZ10 and LZ11 streams written by hand, each block kind and each failure the manifest records. */
class NitroLzTest {

    private fun bytes(vararg values: Any): ByteArray = values.flatMap { value ->
        when (value) {
            is Int -> listOf(value.toByte())
            is String -> value.toByteArray(Charsets.US_ASCII).toList()
            else -> error("bad test byte $value")
        }
    }.toByteArray()

    private fun ascii(text: String) = text.toByteArray(Charsets.US_ASCII)

    @Test
    fun lz10Literals() {
        val result = NitroLz.decompress(bytes(0x10, 5, 0, 0, 0x00, "abcde"))
        assertEquals("LZ10", result.compression)
        assertArrayEquals(ascii("abcde"), result.data)
    }

    @Test
    fun lz10OverlappingBackReference() {
        // 'a', then 7 bytes from 1 back: a run
        val data = bytes(0x10, 8, 0, 0, 0x40, "a", 0x40, 0x00)
        assertArrayEquals(ascii("aaaaaaaa"), NitroLz.decompressLz10(data))
    }

    @Test
    fun lz10BackReferenceStopsAtTheOutputSize() {
        // "ab", then 18 bytes from 2 back, cut to the 6 the header asks for
        val data = bytes(0x10, 6, 0, 0, 0x20, "ab", 0xF0, 0x01)
        assertArrayEquals(ascii("ababab"), NitroLz.decompressLz10(data))
    }

    @Test
    fun lz10ExtendedSizeReadsItsBlocksFromByteFour() {
        // as the PC tool: LZ10's blocks start at byte 4 even after a 32-bit size
        val data = bytes(0x10, 0, 0, 0, 3, 0, 0, 0)
        assertArrayEquals(byteArrayOf(0, 0, 0), NitroLz.decompressLz10(data))
    }

    @Test
    fun lz10Failures() {
        assertFailure("Invalid LZ10 displacement 6 at output offset 0.", bytes(0x10, 4, 0, 0, 0x80, 0x00, 0x05))
        assertFailure("LZ10 literal byte is truncated.", bytes(0x10, 4, 0, 0, 0x00, "a"))
        assertFailure("LZ10 back-reference is truncated.", bytes(0x10, 4, 0, 0, 0x40, "a", 0x10))
        assertFailure("LZ10 stream ended before the output was complete.", bytes(0x10, 9, 0, 0, 0x00, "abcdefgh"))
        assertFailure("Compressed stream is smaller than the 4-byte Nitro header.", bytes(0x10, 1))
        assertFailure("Extended-size Nitro stream is truncated.", bytes(0x10, 0, 0, 0, 1))
        assertFailure("Decompressed size is zero.", bytes(0x10, 0, 0, 0, 0, 0, 0, 0))
        assertFailure("Refusing to allocate 536870913 bytes; maximum is 536870912.", bytes(0x10, 0, 0, 0, 1, 0, 0, 0x20))
    }

    @Test
    fun lz11ShortBackReference() {
        val result = NitroLz.decompress(bytes(0x11, 6, 0, 0, 0x20, "ab", 0x30, 0x01))
        assertEquals("LZ11", result.compression)
        assertArrayEquals(ascii("ababab"), result.data)
    }

    @Test
    fun lz11LongBackReference() {
        // high nibble 0: length 0x11 + 3 = 20
        val data = bytes(0x11, 21, 0, 0, 0x40, "x", 0x00, 0x30, 0x00)
        assertArrayEquals(ascii("x".repeat(21)), NitroLz.decompressLz11(data))
    }

    @Test
    fun lz11VeryLongBackReference() {
        // high nibble 1: length 0x111 + 0x10 = 289
        val data = bytes(0x11, 0x22, 0x01, 0, 0x40, "y", 0x10, 0x01, 0x00, 0x00)
        assertArrayEquals(ascii("y".repeat(290)), NitroLz.decompressLz11(data))
    }

    @Test
    fun lz11ExtendedSizeReadsItsBlocksFromByteEight() {
        val data = bytes(0x11, 0, 0, 0, 2, 0, 0, 0, 0x00, "pq")
        assertArrayEquals(ascii("pq"), NitroLz.decompressLz11(data))
    }

    @Test
    fun lz11Failures() {
        assertFailure("Invalid LZ11 displacement 2 at output offset 1.", bytes(0x11, 4, 0, 0, 0x40, "a", 0x30, 0x01))
        assertFailure("LZ11 long back-reference is truncated.", bytes(0x11, 30, 0, 0, 0x40, "a", 0x00, 0x30))
        assertFailure("LZ11 very-long back-reference is truncated.", bytes(0x11, 30, 0, 0, 0x40, "a", 0x10, 0x01, 0x00))
        assertFailure("LZ11 short back-reference is truncated.", bytes(0x11, 30, 0, 0, 0x40, "a", 0x30))
    }

    @Test
    fun otherTypesAreRefused() {
        assertFailure("Unsupported compression type 0x30.", bytes(0x30, 1, 0, 0))
        assertFailure("Empty input.", ByteArray(0))
    }

    @Test
    fun onlyTheGivenLengthIsRead() {
        val data = bytes(0x10, 4, 0, 0, 0x00, "abcd", 0x00, "junk")
        assertArrayEquals(ascii("abcd"), NitroLz.decompress(data, 9).data)
        assertFailure("LZ10 literal byte is truncated.", data, 7)
    }

    private fun assertFailure(message: String, data: ByteArray, length: Int = data.size) {
        val e = assertThrows(RomFormatException::class.java) { NitroLz.decompress(data, length) }
        assertEquals(message, e.message)
    }
}
