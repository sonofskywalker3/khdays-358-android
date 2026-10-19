package com.sonofskywalker3.khdays358.setup

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.FileInputStream
import java.io.RandomAccessFile

/**
 * The real ROMs extracted and compared, file by file, with the PC tools' extractions of them
 * (tools/extract_data.py and tools/decompress_data.py). Skipped when a ROM or its reference is missing.
 * The paths come from -PkhdaysRomEu= etc. or KHDAYS_ROM_EU etc.; the defaults are this project's own.
 */
class RomExtractionTest {

    @get:Rule
    val temp = TemporaryFolder()

    private val supportedRoms = SetupTestFiles.supportedRoms()

    private val romEu = File(SetupTestFiles.setting("khdaysRomEu", "KHDAYS_ROM_EU", "$ROMS/Kingdom Hearts - 358-2 Days (Europe) (En,Fr,De,Es,It).nds"))
    private val romUs = File(SetupTestFiles.setting("khdaysRomUs", "KHDAYS_ROM_US", "$ROMS/Kingdom Hearts - 358-2 Days (USA) (En,Fr).nds"))
    private val referenceEu = File(SetupTestFiles.setting("khdaysReferenceEu", "KHDAYS_REFERENCE_EU", "$PORT/data/extracted/1ecf5e7a41a2ae48"))
    private val referenceUs = File(SetupTestFiles.setting("khdaysReferenceUs", "KHDAYS_REFERENCE_US", "$PORT/build/us-test/data/extracted/3cbbe3f15b49fc26"))

    @Test
    fun europeMatchesThePcTools() = extractAndCompare(romEu, referenceEu, "358/2 DAYS (Europe)", "Europe")

    @Test
    fun usaMatchesThePcTools() = extractAndCompare(romUs, referenceUs, "358/2 DAYS (USA)", "USA")

    @Test
    fun truncatedCopyIsAnUnknownDump() {
        assumeTrue("no ROM at $romEu", romEu.isFile)
        val cut = temp.newFile("cut.nds")
        RandomAccessFile(romEu, "r").use { rom ->
            val head = ByteArray(1024 * 1024)
            rom.readFully(head)
            cut.writeBytes(head)
        }
        val result = SetupTestFiles.withRom(cut) { RomCheck.identify(it, supportedRoms) }
        result as RomCheck.Result.Unsupported
        assertEquals(RomCheck.Reason.UNKNOWN_DUMP, result.reason)
        assertEquals("Europe", result.info!!.region)
        println("truncated copy: ${result.message}")
    }

    @Test
    fun cancellingKeepsTheOlderExtraction() {
        assumeTrue("no ROM at $romEu", romEu.isFile)
        val out = temp.newFolder("extracted")
        SetupTestFiles.withRom(romEu) { source ->
            val checked = RomCheck.identify(source, supportedRoms) as RomCheck.Result.Supported
            val older = File(out, checked.info.folderName)
            File(older, "nitrofs").mkdirs()
            File(older, "nitrofs/marker").writeText("older")
            var pieces = 0
            assertThrows(SetupCancelledException::class.java) {
                RomExtractor.extract(source, out, supportedRoms, romEu.name, checked, cancelled = { ++pieces > CANCEL_AFTER })
            }
            assertEquals(listOf(checked.info.folderName), out.list()!!.toList())
            assertEquals("older", File(older, "nitrofs/marker").readText())
        }
    }

