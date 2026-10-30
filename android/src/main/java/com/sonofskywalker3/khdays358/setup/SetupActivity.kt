package com.sonofskywalker3.khdays358.setup

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.StateListDrawable
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.StatFs
import android.provider.OpenableColumns
import android.text.format.Formatter
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import com.sonofskywalker3.khdays358.MainActivity
import com.sonofskywalker3.khdays358.R
import com.sonofskywalker3.khdays358.setup.MediaFolder.Kind
import java.io.File
import java.io.FileInputStream
import java.io.IOException

/**
 * The app's first screen: the game set up from the player's own ROM (any supported release, unpacked
 * here by [RomExtractor]), the HD pieces the PC installer made copied in ([HdImport]), and the folders
 * of the player's own for the HD files and the mods ([MediaFolder]). Once the game is set up it goes
 * straight to the game, unless opened by the launcher's "Set up / add HD files" shortcut
 * ([EXTRA_SETUP]) or by the game's settings menu, which may ask for one of its actions ([EXTRA_ACTION]):
 * then that action starts at once and the screen goes back to the game when it is done. Works by touch
 * and by a pad (the D-pad moves, A presses, B goes back).
 */
class SetupActivity : Activity() {

    private val main = Handler(Looper.getMainLooper())
    private lateinit var game: GameFolder
    private lateinit var supported: List<SupportedRom>

    @Volatile private var cancel = false
    private var askingAccess = false // Android's page for the access to all files was asked for
    private var leftForAccess = false // ... and this screen went behind it
    private var working = false
    private var actionMode = false // opened on one action: back to the game once it is done
    private var lastHd: Pair<HdImport, HdImport.Found>? = null

    private lateinit var gameStatus: TextView
    private lateinit var gameButton: Button
    private lateinit var hdStatus: TextView
    private lateinit var hdButton: Button
    private lateinit var deleteButton: Button
    private lateinit var hdFolderStatus: TextView
    private lateinit var hdFolderButton: Button
    private lateinit var hdFolderResetButton: Button
    private lateinit var modsStatus: TextView
    private lateinit var modsFolderButton: Button
    private lateinit var modsFolderResetButton: Button
    private lateinit var progressBar: ProgressBar
    private lateinit var progressText: TextView
    private lateinit var cancelButton: Button
    private lateinit var playButton: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        game = MediaFolder.gameFolder(this)
        supported = assets.open(SupportedRoms.ASSET_NAME).use { SupportedRoms.load(it) }
        val action = intent.getStringExtra(EXTRA_ACTION)
        if (game.extraction() != null && !intent.getBooleanExtra(EXTRA_SETUP, false)) {
            play()
            return
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(build())
        refresh()
        (if (game.extraction() == null) gameButton else playButton).requestFocus()
        if (action != null && savedInstanceState == null) {
            actionMode = true
            when (action) {
                ACTION_ROM -> chooseRom()
                ACTION_HD_IMPORT -> chooseHd()
                ACTION_HD_FOLDER -> chooseFolder(Kind.HD)
                ACTION_HD_APP -> { resetFolder(Kind.HD); endAction() }
                ACTION_MODS_FOLDER -> chooseFolder(Kind.MODS)
                ACTION_MODS_APP -> { resetFolder(Kind.MODS); endAction() }
                ACTION_SAVES_FOLDER -> chooseFolder(Kind.SAVES)
                ACTION_SAVES_APP -> { resetFolder(Kind.SAVES); endAction(READ_DELAY_MS) }
                else -> actionMode = false
            }
        }
    }

    // Back from Android's page for the access to all files (or its question, Android 10). The screen is
    // also resumed once between the picker and that page, before the player has seen it: then the
    // folder waits.
    override fun onResume() {
        super.onResume()
        if (!MediaFolder.hasPending(this) || (askingAccess && !leftForAccess)) {
            return
        }
        val (kind, folder) = MediaFolder.takePending(this) ?: return
        askingAccess = false
        if (MediaFolder.hasAccess(this)) {
            useFolder(kind, folder)
        } else {
            progressText.text = getString(R.string.setup_media_denied)
            endAction()
        }
    }

