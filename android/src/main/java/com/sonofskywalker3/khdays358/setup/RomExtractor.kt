package com.sonofskywalker3.khdays358.setup

import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.IOException
import java.security.MessageDigest

/**
 * The game's data made from the ROM on the device, as the PC installer makes it
 * (tools/extract_data.py, then tools/decompress_data.py; installer/setup_steps.py prepare_data).
 * Into `<dataExtractedDir>/<sha256[:16]>/`:
 *  - `system/`: header.bin, arm9.bin, arm7.bin, the overlay tables, fnt.bin, fat.bin;
 *  - `nitrofs/`: every file of the ROM's file system, at its FNT path (`_unnamed/file_NNNNN.bin` without one);
 *  - `decompressed/`: each LZ10/LZ11 file decompressed (`x.z` as `x`, anything else as `x.decompressed`),
 *    and `decompressed/decompressed_manifest.json`;
 *  - `manifest.json` and `metadata.json`.
 * The files are byte for byte the Python tools' and the manifests carry the same keys and values.
 *
 * The work happens in `<hash>.partial`, which the game never takes as its data
 * (assets/src/filesystem.cpp autodetect_data_root); only when it is whole is it renamed to `<hash>`,
 * replacing an older extraction of the same ROM. A failure or a cancel deletes the partial folder
 * and leaves any older extraction as it was.
 */
object RomExtractor {
    /** The phases [extract] reports: hashing (bytes), copying out of the ROM (bytes), decompressing (files). */
    const val PHASE_CHECK = RomCheck.PHASE
    const val PHASE_EXTRACT = "extract"
    const val PHASE_DECOMPRESS = "decompress"

    private const val PARTIAL_SUFFIX = ".partial"
    private const val OLD_SUFFIX = ".old.partial"
    private const val SCHEMA_VERSION = 1
    private const val UNNAMED_FOLDER = "_unnamed"
    private const val NITROFS = "nitrofs"
    private const val SYSTEM = "system"
    private const val DECOMPRESSED = "decompressed"
    private const val COMPRESSED_SUFFIX = ".z"
    private const val DECOMPRESSED_SUFFIX = ".decompressed"
    private const val MAGIC_LENGTH = 16
    private val PRINTABLE = 32..126

    /**
     * Extracts the ROM in [source] into `<dataExtractedDir>/<hash16>` and returns that folder.
     *
     * [checked] is the result of an earlier [RomCheck.identify] of the same source, which saves hashing
     * the ROM again; without it the ROM is checked first against [supportedRoms] and an unsupported one
     * fails with [UnsupportedRomException]. [sourceName] is the ROM's file name, kept in metadata.json.
     * [progress] hears (done, total, phase) with the phases above; [cancelled] is asked between files and
     * pieces of files and stops the work with [SetupCancelledException]. Other failures are
     * [IOException]s ([RomFormatException] for a ROM whose tables make no sense). Run it off the main thread.
     */
    fun extract(
        source: RomSource,
        dataExtractedDir: File,
        supportedRoms: List<SupportedRom>,
        sourceName: String,
        checked: RomCheck.Result.Supported? = null,
        progress: (done: Long, total: Long, phase: String) -> Unit = { _, _, _ -> },
        cancelled: () -> Boolean = { false },
    ): File {
        val supported = checked ?: when (
            val result = RomCheck.identify(source, supportedRoms, { done, total -> progress(done, total, PHASE_CHECK) }, cancelled)
        ) {
            is RomCheck.Result.Supported -> result
            is RomCheck.Result.Unsupported -> throw UnsupportedRomException(result.message)
        }
        val name = supported.info.folderName
        val final = File(dataExtractedDir, name)
        val work = File(dataExtractedDir, name + PARTIAL_SUFFIX)
        val old = File(dataExtractedDir, name + OLD_SUFFIX)
        removeTree(work)
        removeTree(old)
        if (!work.mkdirs()) throw IOException("Could not make $work.")
        try {
            val records = extractInto(work, source, supported, sourceName, progress, cancelled)
            decompressInto(work, final, records, progress, cancelled)
            if (final.exists()) {
                rename(final, old)
                try {
                    rename(work, final)
                } catch (e: IOException) {
                    old.renameTo(final) // the older extraction back where it was
                    throw e
                }
                removeTree(old)
            } else {
                rename(work, final)
            }
        } catch (e: Throwable) {
            work.deleteRecursively()
            throw e
        }
        return final
    }

    /** One nitrofs file as manifest.json lists it. */
    private class FileRecord(val fileId: Int, val path: String, val start: Long, val end: Long, val sha256: String) {
        fun toJson(): Map<String, Any?> = linkedMapOf(
            "file_id" to fileId, "path" to path, "start" to start, "end" to end, "size" to end - start, "sha256" to sha256,
        )
    }

