package com.sonofskywalker3.khdays358.setup

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

/**
 * The HD pieces the PC installer makes (from the player's own HD collections), found in a folder the
 * player copied to the device and picked, and copied into the [GameFolder]: the videos (a `cutscenes`
 * folder, as in the install's data/) and mods (each folder of a `mods` folder, or a picked mod folder
 * itself: one that holds `files/` or `mod.ini`). The pick may be the whole install, its data/ or mods/,
 * or a folder above it (Download): the search goes [MAX_DEPTH] folders down.
 */
class HdImport(private val resolver: ContentResolver, private val tree: Uri) {

    /** A document of the picked tree. */
    data class Node(val id: String, val name: String, val isFolder: Boolean, val size: Long)

    /** What the picked folder holds: the videos' folder and the mods' folders. */
    data class Found(val cutscenes: Node?, val mods: List<Node>) {
        val isEmpty: Boolean get() = cutscenes == null && mods.isEmpty()
    }

    /** One file to copy, and where. */
    data class Copy(val source: Node, val destination: File)

    /** What a copy needs: its files not already there (`present` were), and their bytes. */
    class Plan(val copies: List<Copy>, val bytes: Long, val present: Int) {
        val files: Int get() = copies.size
    }

    fun find(cancelled: () -> Boolean = { false }): Found {
        val rootId = DocumentsContract.getTreeDocumentId(tree)
        val root = node(rootId) ?: throw IOException("The folder cannot be read.")
        var cutscenes: Node? = null
        val mods = mutableListOf<Node>()
        if (isMod(root)) {
            return Found(null, listOf(root))
        }
        var level = listOf(root)
        for (depth in 0..MAX_DEPTH) {
            val next = mutableListOf<Node>()
            for (folder in level) {
                if (cancelled()) throw SetupCancelledException()
                when {
                    folder.name.equals(CUTSCENES, ignoreCase = true) -> if (cutscenes == null) cutscenes = folder
                    folder.name.equals(MODS, ignoreCase = true) ->
                        mods += children(folder).filter { it.isFolder && !it.name.startsWith(".") }
                    folder !== root && isMod(folder) -> mods += folder
                    else -> next += children(folder).filter { it.isFolder && !it.name.startsWith(".") }
                }
            }
            level = next
        }
        return Found(cutscenes, mods.distinctBy { it.name.lowercase() })
    }

    /** The files of `found` to copy into `game` (one already there at its size is left). */
    fun plan(found: Found, game: GameFolder, cancelled: () -> Boolean = { false }): Plan {
        val copies = mutableListOf<Copy>()
        var bytes = 0L
        var present = 0
        fun add(folder: Node, destination: File) {
            for (child in children(folder)) {
                if (cancelled()) throw SetupCancelledException()
                val target = File(destination, child.name)
                if (child.isFolder) {
                    add(child, target)
                } else if (target.isFile && target.length() == child.size) {
                    ++present
                } else {
                    copies += Copy(child, target)
                    bytes += child.size
                }
            }
        }
        found.cutscenes?.let { add(it, game.cutscenes) }
        found.mods.forEach { add(it, File(game.mods, it.name)) }
        return Plan(copies, bytes, present)
    }

    /** Copies the plan's files, each written beside its place and renamed once whole. */
    fun copy(plan: Plan, progress: (done: Long, total: Long) -> Unit, cancelled: () -> Boolean) {
        val buffer = ByteArray(BUFFER_SIZE)
        var done = 0L
        for (copy in plan.copies) {
            if (cancelled()) throw SetupCancelledException()
            copy.destination.parentFile?.mkdirs()
            val partial = File(copy.destination.path + GameFolder.PARTIAL)
            val uri = DocumentsContract.buildDocumentUriUsingTree(tree, copy.source.id)
            (resolver.openInputStream(uri) ?: throw IOException("${copy.source.name} cannot be read.")).use { input ->
                FileOutputStream(partial).use { output ->
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        output.write(buffer, 0, n)
                        done += n
                        progress(done, plan.bytes)
                        if (cancelled()) {
                            output.close()
                            partial.delete()
                            throw SetupCancelledException()
                        }
                    }
                }
            }
            copy.destination.delete()
            if (!partial.renameTo(copy.destination)) {
                throw IOException("${copy.destination.name} cannot be put in place.")
            }
        }
    }

    /** Deletes the found folders from where the player copied them (only those, never the pick around them). */
    fun deleteSources(found: Found): Boolean {
        var all = true
        for (folder in listOfNotNull(found.cutscenes) + found.mods) {
            val uri = DocumentsContract.buildDocumentUriUsingTree(tree, folder.id)
            all = try {
                DocumentsContract.deleteDocument(resolver, uri) && all
            } catch (e: Exception) {
                false
            }
        }
        return all
    }

    /** A folder is a mod when it holds the mod's files (`files/`) or a scripted mod's `mod.ini`. */
    private fun isMod(folder: Node): Boolean =
        folder.name.equals(GameFolder.HD_MENUS_MOD, ignoreCase = true) ||
            children(folder).any { (it.isFolder && it.name == MOD_FILES) || (!it.isFolder && it.name == MOD_INI) }

    private fun node(id: String): Node? {
        val uri = DocumentsContract.buildDocumentUriUsingTree(tree, id)
        resolver.query(uri, COLUMNS, null, null, null)?.use { c ->
            if (c.moveToFirst()) return Node(id, c.getString(1) ?: "", c.getString(2) == Document.MIME_TYPE_DIR, c.getLong(3))
        }
        return null
    }

    private fun children(folder: Node): List<Node> {
        val uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, folder.id)
        val out = mutableListOf<Node>()
        resolver.query(uri, COLUMNS, null, null, null)?.use { c ->
            while (c.moveToNext()) {
                out += Node(c.getString(0), c.getString(1) ?: "", c.getString(2) == Document.MIME_TYPE_DIR, c.getLong(3))
            }
        }
        return out
    }

    companion object {
        /** How many folders below the pick the HD pieces are looked for. */
        const val MAX_DEPTH = 4
        private const val CUTSCENES = "cutscenes"
        private const val MODS = "mods"
        private const val MOD_FILES = "files"
        private const val MOD_INI = "mod.ini"
        private const val BUFFER_SIZE = 1 shl 20
        private val COLUMNS = arrayOf(
            Document.COLUMN_DOCUMENT_ID,
            Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_MIME_TYPE,
            Document.COLUMN_SIZE,
        )
    }
}
