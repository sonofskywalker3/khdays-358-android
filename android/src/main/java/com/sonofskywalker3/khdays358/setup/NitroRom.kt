package com.sonofskywalker3.khdays358.setup

/**
 * The DS cartridge header's fields the extraction uses (tools/extract_data.py RomHeader): the
 * title and codes, and where the ARM9/ARM7 programs, the file name table (FNT), the file allocation
 * table (FAT) and the overlay tables sit in the ROM.
 */
data class NdsHeader(
    val title: String,
    val gameCode: String,
    val makerCode: String,
    val unitCode: Int,
    val romVersion: Int,
    val arm9Offset: Long,
    val arm9Size: Long,
    val arm7Offset: Long,
    val arm7Size: Long,
    val fntOffset: Long,
    val fntSize: Long,
    val fatOffset: Long,
    val fatSize: Long,
    val arm9OverlayOffset: Long,
    val arm9OverlaySize: Long,
    val arm7OverlayOffset: Long,
    val arm7OverlaySize: Long,
) {
    /** The fields as metadata.json's "header" lists them, in extract_data.py's order. */
    fun toJson(): Map<String, Any?> = linkedMapOf(
        "title" to title,
        "game_code" to gameCode,
        "maker_code" to makerCode,
        "unit_code" to unitCode,
        "rom_version" to romVersion,
        "arm9_offset" to arm9Offset,
        "arm9_size" to arm9Size,
        "arm7_offset" to arm7Offset,
        "arm7_size" to arm7Size,
        "fnt_offset" to fntOffset,
        "fnt_size" to fntSize,
        "fat_offset" to fatOffset,
        "fat_size" to fatSize,
        "arm9_overlay_offset" to arm9OverlayOffset,
        "arm9_overlay_size" to arm9OverlaySize,
        "arm7_overlay_offset" to arm7OverlayOffset,
        "arm7_overlay_size" to arm7OverlaySize,
    )

    companion object {
        /** The header's first 0x200 bytes: what the DS loader copies, and what system/header.bin keeps. */
        const val SIZE = 0x200

        private const val FAT_ENTRY_SIZE = 8

        /** The header in [bytes] (at least [SIZE] of them). */
        fun parse(bytes: ByteArray): NdsHeader = NdsHeader(
            title = asciiField(bytes, 0x00, 0x0C),
            gameCode = asciiField(bytes, 0x0C, 0x10),
            makerCode = asciiField(bytes, 0x10, 0x12),
            unitCode = bytes[0x12].toInt() and 0xFF,
            romVersion = bytes[0x1E].toInt() and 0xFF,
            arm9Offset = u32(bytes, 0x20),
            arm9Size = u32(bytes, 0x2C),
            arm7Offset = u32(bytes, 0x30),
            arm7Size = u32(bytes, 0x3C),
            fntOffset = u32(bytes, 0x40),
            fntSize = u32(bytes, 0x44),
            fatOffset = u32(bytes, 0x48),
            fatSize = u32(bytes, 0x4C),
            arm9OverlayOffset = u32(bytes, 0x50),
            arm9OverlaySize = u32(bytes, 0x54),
            arm7OverlayOffset = u32(bytes, 0x58),
            arm7OverlaySize = u32(bytes, 0x5C),
        )

        /** The header of the ROM in [source], its ranges checked against the ROM's size. */
        fun read(source: RomSource): NdsHeader {
            val header = parse(source.readExact(0, SIZE.toLong(), "NDS header"))
            val ranges = listOf(
                Triple("ARM9", header.arm9Offset, header.arm9Size),
                Triple("ARM7", header.arm7Offset, header.arm7Size),
                Triple("FNT", header.fntOffset, header.fntSize),
                Triple("FAT", header.fatOffset, header.fatSize),
                Triple("ARM9 overlay table", header.arm9OverlayOffset, header.arm9OverlaySize),
                Triple("ARM7 overlay table", header.arm7OverlayOffset, header.arm7OverlaySize),
            )
            for ((label, offset, size) in ranges) {
                if (size != 0L && offset + size > source.size) {
                    throw RomFormatException(
                        "$label: range outside the ROM (offset=0x${hex(offset)}, size=0x${hex(size)}, rom=0x${hex(source.size)})."
                    )
                }
            }
            if (header.fatSize % FAT_ENTRY_SIZE != 0L) {
                throw RomFormatException("The FAT size is not a multiple of 8 bytes.")
            }
            return header
        }
    }
}

/** One file of the ROM's NitroFS: its FAT number and its byte range in the ROM. */
data class FatEntry(val fileId: Int, val start: Long, val end: Long) {
    val size: Long get() = end - start
}

/** The ROM's NitroFS tables: the FAT's file ranges and the FNT's paths (tools/extract_data.py parse_fat/parse_fnt). */
internal object NitroFs {
    private const val FAT_ENTRY_SIZE = 8
    private const val MAIN_ENTRY_SIZE = 8
    private const val ROOT_DIRECTORY_ID = 0xF000
    private const val END_OF_DIRECTORY = 0
    private const val DIRECTORY_FLAG = 0x80
    private const val NAME_LENGTH_MASK = 0x7F

    fun parseFat(fat: ByteArray, romSize: Long): List<FatEntry> =
        (0 until fat.size / FAT_ENTRY_SIZE).map { fileId ->
            val start = u32(fat, fileId * FAT_ENTRY_SIZE)
            val end = u32(fat, fileId * FAT_ENTRY_SIZE + 4)
            if (end < start) {
                throw RomFormatException("FAT[$fileId]: end 0x${hex(end)} precedes start 0x${hex(start)}.")
            }
            if (end > romSize) {
                throw RomFormatException("FAT[$fileId]: end 0x${hex(end)} outside the ROM (0x${hex(romSize)}).")
            }
            FatEntry(fileId, start, end)
        }

