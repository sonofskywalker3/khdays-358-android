package com.sonofskywalker3.khdays358.setup

import java.io.File

/**
 * The game's folder on the shared storage (Android/data/<package>/files) and what is set up in it: the
 * extracted ROM (data/extracted/<hash>), the HD collection's videos (data/cutscenes) and mods (mods/),
 * where the game itself looks for them. With folders of the player's own ([MediaFolder]) the HD videos
 * are the HD folder's data/cutscenes (the PC installer's folder copied whole) or cutscenes/, and the
 * mods are the chosen mods folder's, as port/runtime/core/folders.c reads them; the app's own while a
 * chosen folder cannot be read.
 */
class GameFolder(val root: File, hd: File? = null, modsFolder: File? = null) {

    /** The player's folder for the HD files, when one is chosen and can be read. */
    val hd: File? = hd?.takeIf { it.isDirectory }

    /** The player's mods folder, when one is chosen and can be read. */
    val modsFolder: File? = modsFolder?.takeIf { it.isDirectory }

    val extracted: File get() = File(root, "data/extracted")
    val cutscenes: File get() = hd?.let { h -> File(h, "data/cutscenes").takeIf { it.isDirectory } ?: File(h, "cutscenes") }
        ?: File(root, "data/cutscenes")
    val mods: File get() = modsFolder ?: File(root, "mods")

    /** The player's saves folder, where the game keeps the mods' order and which are on (saves/mods/order.ini). */
    val saves: File get() = File(root, "saves")

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

    /** A mod found, and whether the game reads it. */
    data class Mod(val name: String, val on: Boolean, val ignored: Boolean)

    /**
     * The mods in the game's order (saves/mods/order.ini, `<folder> = on|off` a line, the ones not
     * listed after in name order) with whether each is on: off when its line says so, when its folder
     * holds a file named .ignore, or when its name begins with a dot (port/runtime/mods/list.h).
     */
    fun modList(): List<Mod> {
        val found = modNames()
        val order = File(saves, "mods/order.ini")
        val listed = LinkedHashMap<String, Boolean>()
        if (order.isFile) {
            order.readLines().forEach { line ->
                val eq = line.lastIndexOf('=')
                if (eq > 0 && !line.trimStart().startsWith("#")) {
                    val name = line.substring(0, eq).trim()
                    if (name.isNotEmpty() && name !in listed) {
                        listed[name] = line.substring(eq + 1).trim() != "off"
                    }
                }
            }
        }
        val names = listed.keys.filter { it in found } + found.filter { it !in listed }
        return names.map { name ->
            val ignored = name.startsWith(".") || File(File(mods, name), IGNORE_FILE).isFile
            Mod(name, listed[name] ?: true, ignored)
        }
    }

    companion object {
        const val PARTIAL = ".partial"
        const val HD_MENUS_MOD = "HD PACK"
        const val IGNORE_FILE = ".ignore"
    }
}
