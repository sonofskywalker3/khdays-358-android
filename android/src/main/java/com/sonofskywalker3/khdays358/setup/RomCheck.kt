package com.sonofskywalker3.khdays358.setup

import java.security.MessageDigest

/**
 * What the ROM's header says and its hashes, as tools/verify_rom.py reports them. [sha256] is the
 * hash of the whole cartridge (a trimmed dump counts with its 0xFF padding put back);
 * [sha256HeaderFree] the same with the header's unused 0x200-0xFFF as zeroes, which dumpers fill
 * differently. [supported] is the supported_roms.json entry it matched, if any.
 */
data class RomInfo(
    val title: String,
    val gameCode: String,
    val makerCode: String,
    val unitCode: Int,
    val romVersion: Int,
    val region: String,
    val fileSize: Long,
    val sha256: String,
    val sha256HeaderFree: String,
    val supported: SupportedRom?,
) {
    /** The extraction's folder name under data/extracted/: the first 16 hex digits of [sha256]. */
    val folderName: String get() = sha256.take(FOLDER_NAME_LENGTH)

    private companion object {
        const val FOLDER_NAME_LENGTH = 16
    }
}

/**
 * Whether a file is a ROM the port supports, by its hashes against supported_roms.json
 * (tools/verify_rom.py, installer/setup_steps.py rom_check). Hashing reads the whole ROM once
 * (256 MB for 358/2 Days), streamed in 1 MiB pieces.
 */
object RomCheck {
    /** The outcome of [identify]. */
    sealed interface Result {
        /** The ROM is [rom], one the port supports. */
        data class Supported(val info: RomInfo, val rom: SupportedRom) : Result

        /** The ROM is not supported; [message] says why in words for the player, [info] is null when it is no DS ROM at all. */
        data class Unsupported(val reason: Reason, val message: String, val info: RomInfo?) : Result
    }

    /** Why a ROM is not supported. */
    enum class Reason {
        /** Too small to be a DS ROM, or unreadable as one. */
        NOT_A_DS_ROM,

        /** A DS ROM of another game. */
        OTHER_GAME,

        /** 358/2 Days, but not a dump in the list: patched, damaged, cut short, or another revision. */
        UNKNOWN_DUMP,
    }

    const val PHASE = "check"

    private const val CAPACITY_OFFSET = 0x14 // the cartridge's capacity: 128 KiB shifted left by this byte
    private const val USED_SIZE_OFFSET = 0x80 // the ROM's used size: a trimmed dump ends here
    private const val LARGEST_CAPACITY_SHIFT = 12 // 512 MiB, the largest DS cartridge
    private const val SMALLEST_CAPACITY = 0x20000L // 128 KiB
    private const val HEADER_FREE_START = 0x200L
    private const val HEADER_FREE_END = 0x1000L
    private const val PADDING_BYTE = 0xFF.toByte()
    internal const val CHUNK_SIZE = 1024 * 1024

    private val REGION_BY_CODE = mapOf('E' to "USA", 'J' to "Japan", 'P' to "Europe", 'K' to "Korea", 'C' to "China")
    private const val UNKNOWN_REGION = "Unknown"