    /** system/, nitrofs/, manifest.json and metadata.json (extract_data.py extract_rom): the nitrofs files' records. */
    private fun extractInto(
        root: File,
        source: RomSource,
        supported: RomCheck.Result.Supported,
        sourceName: String,
        progress: (Long, Long, String) -> Unit,
        cancelled: () -> Boolean,
    ): List<FileRecord> {
        val header = NdsHeader.read(source)
        val fnt = source.readExact(header.fntOffset, header.fntSize, "FNT")
        val fat = source.readExact(header.fatOffset, header.fatSize, "FAT")
        val entries = NitroFs.parseFat(fat, source.size)
        val paths = NitroFs.parseFnt(fnt, entries.size)
        val copier = Copier(source, cancelled)

        val systemRanges = listOf(
            // the cartridge header: what the DS loader copies to 0x027ffe00
            Triple("header.bin", 0L, NdsHeader.SIZE.toLong()),
            Triple("arm9.bin", header.arm9Offset, header.arm9Size),
            Triple("arm7.bin", header.arm7Offset, header.arm7Size),
            Triple("arm9_overlay_table.bin", header.arm9OverlayOffset, header.arm9OverlaySize),
            Triple("arm7_overlay_table.bin", header.arm7OverlayOffset, header.arm7OverlaySize),
        ).filter { it.third != 0L }
        val total = systemRanges.sumOf { it.third } + fnt.size + fat.size + entries.sumOf { it.size }
        var done = 0L
        progress(done, total, PHASE_EXTRACT)

        val system = File(root, SYSTEM).also { it.mkdirs() }
        val systemRecords = LinkedHashMap<String, Any?>()
        for ((fileName, offset, size) in systemRanges) {
            val sha256 = copier.copy(offset, size, File(system, fileName))
            systemRecords[fileName] = rangeJson("$SYSTEM/$fileName", offset, size, sha256)
            done += size
        }
        for ((fileName, offset, bytes) in listOf(Triple("fnt.bin", header.fntOffset, fnt), Triple("fat.bin", header.fatOffset, fat))) {
            File(system, fileName).writeBytes(bytes)
            systemRecords[fileName] = rangeJson("$SYSTEM/$fileName", offset, bytes.size.toLong(), copier.sha256(bytes))
            done += bytes.size
        }
        progress(done, total, PHASE_EXTRACT)

        val nitrofs = File(root, NITROFS).also { it.mkdirs() }
        val made = HashSet<File>()
        val records = ArrayList<FileRecord>(entries.size)
        for (entry in entries) {
            val relative = paths[entry.fileId] ?: "$UNNAMED_FOLDER/file_${entry.fileId.toString().padStart(5, '0')}.bin"
            val destination = File(nitrofs, relative)
            destination.parentFile?.let { if (made.add(it)) it.mkdirs() }
            val sha256 = copier.copy(entry.start, entry.size, destination) { piece ->
                done += piece
                progress(done, total, PHASE_EXTRACT)
            }
            records.add(FileRecord(entry.fileId, "$NITROFS/$relative", entry.start, entry.end, sha256))
        }

        val info = supported.info
        val metadata = linkedMapOf(
            "schema_version" to SCHEMA_VERSION,
            "source" to linkedMapOf(
                "filename" to sourceName,
                "file_size" to info.fileSize,
                "sha256" to info.sha256,
                "supported" to true,
                "supported_entry" to supported.rom.entry,
            ),
            "header" to header.toJson(),
            "extraction" to linkedMapOf(
                "nitrofs_file_count" to records.size,
                "named_file_count" to paths.size,
                "unnamed_file_count" to records.size - paths.size,
            ),
        )
        val manifest = linkedMapOf(
            "schema_version" to SCHEMA_VERSION,
            "system" to systemRecords,
            "files" to records.map { it.toJson() },
        )
        writeJson(File(root, "metadata.json"), metadata)
        writeJson(File(root, "manifest.json"), manifest)
        return records
    }