    private fun extractAndCompare(rom: File, reference: File, name: String, region: String) {
        assumeTrue("no ROM at $rom", rom.isFile)
        assumeTrue("no reference extraction at $reference", File(reference, "decompressed").isDirectory)
        val out = temp.newFolder("extracted")
        val phases = LinkedHashSet<String>()
        val started = System.nanoTime()
        val folder = SetupTestFiles.withRom(rom) { source ->
            RomExtractor.extract(source, out, supportedRoms, rom.name, progress = { _, _, phase -> phases.add(phase) })
        }
        val seconds = (System.nanoTime() - started) / NANOS_PER_SECOND
        println("$name: checked and extracted in %.1f s".format(seconds))

        assertEquals(reference.name, folder.name)
        assertEquals(listOf(reference.name), out.list()!!.toList())
        assertEquals(listOf(RomExtractor.PHASE_CHECK, RomExtractor.PHASE_EXTRACT, RomExtractor.PHASE_DECOMPRESS), phases.toList())
        for (tree in listOf("system", "nitrofs", "decompressed")) {
            assertSameTree(File(reference, tree), File(folder, tree))
        }

        assertEquals(readJson(File(reference, "manifest.json")), readJson(File(folder, "manifest.json")))

        val metadata = readJson(File(folder, "metadata.json"))
        val expectedMetadata = readJson(File(reference, "metadata.json"))
        val source = metadata.getValue("source") as Map<*, *>
        assertEquals(true, source["supported"])
        assertEquals(name, (source["supported_entry"] as Map<*, *>)["name"])
        // the reference may predate the ROM's entry in supported_roms.json: its "supported" fields aside
        assertEquals(withoutSupport(expectedMetadata), withoutSupport(metadata))

        val decompressed = readJson(File(folder, "decompressed/decompressed_manifest.json"))
        val expectedDecompressed = readJson(File(reference, "decompressed/decompressed_manifest.json"))
        assertEquals(expectedDecompressed - "source_extraction", decompressed - "source_extraction")
        assertEquals(EXPECTED_FAILURES, decompressed["failure_count"])

        val check = SetupTestFiles.withRom(rom) { RomCheck.identify(it, supportedRoms) } as RomCheck.Result.Supported
        assertEquals(region, check.info.region)
    }

    private fun withoutSupport(metadata: Map<String, Any?>): Map<String, Any?> =
        metadata + ("source" to (metadata.getValue("source") as Map<*, *>).filterKeys { it != "supported" && it != "supported_entry" })

    @Suppress("UNCHECKED_CAST")
    private fun readJson(file: File): Map<String, Any?> = Json.parse(file.readText()) as Map<String, Any?>

    /** Every file under [expected] is under [actual] with the same bytes, and nothing else is. */
    private fun assertSameTree(expected: File, actual: File) {
        val expectedFiles = relativeFiles(expected)
        val actualFiles = relativeFiles(actual)
        assertEquals("files under ${actual.name}/", expectedFiles, actualFiles)
        assertTrue(expectedFiles.isNotEmpty())
        val a = ByteArray(COMPARE_CHUNK)
        val b = ByteArray(COMPARE_CHUNK)
        // the decompressed manifest names its own folder: compared as JSON instead
        for (path in expectedFiles - DECOMPRESSED_MANIFEST) {
            val left = File(expected, path)
            val right = File(actual, path)
            assertEquals("size of $path", left.length(), right.length())
            FileInputStream(left).use { l ->
                FileInputStream(right).use { r ->
                    while (true) {
                        val count = l.readNBytes(a, 0, a.size)
                        r.readNBytes(b, 0, count)
                        if (count == 0) break
                        assertTrue("bytes of ${actual.name}/$path", a.copyOf(count).contentEquals(b.copyOf(count)))
                    }
                }
            }
        }
    }

    private fun relativeFiles(root: File): Set<String> =
        root.walkTopDown().filter { it.isFile }.map { it.relativeTo(root).invariantSeparatorsPath }.toSortedSet()

    private companion object {
        const val ROMS = "C:/Users/Jeff/Documents/Projects/358/roms"
        const val PORT = "C:/Users/Jeff/Documents/Projects/358-port"
        const val NANOS_PER_SECOND = 1e9
        const val COMPARE_CHUNK = 1024 * 1024
        const val CANCEL_AFTER = 500
        const val DECOMPRESSED_MANIFEST = "decompressed_manifest.json"

        /** The three files under nitrofs/_unnamed that start like LZ10 but are not (both releases). */
        const val EXPECTED_FAILURES = 3L
    }
}