    /**
     * The ROM in [source] checked against [supportedRoms]. [progress] hears (bytes hashed, bytes to
     * hash); [cancelled] is asked between pieces and ends the check with [SetupCancelledException].
     */
    fun identify(
        source: RomSource,
        supportedRoms: List<SupportedRom>,
        progress: (done: Long, total: Long) -> Unit = { _, _ -> },
        cancelled: () -> Boolean = { false },
    ): Result {
        val headerBytes = ByteArray(NdsHeader.SIZE)
        if (source.read(0, headerBytes, 0, headerBytes.size) < headerBytes.size) {
            return Result.Unsupported(
                Reason.NOT_A_DS_ROM,
                "The file is too small to be a Nintendo DS ROM (${source.size} bytes).",
                null,
            )
        }
        val header = NdsHeader.parse(headerBytes)
        val (sha256, headerFree) = hashes(source, headerBytes, progress, cancelled)
        val match = supportedRoms.firstOrNull { it.matches(sha256, headerFree) }
        val info = RomInfo(
            title = header.title,
            gameCode = header.gameCode,
            makerCode = header.makerCode,
            unitCode = header.unitCode,
            romVersion = header.romVersion,
            region = REGION_BY_CODE[header.gameCode.lastOrNull()] ?: UNKNOWN_REGION,
            fileSize = source.size,
            sha256 = sha256,
            sha256HeaderFree = headerFree,
            supported = match,
        )
        if (match != null) {
            return Result.Supported(info, match)
        }
        val names = supportedRoms.joinToString(", ") { it.name }
        val sameGame = supportedRoms.any { it.gameCode.take(GAME_ID_LENGTH) == header.gameCode.take(GAME_ID_LENGTH) }
        return if (sameGame) {
            Result.Unsupported(
                Reason.UNKNOWN_DUMP,
                "This is ${header.title} (${info.region}, ${header.gameCode}), but not a copy the port knows: " +
                    "it is patched, damaged or cut short, or another revision. Supported: $names.",
                info,
            )
        } else {
            val readable = (header.title + header.gameCode).all { it in PRINTABLE_ASCII }
            val what = if (header.title.isBlank() || !readable) "This file is" else "This is ${header.title} (${header.gameCode}),"
            Result.Unsupported(Reason.OTHER_GAME, "$what not 358/2 Days. Supported: $names.", info)
        }
    }

    /** The game's code without its region letter: YKG for 358/2 Days. */
    private const val GAME_ID_LENGTH = 3

    private val PRINTABLE_ASCII = ' '..'~'

    /** (whole-dump SHA-256, header-free SHA-256) in one pass over the ROM (extract_data.py whole_dump_sha256). */
    internal fun hashes(
        source: RomSource,
        header: ByteArray,
        progress: (done: Long, total: Long) -> Unit,
        cancelled: () -> Boolean,
    ): Pair<String, String> {
        val size = source.size
        val padding = trimmedPadding(header, size)
        val total = size + padding
        val whole = MessageDigest.getInstance("SHA-256")
        val headerFree = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(CHUNK_SIZE)
        var position = 0L
        while (position < size) {
            if (cancelled()) throw SetupCancelledException()
            val count = source.read(position, buffer, 0, minOf(CHUNK_SIZE.toLong(), size - position).toInt())
            if (count <= 0) throw RomFormatException("The ROM ended at 0x${hex(position)}, before its size 0x${hex(size)}.")
            whole.update(buffer, 0, count)
            val zeroFrom = maxOf(position, HEADER_FREE_START)
            val zeroTo = minOf(position + count, HEADER_FREE_END)
            if (zeroFrom < zeroTo) {
                // the header's unused area counts as zeroes (the whole hash has already taken the bytes)
                buffer.fill(0, (zeroFrom - position).toInt(), (zeroTo - position).toInt())
            }
            headerFree.update(buffer, 0, count)
            position += count
            progress(position, total)
        }
        if (padding > 0) {
            buffer.fill(PADDING_BYTE)
            var left = padding
            while (left > 0) {
                if (cancelled()) throw SetupCancelledException()
                val step = minOf(left, CHUNK_SIZE.toLong()).toInt()
                whole.update(buffer, 0, step)
                headerFree.update(buffer, 0, step)
                left -= step
                progress(total - left, total)
            }
        }
        return hexString(whole.digest()) to hexString(headerFree.digest())
    }

    /** The 0xFF bytes a trimmed dump lacks to be its whole cartridge (0 for a whole dump). */
    private fun trimmedPadding(header: ByteArray, size: Long): Long {
        val shift = header[CAPACITY_OFFSET].toInt() and 0xFF
        if (shift > LARGEST_CAPACITY_SHIFT) return 0
        val capacity = SMALLEST_CAPACITY shl shift
        val used = u32(header, USED_SIZE_OFFSET)
        return if (NdsHeader.SIZE <= used && used <= size && size < capacity) capacity - size else 0
    }
}

private val HEX_DIGITS = "0123456789abcdef".toCharArray()

/** [bytes] as lowercase hex, as Python's hexdigest(). */
internal fun hexString(bytes: ByteArray): String {
    val out = CharArray(bytes.size * 2)
    for (i in bytes.indices) {
        val b = bytes[i].toInt() and 0xFF
        out[i * 2] = HEX_DIGITS[b ushr 4]
        out[i * 2 + 1] = HEX_DIGITS[b and 0x0F]
    }
    return String(out)
}