    override fun onPause() {
        super.onPause()
        leftForAccess = askingAccess
    }

    override fun onDestroy() {
        cancel = true
        super.onDestroy()
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) {
            if (actionMode) finish() // the picker left: nothing to do here
            return
        }
        when (requestCode) {
            PICK_ROM -> importRom(uri)
            PICK_HD -> importHd(uri)
            PICK_HD_FOLDER -> pickFolder(Kind.HD, uri)
            PICK_MODS_FOLDER -> pickFolder(Kind.MODS, uri)
            PICK_SAVES_FOLDER -> pickFolder(Kind.SAVES, uri)
        }
    }

    /** A pad's A presses the focused button and B goes back (cancels the work, else leaves). */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_UP) {
            when (event.keyCode) {
                KeyEvent.KEYCODE_BUTTON_A -> {
                    currentFocus?.takeIf { it.isEnabled }?.performClick()
                    return true
                }
                KeyEvent.KEYCODE_BUTTON_B -> {
                    if (working) cancel = true else finish()
                    return true
                }
            }
        }
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_A || event.keyCode == KeyEvent.KEYCODE_BUTTON_B) {
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    /** Opened on one action and it is done: back to the game, after a moment to read what was said. */
    private fun endAction(delay: Long = 0L) {
        if (!actionMode) {
            return
        }
        if (delay > 0L) main.postDelayed({ if (!isDestroyed) finish() }, delay) else finish()
    }

    // --- The game ----------------------------------------------------------------------------------

    private fun chooseRom() {
        // .nds files have no MIME type of their own: any file, checked once chosen
        val pick = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        startActivityForResult(pick, PICK_ROM)
    }

    private fun importRom(uri: Uri) {
        val name = displayName(uri)
        work { setProgress ->
            val pfd = contentResolver.openFileDescriptor(uri, "r") ?: throw IOException("the file cannot be opened")
            pfd.use {
                FileInputStream(it.fileDescriptor).channel.use { channel ->
                    val source = RomSource.of(channel)
                    setProgress(getString(R.string.setup_game_checking), 0, 1)
                    val checked = RomCheck.identify(
                        source, supported,
                        { done, total -> setProgress(getString(R.string.setup_game_checking), done, total) },
                        { cancel },
                    )
                    if (checked is RomCheck.Result.Unsupported) {
                        return@work checked.message
                    }
                    checked as RomCheck.Result.Supported
                    val need = source.size * SPACE_PER_ROM_BYTE_NUMERATOR / SPACE_PER_ROM_BYTE_DENOMINATOR
                    game.extracted.mkdirs()
                    val free = StatFs(game.extracted.path).availableBytes
                    if (free < need) {
                        return@work getString(R.string.setup_game_no_space, size(need), size(free))
                    }
                    val folder = RomExtractor.extract(
                        source, game.extracted, supported, name, checked,
                        { done, total, phase ->
                            val label = if (phase == RomExtractor.PHASE_DECOMPRESS) {
                                getString(R.string.setup_game_decompressing, done.toInt(), total.toInt())
                            } else {
                                getString(R.string.setup_game_extracting)
                            }
                            setProgress(label, done, total)
                        },
                        { cancel },
                    )
                    game.removeOtherExtractions(folder)
                    getString(R.string.setup_game_done, checked.rom.name)
                }
            }
        }
    }

    // --- HD ----------------------------------------------------------------------------------------

    private fun chooseHd() {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_HD)
    }

    private fun importHd(tree: Uri) {
        lastHd = null
        work { setProgress ->
            setProgress(getString(R.string.setup_hd_looking), 0, 0)
            val import = HdImport(contentResolver, tree)
            val found = import.find { cancel }
            if (found.isEmpty) {
                return@work getString(R.string.setup_hd_none)
            }
            val what = (listOfNotNull(found.cutscenes?.let { getString(R.string.setup_hd_found_videos) }) +
                found.mods.map { getString(R.string.setup_hd_found_mod, it.name) }).joinToString(", ")
            setProgress(getString(R.string.setup_hd_looking), 0, 0)
            val plan = import.plan(found, game) { cancel }
            lastHd = import to found
            if (plan.files == 0) {
                return@work getString(R.string.setup_hd_already)
            }
            val into = game.hd ?: game.root
            into.mkdirs()
            val free = StatFs(into.path).availableBytes
            if (free < plan.bytes + SPACE_MARGIN) {
                lastHd = null
                return@work getString(R.string.setup_hd_no_space, size(plan.bytes), size(free))
            }
            import.copy(plan, { done, total -> setProgress(getString(R.string.setup_hd_copying, what), done, total) }) { cancel }
            getString(R.string.setup_hd_done, what)
        }
    }

    private fun deleteHdSource() {
        val (import, found) = lastHd ?: return
        lastHd = null
        work { setProgress ->
            setProgress(getString(R.string.setup_hd_looking), 0, 0)
            getString(if (import.deleteSources(found)) R.string.setup_hd_deleted else R.string.setup_hd_delete_failed)
        }
    }

    // --- The folders of the player's own: the HD files', the mods' --------------------------------------

    private fun chooseFolder(kind: Kind) {
        val request = when (kind) {
            Kind.HD -> PICK_HD_FOLDER
            Kind.MODS -> PICK_MODS_FOLDER
            Kind.SAVES -> PICK_SAVES_FOLDER
        }
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), request)
    }

    private fun pickFolder(kind: Kind, tree: Uri) {
        val folder = MediaFolder.pathOf(tree)
        if (folder == null) {
            progressText.text = getString(R.string.setup_media_not_local)
            endAction(READ_DELAY_MS)
            return
        }
        if (MediaFolder.hasAccess(this)) {
            useFolder(kind, folder)
            return
        }
        MediaFolder.keepPending(this, kind, folder)
        askingAccess = true
        leftForAccess = false
        progressText.text = getString(R.string.setup_media_asking)
        MediaFolder.askAccess(this, PICK_HD_FOLDER)
    }

    private fun useFolder(kind: Kind, folder: File) {
        MediaFolder.keep(this, kind, folder)
        lastHd = null
        game = MediaFolder.gameFolder(this)
        val done = when (kind) {
            Kind.HD -> R.string.setup_hd_folder_done
            Kind.MODS -> R.string.setup_mods_folder_done
            Kind.SAVES -> R.string.setup_saves_folder_done
        }
        progressText.text = getString(done, folder.path)
        refresh()
        endAction(READ_DELAY_MS)
    }

    private fun resetFolder(kind: Kind) {
        MediaFolder.forget(this, kind)
        lastHd = null
        game = MediaFolder.gameFolder(this)
        val done = when (kind) {
            Kind.HD -> R.string.setup_hd_folder_reset_done
            Kind.MODS -> R.string.setup_mods_folder_reset_done
            Kind.SAVES -> R.string.setup_saves_folder_reset_done
        }
        progressText.text = getString(done)
        refresh()
        (if (kind == Kind.HD) hdFolderButton else modsFolderButton).requestFocus()
    }

    // --- The work and the screen -------------------------------------------------------------------

    /**
     * Runs `job` off the main thread with the progress shown; what it returns is shown when it ends.
     * [cancel] (Cancel, or B) stops it with [SetupCancelledException].
     */
    private fun work(job: (setProgress: (String, Long, Long) -> Unit) -> String) {
        cancel = false
        working = true
        refresh()
        cancelButton.requestFocus()
        Thread {
            var shownAt = 0L
            val message = try {
                job { label, done, total ->
                    val now = System.nanoTime()
                    if (now - shownAt >= PROGRESS_INTERVAL_NS || done == total) {
                        shownAt = now
                        main.post { showProgress(label, done, total) }
                    }
                }
            } catch (e: SetupCancelledException) {
                getString(R.string.setup_cancelled)
            } catch (e: IOException) {
                Log.w(TAG, "setup: $e")
                getString(R.string.setup_failed, e.message ?: e.toString())
            } catch (e: RuntimeException) {
                Log.e(TAG, "setup", e)
                getString(R.string.setup_failed, e.message ?: e.toString())
            }
            main.post {
                if (isDestroyed) return@post
                working = false
                progressText.text = message
                refresh()
                when {
                    deleteButton.visibility == View.VISIBLE -> deleteButton
                    game.extraction() != null -> playButton
                    else -> gameButton
                }.requestFocus()
                if (deleteButton.visibility == View.VISIBLE) {
                    actionMode = false // the copy made can be deleted from here: the screen stays
                }
                endAction(READ_DELAY_MS)
            }
        }.start()
    }

    private fun showProgress(label: String, done: Long, total: Long) {
        progressText.text = label
        progressBar.isIndeterminate = total <= 0
        if (total > 0) {
            progressBar.max = PROGRESS_STEPS
            progressBar.progress = (done * PROGRESS_STEPS / total).toInt()
        }
    }

    /** The screen as things stand: what is set up, what can be pressed. */
    private fun refresh() {
        val status = SetupStatus.of(this, game, supported)
        val ready = game.extraction() != null
        gameStatus.text = status.game
        gameButton.text = getString(if (ready) R.string.setup_game_choose_other else R.string.setup_game_choose)
        hdStatus.text = status.hd
        gameButton.isEnabled = !working
        hdButton.isEnabled = !working && ready
        playButton.isEnabled = !working && ready
        folderRows(Kind.HD, hdFolderStatus, hdFolderButton, hdFolderResetButton)
        folderRows(Kind.MODS, modsStatus, modsFolderButton, modsFolderResetButton)
        modsStatus.text = modsStatus.text.toString() + "\n" + status.mods
        val hd = lastHd
        deleteButton.visibility = if (!working && hd != null) View.VISIBLE else View.GONE
        if (hd != null) {
            deleteButton.text = getString(R.string.setup_hd_delete_source, size(sourceBytes(hd.second)))
        }
        progressBar.visibility = if (working) View.VISIBLE else View.GONE
        cancelButton.visibility = if (working) View.VISIBLE else View.GONE
    }

    /** A folder's status line and its two buttons, as it stands. */
    private fun folderRows(kind: Kind, status: TextView, choose: Button, reset: Button) {
        val chosen = MediaFolder.chosen(this, kind)
        val readable = if (kind == Kind.HD) game.hd else game.modsFolder
        val own = if (kind == Kind.HD) File(game.root, "data/cutscenes") else File(game.root, "mods")
        status.text = when {
            chosen == null -> getString(R.string.setup_media_app, own.path)
            readable == null -> getString(R.string.setup_media_unreadable, chosen.path)
            else -> getString(R.string.setup_media_own, chosen.path)
        }
        choose.text = getString(
            when {
                chosen != null -> R.string.setup_media_choose_other
                kind == Kind.HD -> R.string.setup_hd_folder_choose
                else -> R.string.setup_mods_folder_choose
            },
        )
        choose.isEnabled = !working
        reset.isEnabled = !working
        reset.visibility = if (chosen != null) View.VISIBLE else View.GONE
    }

    private fun sourceBytes(found: HdImport.Found): Long {
        var bytes = 0L
        found.cutscenes?.let { bytes += game.cutscenes.walkTopDown().filter { it.isFile }.sumOf { it.length() } }
        found.mods.forEach { mod -> bytes += File(game.mods, mod.name).walkTopDown().filter { it.isFile }.sumOf { it.length() } }
        return bytes
    }

    private fun play() {
        startActivity(Intent(this, MainActivity::class.java))
        finish()
    }

    private fun build(): View {
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(32), dp(24), dp(32), dp(32))
        }
        column.addView(text(R.string.setup_title, TITLE_SP, bold = true))
        column.addView(text(R.string.setup_subtitle, HEADING_SP, color = MUTED))
        column.addView(text(R.string.setup_controls_hint, SMALL_SP, color = MUTED).spaced(dp(4)))

        column.addView(section().apply {
            addView(text(R.string.setup_game_heading, HEADING_SP, bold = true))
            gameStatus = text(0, BODY_SP, color = ACCENT).spaced(dp(8)).also { addView(it) }
            addView(text(R.string.setup_game_help, BODY_SP).spaced(dp(8)))
            gameButton = button(R.string.setup_game_choose) { chooseRom() }.also { addView(it) }
        })

        column.addView(section().apply {
            addView(text(R.string.setup_hd_heading, HEADING_SP, bold = true))
            hdStatus = text(0, BODY_SP, color = ACCENT).spaced(dp(8)).also { addView(it) }
            addView(text(R.string.setup_hd_help_title, BODY_SP).spaced(dp(8)))
            for (step in intArrayOf(R.string.setup_hd_step1, R.string.setup_hd_step2, R.string.setup_hd_step3, R.string.setup_hd_step4)) {
                addView(text(step, BODY_SP).spaced(dp(6)))
            }
            hdButton = button(R.string.setup_hd_choose) { chooseHd() }.also { addView(it) }
            deleteButton = button(R.string.setup_hd_delete_source) { deleteHdSource() }.also { addView(it) }
            hdFolderStatus = text(0, BODY_SP, color = ACCENT).spaced(dp(20)).also { addView(it) }
            addView(text(R.string.setup_hd_folder_help, BODY_SP).spaced(dp(8)))
            hdFolderButton = button(R.string.setup_hd_folder_choose) { chooseFolder(Kind.HD) }.also { addView(it) }
            hdFolderResetButton = button(R.string.setup_media_reset) { resetFolder(Kind.HD) }.also { addView(it) }
        })

        column.addView(section().apply {
            addView(text(R.string.setup_mods_heading, HEADING_SP, bold = true))
            modsStatus = text(0, BODY_SP, color = ACCENT).spaced(dp(8)).also { addView(it) }
            addView(text(R.string.setup_mods_help, BODY_SP).spaced(dp(8)))
            modsFolderButton = button(R.string.setup_mods_folder_choose) { chooseFolder(Kind.MODS) }.also { addView(it) }
            modsFolderResetButton = button(R.string.setup_media_reset) { resetFolder(Kind.MODS) }.also { addView(it) }
        })

        progressText = text(0, BODY_SP).spaced(dp(16)).also { column.addView(it) }
        progressBar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).also {
            column.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, dp(16)).apply { topMargin = dp(8) })
        }
        cancelButton = button(R.string.setup_cancel) { cancel = true }.also { column.addView(it) }
        playButton = button(R.string.setup_play, primary = true) { play() }.also { column.addView(it) }

        val centred = FrameLayout(this).apply {
            addView(column, FrameLayout.LayoutParams(dp(MAX_WIDTH_DP).coerceAtMost(resources.displayMetrics.widthPixels), WRAP_CONTENT, Gravity.CENTER_HORIZONTAL))
        }
        return ScrollView(this).apply {
            setBackgroundColor(BACKGROUND)
            isFillViewport = true
            addView(centred)
        }
    }

    private fun section(): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(24), dp(20), dp(24), dp(20))
        background = GradientDrawable().apply { setColor(PANEL); cornerRadius = dp(12).toFloat() }
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(20) }
    }

    private fun text(res: Int, sp: Float, bold: Boolean = false, color: Int = TEXT): TextView = TextView(this).apply {
        if (res != 0) setText(res)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sp)
        setTextColor(color)
        if (bold) typeface = Typeface.DEFAULT_BOLD
    }

    private fun TextView.spaced(top: Int): TextView = apply {
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = top }
    }

    /** A button that shows when it has the pad's focus (a ring) and when it cannot be pressed (dimmed). */
    private fun button(res: Int, primary: Boolean = false, onClick: () -> Unit): Button = Button(this).apply {
        setText(res)
        isAllCaps = false
        setTextSize(TypedValue.COMPLEX_UNIT_SP, if (primary) HEADING_SP else BODY_SP)
        setTextColor(if (primary) BACKGROUND else TEXT)
        isFocusable = true
        val fill = if (primary) ACCENT else BUTTON
        background = StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_focused), shape(fill, FOCUS_RING))
            addState(intArrayOf(-android.R.attr.state_enabled), shape(DISABLED, DISABLED))
            addState(intArrayOf(), shape(fill, fill))
        }
        setPadding(dp(20), dp(12), dp(20), dp(12))
        layoutParams = LinearLayout.LayoutParams(if (primary) MATCH_PARENT else WRAP_CONTENT, WRAP_CONTENT).apply {
            topMargin = dp(16)
        }
        setOnClickListener { onClick() }
    }

    private fun shape(fill: Int, stroke: Int) = GradientDrawable().apply {
        setColor(fill)
        cornerRadius = dp(8).toFloat()
        setStroke(dp(3), stroke)
    }

    private fun displayName(uri: Uri): String =
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        } ?: uri.lastPathSegment ?: "rom.nds"

    private fun size(bytes: Long): String = Formatter.formatShortFileSize(this, bytes)

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    companion object {
        /** Opens the screen even when the game is set up (the launcher's shortcut). */
        const val EXTRA_SETUP = "com.sonofskywalker3.khdays358.SETUP"
        /** One of the screen's actions, started at once; back to the game when it is done (MediaFolder.act). */
        const val EXTRA_ACTION = "com.sonofskywalker3.khdays358.ACTION"
        const val ACTION_ROM = "rom"
        const val ACTION_HD_IMPORT = "hd_import"
        const val ACTION_HD_FOLDER = "hd_folder"
        const val ACTION_HD_APP = "hd_app"
        const val ACTION_MODS_FOLDER = "mods_folder"
        const val ACTION_MODS_APP = "mods_app"
        const val ACTION_SAVES_FOLDER = "saves_folder" // the saves' folder: from the game's settings menu only
        const val ACTION_SAVES_APP = "saves_app"
        private const val TAG = "khdays"
        private const val PICK_ROM = 1
        private const val PICK_HD = 2
        private const val PICK_HD_FOLDER = 3
        private const val PICK_MODS_FOLDER = 4
        private const val PICK_SAVES_FOLDER = 5
        // the unpacked game takes about 1.1 times its ROM: 1.5 asked for
        private const val SPACE_PER_ROM_BYTE_NUMERATOR = 3L
        private const val SPACE_PER_ROM_BYTE_DENOMINATOR = 2L
        private const val SPACE_MARGIN = 64L * 1024 * 1024
        private const val PROGRESS_STEPS = 1000
        private const val PROGRESS_INTERVAL_NS = 100_000_000L
        private const val READ_DELAY_MS = 2500L // what was said stays this long before the game is back
        private const val MAX_WIDTH_DP = 900
        private const val TITLE_SP = 32f
        private const val HEADING_SP = 22f
        private const val BODY_SP = 17f
        private const val SMALL_SP = 14f
        private val BACKGROUND = Color.rgb(0x10, 0x12, 0x1a)
        private val PANEL = Color.rgb(0x1c, 0x20, 0x2c)
        private val BUTTON = Color.rgb(0x2c, 0x33, 0x45)
        private val DISABLED = Color.rgb(0x22, 0x25, 0x2e)
        private val TEXT = Color.rgb(0xec, 0xee, 0xf4)
        private val MUTED = Color.rgb(0x9a, 0xa1, 0xb4)
        private val ACCENT = Color.rgb(0xf2, 0xc9, 0x4c)
        private val FOCUS_RING = Color.WHITE
    }
}
