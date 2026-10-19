package com.sonofskywalker3.khdays358.setup

import java.io.InputStream

/**
 * One cartridge dump the port supports: an entry of tools/supported_roms.json, the same list the PC
 * installer checks. [entry] is the entry as written, kept for the extraction's metadata.json.
 */
data class SupportedRom(
    val name: String,
    val gameCode: String,
    val romVersion: Int,
    val sha256: String,
    val sha256HeaderFree: String,
    val entry: Map<String, Any?>,
) {
    /** Whether a ROM with these hashes is this dump: its whole hash, or its hash without the header's unused area. */
    fun matches(sha256: String, sha256HeaderFree: String): Boolean =
        (this.sha256.isNotEmpty() && this.sha256 == sha256.lowercase()) ||
            (this.sha256HeaderFree.isNotEmpty() && this.sha256HeaderFree == sha256HeaderFree.lowercase())
}

/**
 * Reads supported_roms.json. The app packages tools/supported_roms.json as the asset [ASSET_NAME]
 * (android/build.gradle copies it), so `SupportedRoms.load(context.assets.open(SupportedRoms.ASSET_NAME))`.
 */
object SupportedRoms {
    const val ASSET_NAME = "supported_roms.json"

    fun load(stream: InputStream): List<SupportedRom> = stream.use { parse(it.readBytes().toString(Charsets.UTF_8)) }

    fun parse(json: String): List<SupportedRom> {
        val root = Json.parse(json) as? Map<*, *> ?: throw IllegalArgumentException("supported_roms.json: not an object.")
        val roms = root["roms"] as? List<*> ?: throw IllegalArgumentException("supported_roms.json must contain a list named 'roms'.")
        return roms.filterIsInstance<Map<*, *>>().map { raw ->
            val entry = raw.entries.associate { it.key.toString() to it.value }
            SupportedRom(
                name = entry["name"]?.toString().orEmpty(),
                gameCode = entry["game_code"]?.toString().orEmpty(),
                romVersion = (entry["rom_version"] as? Number)?.toInt() ?: 0,
                sha256 = entry["sha256"]?.toString().orEmpty().trim().lowercase(),
                sha256HeaderFree = entry["sha256_header_free"]?.toString().orEmpty().trim().lowercase(),
                entry = entry,
            )
        }
    }
}