    /** Each named file's path ('/'-separated, relative to nitrofs/), by FAT number. */
    fun parseFnt(fnt: ByteArray, fatCount: Int): Map<Int, String> {
        if (fnt.size < MAIN_ENTRY_SIZE) throw RomFormatException("The FNT is too small.")
        val rootSubtable = u32(fnt, 0)
        val directoryCount = u16(fnt, 6)
        if (directoryCount == 0) throw RomFormatException("The FNT declares zero directories.")
        if (directoryCount * MAIN_ENTRY_SIZE > fnt.size) throw RomFormatException("The main directory table exceeds the FNT.")
        if (rootSubtable >= fnt.size) throw RomFormatException("The root subdirectory points outside the FNT.")

        // directory id -> (subtable offset, first file id)
        val main = HashMap<Int, Pair<Int, Int>>()
        for (index in 0 until directoryCount) {
            val offset = index * MAIN_ENTRY_SIZE
            val subtable = u32(fnt, offset)
            val directoryId = ROOT_DIRECTORY_ID + index
            if (subtable >= fnt.size) {
                throw RomFormatException("Directory 0x${directoryId.toString(16).uppercase().padStart(4, '0')}: subtable outside the FNT.")
            }
            main[directoryId] = subtable.toInt() to u16(fnt, offset + 4)
        }

        val paths = HashMap<Int, String>()
        val visited = HashSet<Int>()

        fun walk(directoryId: Int, parent: String) {
            val id = "0x${directoryId.toString(16).uppercase().padStart(4, '0')}"
            val label = "Directory $id"
            if (!visited.add(directoryId)) throw RomFormatException("Cycle detected in directory $id.")
            val (subtable, firstFile) = main[directoryId]
                ?: throw RomFormatException("Reference to nonexistent directory $id.")
            var cursor = subtable
            var fileId = firstFile
            while (true) {
                if (cursor >= fnt.size) throw RomFormatException("$label: subtable without terminator.")
                val descriptor = fnt[cursor++].toInt() and 0xFF
                if (descriptor == END_OF_DIRECTORY) break
                val nameLength = descriptor and NAME_LENGTH_MASK
                if (nameLength == 0) throw RomFormatException("$label: zero-length name.")
                if (cursor + nameLength > fnt.size) throw RomFormatException("$label: name outside the FNT.")
                val name = decodeAscii(fnt, cursor, cursor + nameLength)
                cursor += nameLength
                if (descriptor and DIRECTORY_FLAG != 0) {
                    if (cursor + 2 > fnt.size) throw RomFormatException("Truncated subdirectory ID.")
                    val childId = u16(fnt, cursor)
                    cursor += 2
                    val childName = safeComponent(name, "dir_${childId.toString(16).uppercase().padStart(4, '0')}")
                    walk(childId, join(parent, childName))
                } else {
                    if (fileId >= fatCount) {
                        throw RomFormatException("The FNT references file_id $fileId, but the FAT only contains $fatCount entries.")
                    }
                    paths[fileId] = join(parent, safeComponent(name, "file_${fileId.toString().padStart(5, '0')}.bin"))
                    fileId++
                }
            }
            visited.remove(directoryId)
        }

        walk(ROOT_DIRECTORY_ID, "")
        return paths
    }

    private fun join(parent: String, name: String): String = if (parent.isEmpty()) name else "$parent/$name"

    /** A name safe as one path component: separators and NULs out, spaces and dots trimmed off its ends. */
    fun safeComponent(name: String, fallback: String): String {
        val cleaned = name.replace('\\', '_').replace('/', '_').replace("\u0000", "")
            .trim(::isPythonSpace).trim('.')
        return cleaned.ifEmpty { fallback }
    }
}

private const val NUL = 0
private const val ASCII_LIMIT = 0x80
private const val REPLACEMENT_CHARACTER = '�'

/** Whitespace as Python's str.strip() sees it, over the ASCII range plus U+FFFD that decoding makes. */
private fun isPythonSpace(c: Char): Boolean = c == ' ' || c in '\t'..'\r' || c in '\u001C'..'\u001F'

/** Bytes [from, to) as ASCII, non-ASCII bytes as U+FFFD (Python's `decode("ascii", errors="replace")`). */
internal fun decodeAscii(bytes: ByteArray, from: Int, to: Int): String {
    val out = StringBuilder(to - from)
    for (i in from until to) {
        val b = bytes[i].toInt() and 0xFF
        out.append(if (b < ASCII_LIMIT) b.toChar() else REPLACEMENT_CHARACTER)
    }
    return out.toString()
}

/** A header text field: up to its first NUL, as ASCII, trimmed. */
internal fun asciiField(bytes: ByteArray, from: Int, to: Int): String {
    var end = from
    while (end < to && bytes[end].toInt() != NUL) end++
    return decodeAscii(bytes, from, end).trim(::isPythonSpace)
}

internal fun u16(bytes: ByteArray, at: Int): Int = (bytes[at].toInt() and 0xFF) or ((bytes[at + 1].toInt() and 0xFF) shl 8)

internal fun u32(bytes: ByteArray, at: Int): Long = (u16(bytes, at).toLong()) or (u16(bytes, at + 2).toLong() shl 16)

internal fun hex(value: Long): String = value.toString(16).uppercase()
