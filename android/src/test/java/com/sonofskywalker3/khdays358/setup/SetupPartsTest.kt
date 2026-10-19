package com.sonofskywalker3.khdays358.setup

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.RandomAccessFile
import kotlin.random.Random

/** The setup's smaller pieces, and ROMs it must refuse, without needing a real ROM. */
class SetupPartsTest {

    @get:Rule
    val temp = TemporaryFolder()

    private val supportedRoms by lazy { SetupTestFiles.supportedRoms() }

    @Test
    fun supportedListHasBothReleases() {
        val byCode = supportedRoms.associateBy { it.gameCode }
        assertEquals("358/2 DAYS (Europe)", byCode.getValue("YKGP").name)
        assertEquals("358/2 DAYS (USA)", byCode.getValue("YKGE").name)
        assertEquals("3cbbe3f15b49fc26f43edaf0050899b3cc1091a25e7a496373409c7c8c6fd0cb", byCode.getValue("YKGE").sha256)
    }

    @Test
    fun jsonWritesAsPythonDoes() {
        val value = linkedMapOf(
            "a" to 1L,
            "b" to listOf("x\"\\\n", true, null),
            "c" to emptyMap<String, Any?>(),
            "d" to emptyList<Any?>(),
            "e" to "\u0001\u00e9",
        )
        val expected = "{\n  \"a\": 1,\n  \"b\": [\n    \"x\\\"\\\\\\n\",\n    true,\n    null\n  ],\n" +
            "  \"c\": {},\n  \"d\": [],\n  \"e\": \"\\u0001\u00e9\"\n}"
        assertEquals(expected, Json.write(value))
        assertEquals(value, Json.parse(expected))
    }

    @Test
    fun decompressedPaths() {
        assertEquals("ev/foo.bin", RomExtractor.decompressedPath("nitrofs/ev/foo.bin.z"))
        assertEquals("ev/foo", RomExtractor.decompressedPath("nitrofs/ev/foo.Z"))
        assertEquals("_unnamed/file_00003.bin.decompressed", RomExtractor.decompressedPath("nitrofs/_unnamed/file_00003.bin"))
        assertEquals("a.z/b.decompressed", RomExtractor.decompressedPath("nitrofs/a.z/b"))
    }

    @Test
    fun safeComponents() {
        assertEquals("a_b_c", NitroFs.safeComponent("a/b\\c", "x"))
        assertEquals("name", NitroFs.safeComponent(" ..name.. ", "x"))
        assertEquals("fallback", NitroFs.safeComponent("..", "fallback"))
    }

    @Test
    fun tinyFileIsNoRom() {
        val result = RomCheck.identify(sourceOf(ByteArray(100)), supportedRoms)
        result as RomCheck.Result.Unsupported
        assertEquals(RomCheck.Reason.NOT_A_DS_ROM, result.reason)
        assertNull(result.info)
        assertTrue(result.message, result.message.contains("too small"))
    }

    @Test
    fun randomFileIsRefused() {
        val result = RomCheck.identify(sourceOf(Random(358).nextBytes(1024 * 1024)), supportedRoms)
        result as RomCheck.Result.Unsupported
        assertEquals(RomCheck.Reason.OTHER_GAME, result.reason)
        assertTrue(result.message, result.message.startsWith("This file is not 358/2 Days."))
    }

    @Test
    fun extractingAnUnsupportedRomLeavesNothing() {
        val out = temp.newFolder("extracted")
        val e = assertThrows(UnsupportedRomException::class.java) {
            RomExtractor.extract(sourceOf(Random(1).nextBytes(64 * 1024)), out, supportedRoms, "random.nds")
        }
        assertTrue(e.message!!.contains("not 358/2 Days"))
        assertEquals(0, out.listFiles()!!.size)
    }

    @Test
    fun trimmedDumpHashesAsTheWholeCartridge() {
        // a 128 KiB-capacity header (shift 0) whose used size is 0x400: the trimmed file hashes as the
        // whole 128 KiB cartridge, its end 0xFF
        val whole = ByteArray(0x20000) { 0xFF.toByte() }
        for (i in 0 until 0x400) whole[i] = (i * 7).toByte()
        whole[0x14] = 0
        whole[0x80] = 0x00
        whole[0x81] = 0x04
        whole[0x82] = 0
        whole[0x83] = 0
        val trimmed = whole.copyOf(0x400)
        val wholeHashes = RomCheck.hashes(sourceOf(whole), whole, { _, _ -> }, { false })
        val trimmedHashes = RomCheck.hashes(sourceOf(trimmed), trimmed, { _, _ -> }, { false })
        // (only the whole hash: as in the PC tools, the put-back padding is never zeroed for the header-free one)
        assertEquals(wholeHashes.first, trimmedHashes.first)
        // the header-free hash ignores 0x200-0xFFF
        val other = whole.copyOf().also { it[0x300] = 1 }
        val otherHashes = RomCheck.hashes(sourceOf(other), other, { _, _ -> }, { false })
        assertFalse(wholeHashes.first == otherHashes.first)
        assertEquals(wholeHashes.second, otherHashes.second)
    }

    private fun sourceOf(bytes: ByteArray): RomSource = SetupTestFiles.BytesSource(bytes)
}

/** What the setup's tests share: the supported list from tools/, and ROMs opened as [RomSource]s. */
internal object SetupTestFiles {
    fun supportedRoms(): List<SupportedRom> {
        val path = System.getProperty("khdays.supportedRoms") ?: "../tools/supported_roms.json"
        return SupportedRoms.parse(File(path).readText())
    }

    /** [block] given [file] as a [RomSource], read through its FileChannel as the app reads the picked file. */
    fun <T> withRom(file: File, block: (RomSource) -> T): T =
        RandomAccessFile(file, "r").use { block(RomSource.of(it.channel)) }

    /** A setting from -P<name> (a system property here), else the environment's [env], else [default]. */
    fun setting(name: String, env: String, default: String): String =
        System.getProperty(name) ?: System.getenv(env) ?: default

    /** A ROM held in memory. */
    class BytesSource(private val bytes: ByteArray) : RomSource {
        override val size: Long = bytes.size.toLong()

        override fun read(position: Long, buffer: ByteArray, offset: Int, length: Int): Int {
            if (position >= size) return 0
            val count = minOf(length.toLong(), size - position).toInt()
            System.arraycopy(bytes, position.toInt(), buffer, offset, count)
            return count
        }
    }
}