    /** decompressed/ and its manifest from the nitrofs files just written (decompress_data.py decompress_extraction). */
    private fun decompressInto(
        root: File,
        final: File,
        records: List<FileRecord>,
        progress: (Long, Long, String) -> Unit,
        cancelled: () -> Boolean,
    ) {
        val output = File(root, DECOMPRESSED).also { it.mkdirs() }
        val digest = MessageDigest.getInstance("SHA-256")
        val made = HashSet<File>()
        val decompressed = ArrayList<Map<String, Any?>>()
        val failures = ArrayList<Map<String, Any?>>()
        records.forEachIndexed { index, record ->
            if (cancelled()) throw SetupCancelledException()
            progress(index.toLong(), records.size.toLong(), PHASE_DECOMPRESS)
            val file = File(root, record.path)
            if (!file.isFile) {
                failures.add(linkedMapOf("path" to record.path, "error" to "Source file is missing"))
                return@forEachIndexed
            }
            if (!NitroLz.isCompressed(firstByte(file))) return@forEachIndexed
            val compressed = file.readBytes()
            val result = try {
                NitroLz.decompress(compressed)
            } catch (e: RomFormatException) {
                // a file that only starts like a stream: recorded, as the PC tool does, and left out
                failures.add(linkedMapOf("path" to record.path, "error" to e.message))
                return@forEachIndexed
            }
            val relative = decompressedPath(record.path)
            val destination = File(output, relative)
            destination.parentFile?.let { if (made.add(it)) it.mkdirs() }
            destination.writeBytes(result.data)
            val data = result.data
            val magic = data.copyOf(minOf(MAGIC_LENGTH, data.size))
            decompressed.add(
                linkedMapOf(
                    "source_path" to record.path,
                    "output_path" to relative,
                    "compression" to result.compression,
                    "compressed_size" to compressed.size,
                    "decompressed_size" to data.size,
                    "sha256" to hexString(digest.digest(data)),
                    "inner_magic_ascii" to magic.joinToString("") { b ->
                        val c = b.toInt() and 0xFF
                        if (c in PRINTABLE) c.toChar().toString() else "."
                    },
                    "inner_magic_hex" to hexString(magic).uppercase(),
                )
            )
        }
        progress(records.size.toLong(), records.size.toLong(), PHASE_DECOMPRESS)
        val manifest = linkedMapOf(
            "schema_version" to SCHEMA_VERSION,
            "source_extraction" to final.absolutePath,
            "decompressed_count" to decompressed.size,
            "failure_count" to failures.size,
            "files" to decompressed,
            "failures" to failures,
        )
        writeJson(File(output, "decompressed_manifest.json"), manifest)
    }

    /** Where nitrofs/[sourcePath] goes under decompressed/: `x.z` as `x`, anything else as `x.decompressed`. */
    internal fun decompressedPath(sourcePath: String): String {
        val path = sourcePath.removePrefix("$NITROFS/")
        val name = path.substringAfterLast('/')
        val dot = name.lastIndexOf('.')
        // a suffix as Python's PurePath.suffix: a dot neither first nor last in the name
        val suffix = if (dot > 0 && dot < name.length - 1) name.substring(dot) else ""
        return if (suffix.lowercase() == COMPRESSED_SUFFIX) path.dropLast(suffix.length) else path + DECOMPRESSED_SUFFIX
    }

    private fun firstByte(file: File): Int = FileInputStream(file).use { it.read() }

    private fun rangeJson(path: String, offset: Long, size: Long, sha256: String): Map<String, Any?> =
        linkedMapOf("path" to path, "offset" to offset, "size" to size, "sha256" to sha256)

    private fun writeJson(file: File, value: Any?) = file.writeText(Json.write(value) + "\n", Charsets.UTF_8)

    private fun rename(from: File, to: File) {
        if (!from.renameTo(to)) throw IOException("Could not rename $from to $to.")
    }

    private fun removeTree(dir: File) {
        if (dir.exists() && !dir.deleteRecursively()) throw IOException("Could not remove $dir.")
    }

    /** Copies ROM ranges to files through one reused buffer, hashing what it writes. */
    private class Copier(private val source: RomSource, private val cancelled: () -> Boolean) {
        private val buffer = ByteArray(RomCheck.CHUNK_SIZE)
        private val digest = MessageDigest.getInstance("SHA-256")

        fun sha256(bytes: ByteArray): String = hexString(digest.digest(bytes))

        /** [size] bytes at [offset] into [destination]: their SHA-256. [copied] hears each piece's size. */
        fun copy(offset: Long, size: Long, destination: File, copied: (Long) -> Unit = {}): String {
            if (cancelled()) throw SetupCancelledException()
            FileOutputStream(destination).use { out ->
                var position = offset
                var left = size
                while (left > 0) {
                    val count = source.read(position, buffer, 0, minOf(left, buffer.size.toLong()).toInt())
                    if (count <= 0) throw IOException("Truncated read while extracting $destination.")
                    out.write(buffer, 0, count)
                    digest.update(buffer, 0, count)
                    position += count
                    left -= count
                    copied(count.toLong())
                    if (left > 0 && cancelled()) throw SetupCancelledException()
                }
            }
            return hexString(digest.digest())
        }
    }
}
