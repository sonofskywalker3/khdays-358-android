package com.sonofskywalker3.khdays358.setup

import java.io.File

/**
 * The game's folder on the shared storage (Android/data/<package>/files) and what is set up in it: the
 * extracted ROM (data/extracted/<hash>), the HD collection's videos (data/cutscenes) and mods (mods/),
 * where the game itself looks for them. With a folder of the player's own for the mods and HD videos
 * ([media], [MediaFolder]) those two are its mods/ and its data/cutscenes (the PC installer's folder
 * copied whole) or cutscenes/, as port/runtime/core/folders.c reads them; the app's own while it cannot
 * be read.
 */
class GameFolder(val root: File, media: File? = null) {

    /** The player's folder for the mods and HD videos, when one is chosen and can be read. */
    val media: File? = media?.takeIf { it.isDirectory }

    val extracted: File get() = File(root, "data/extracted")
    val cutscenes: File get() = media?.let { m -> File(m, "data/cutscenes").takeIf { it.isDirectory } ?: File(m, "cutscenes") }
        ?: File(root, "data/cutscenes")
    val mods: File get() = File(media ?: root, "mods")

    /** The complete extraction the game will play (one that is still being made ends in .partial). */
    fun extraction(): File? = extracted.listFiles()
        ?.filter { it.isDirectory && !it.name.endsWith(PARTIAL) && File(it, "nitrofs").isDirectory }
        ?.minByOrNull { it.name }

    /** The release of the extraction the game will play, by its folder's name (the ROM's hash). */
    fun release(supported: List<SupportedRom>): SupportedRom? {
        val folder = extraction()?.name ?: return null
        return supported.firstOrNull { it.sha256.startsWith(folder) }
    }

    /** Every extraction but `keep`, and any left half-made: the game plays the first it finds. */
    fun removeOtherExtractions(keep: File) {
        extracted.listFiles()?.filter { it.isDirectory && it != keep }?.forEach { it.deleteRecursively() }
    }

    /** The HD videos are there (the theater's cinematics, as the PC installer lays them out). */
    fun hasHdVideos(): Boolean = File(cutscenes, "cinematics").listFiles()?.isNotEmpty() == true

    /** The installed mods' folder names (the HD menus are the mod "HD PACK"). */
    fun modNames(): List<String> = mods.listFiles()?.filter { it.isDirectory }?.map { it.name }?.sorted().orEmpty()

    companion object {
        const val PARTIAL = ".partial"
        const val HD_MENUS_MOD = "HD PACK"
    }
}
