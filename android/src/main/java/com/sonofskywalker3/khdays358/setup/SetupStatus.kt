package com.sonofskywalker3.khdays358.setup

import android.content.Context
import com.sonofskywalker3.khdays358.R

/**
 * What the setup screen says of the game's files, in words: shown on the screen ([SetupActivity]) and
 * in the game's settings menu ([MediaFolder.status], a `key=value` line each).
 */
class SetupStatus(
    val game: String,
    val hd: String,
    val hdFolder: String,
    val modsFolder: String,
    val mods: String,
    val savesFolder: String,
) {
    /** The lines the settings menu reads (port/host/settings/settings_model.c). */
    fun lines(): String = listOf("game" to game, "hd" to hd, "hd_folder" to hdFolder, "mods_folder" to modsFolder, "mods" to mods,
        "saves_folder" to savesFolder)
        .joinToString("\n") { (key, value) -> key + "=" + value.replace("\n", ". ").replace(" ✓", "") } // no tick in the game's font

    companion object {
        fun of(context: Context, game: GameFolder, supported: List<SupportedRom>): SetupStatus {
            val release = game.release(supported)
            val ready = game.extraction() != null
            val gameLine = if (ready) {
                context.getString(R.string.setup_game_ready, release?.name ?: game.extraction()?.name)
            } else {
                context.getString(R.string.setup_game_missing)
            }
            val mods = game.modList()
            val menus = mods.any { it.name.equals(GameFolder.HD_MENUS_MOD, ignoreCase = true) && it.on && !it.ignored }
            val hdLine = context.getString(if (game.hasHdVideos()) R.string.setup_hd_videos_yes else R.string.setup_hd_videos_no) +
                "\n" + context.getString(if (menus) R.string.setup_hd_menus_yes else R.string.setup_hd_menus_no)
            val modsLine = if (mods.isEmpty()) {
                context.getString(R.string.setup_mods_none)
            } else {
                context.getString(
                    R.string.setup_mods_found,
                    mods.joinToString(", ") { mod ->
                        when {
                            mod.ignored -> context.getString(R.string.setup_mods_ignored, mod.name)
                            !mod.on -> context.getString(R.string.setup_mods_off, mod.name)
                            else -> mod.name
                        }
                    },
                )
            }
            return SetupStatus(
                gameLine, hdLine,
                MediaFolder.chosen(context, MediaFolder.Kind.HD)?.path ?: "",
                MediaFolder.chosen(context, MediaFolder.Kind.MODS)?.path ?: "",
                modsLine,
                MediaFolder.chosen(context, MediaFolder.Kind.SAVES)?.path ?: "",
            )
        }
    }
}
