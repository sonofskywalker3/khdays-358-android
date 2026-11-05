/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file (LICENSE-MPL-2.0.txt beside it), You can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Ported from Cemu Android's input overlay (https://github.com/SSimco/Cemu,
 * src/android/app/src/main/java/info/cemu/cemu/emulation/inputoverlay/InputOverlaySurfaceView.kt) and changed
 * for this game: a plain view, no Wii U controllers, the fingers that miss every control handed to SDL.
 */
package com.sonofskywalker3.khdays358.touch

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Rect
import android.os.VibrationEffect
import android.os.Vibrator
import android.util.DisplayMetrics
import android.view.MotionEvent
import android.view.View
import org.libsdl.app.SDLActivity
import kotlin.math.abs
import kotlin.math.max

/**
 * The on-screen controls, a plain view over the game's surface, drawn and answering on Android's own thread however
 * slowly the game draws. A window's second finger goes to whichever view has its first, whatever it lands on, so
 * this one takes every finger while it shows and hands those that miss its controls to SDL itself, one by one: to
 * the game they are touches as any other (the camera, the command deck, a movie's tap, the layout editor).
 */
@SuppressLint("ViewConstructor")
class TouchOverlayView(context: Context) : View(context) {
    private var mode = TouchOverlay.MODE_NONE
    private var edit = 0
    private var style = -1
    private var opacity = -1
    private var vibration = false
    private var gameWidth = 1f
    private var gameHeight = 1f
    private val boxes = FloatArray((TouchOverlay.CONTROLS + TouchOverlay.DECK_ROWS) * 4)
    private var art: ControlArt? = null
    private var selectWord: Bitmap? = null
    private var startWord: Bitmap? = null

    private val inputs = ArrayList<Pair<Int, Input>>()
    private val forwarded = HashSet<Int>()
    private var forwardedDevice = 0

    private var faceBits = 0
    private var dpadBits = 0
    private var otherBits = 0
    private var lockOn = false
    private var stickHeld = false
    private var stickX = 0f
    private var stickY = 0f
    private var gear = false

    private var saidBits = 0
    private var saidLock = false
    private var saidHeld = false
    private var saidX = 0f
    private var saidY = 0f

    private var editing: Pair<Int, Input>? = null
    private var editingPointer = -1
    private var editingFrom = Rect()
    private var lastX = 0f
    private var lastY = 0f

    private val vibrator: Vibrator? = context.getSystemService(Vibrator::class.java)
    private val tick = VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK)
    private val minSize = (MIN_SIZE_DP * resources.displayMetrics.densityDpi.toFloat() / DisplayMetrics.DENSITY_DEFAULT).toInt()

    init {
        visibility = GONE
        isFocusable = false
        setBackgroundColor(Color.TRANSPARENT)
    }

    fun labels(select: Bitmap, start: Bitmap) {
        selectWord = select
        startWord = start
        rebuild()
        invalidate()
    }

    fun show(
        mode: Int, edit: Int, style: Int, opacity: Int, vibration: Boolean, width: Float, height: Float, boxes: FloatArray,
    ) {
        this.vibration = vibration
        if (mode == this.mode && edit == this.edit && style == this.style && opacity == this.opacity &&
            width == gameWidth && height == gameHeight && boxes.contentEquals(this.boxes)
        ) {
            return
        }
        if (style != this.style || opacity != this.opacity || art == null) {
            art = ControlArt(context, style, opacity * OPAQUE / PERCENT)
        }
        letGo(mode != this.mode || edit != this.edit)
        this.mode = mode
        this.edit = edit
        this.style = style
        this.opacity = opacity
        gameWidth = width
        gameHeight = height
        boxes.copyInto(this.boxes)
        visibility = if (mode == TouchOverlay.MODE_NONE) GONE else VISIBLE
        rebuild()
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        rebuild()
    }

    /** Whatever was held is let go, and the game told so. `fingers`: those handed to SDL too. */
    private fun letGo(fingers: Boolean) {
        gear = false // let go, not tapped: its release opens nothing
        inputs.forEach { it.second.reset() }
        faceBits = 0
        dpadBits = 0
        otherBits = 0
        lockOn = false
        stickHeld = false
        stickX = 0f
        stickY = 0f
        editing = null
        editingPointer = -1
        if (fingers) {
            forwarded.forEach { SDLActivity.onNativeTouch(forwardedDevice, it, MotionEvent.ACTION_CANCEL, 0f, 0f, 0f) }
            forwarded.clear()
        }
        publish()
    }

    private fun box(control: Int): Rect {
        val kx = width / gameWidth
        val ky = height / gameHeight
        val i = control * 4
        return Rect((boxes[i] * kx).toInt(), (boxes[i + 1] * ky).toInt(), (boxes[i + 2] * kx).toInt(), (boxes[i + 3] * ky).toInt())
    }

    private fun onDeck(x: Float, y: Float): Boolean {
        for (r in 0 until TouchOverlay.DECK_ROWS) {
            val b = box(TouchOverlay.CONTROLS + r)
            if (b.width() > 0 && b.contains(x.toInt(), y.toInt())) {
                return true
            }
        }
        return false
    }

    private fun button(control: Int, art: ControlArt, shape: ControlArt.Shape, label: LabelPicture, onState: (Boolean) -> Unit) =
        control to ImageButton(art, shape, label, REACH, onState, box(control))

    private fun holds(bit: Int): (Boolean) -> Unit = { on ->
        otherBits = if (on) otherBits or bit else otherBits and bit.inv()
    }

    private fun rebuild() {
        val art = art
        letGo(false) // the fingers on the old controls have nothing to let go of on the new
        inputs.clear()
        if (art == null || width == 0 || mode == TouchOverlay.MODE_NONE) {
            return
        }
        val selectLabel = LabelPicture(selectWord, art.wordPaint, WORD_SHARE)
        val startLabel = LabelPicture(startWord, art.wordPaint, WORD_SHARE)
        val start = button(TouchOverlay.START, art, ControlArt.Shape.WIDE, startLabel, holds(DsButton.START))
        if (mode == TouchOverlay.MODE_TALK) {
            inputs += start
            return
        }
        inputs += TouchOverlay.FACE to FaceButtons(
            art,
            arrayOf(art.icon("button_a"), art.icon("button_b"), art.icon("button_x"), art.icon("button_y")),
            { bits -> faceBits = bits },
            box(TouchOverlay.FACE),
        )
        inputs += button(TouchOverlay.L, art, ControlArt.Shape.WIDE, art.icon("button_l"), holds(DsButton.L))
        inputs += button(TouchOverlay.R, art, ControlArt.Shape.WIDE, art.icon("button_r"), holds(DsButton.R))
        inputs += button(TouchOverlay.SELECT, art, ControlArt.Shape.WIDE, selectLabel, holds(DsButton.SELECT))
        inputs += start
        inputs += button(TouchOverlay.GEAR, art, ControlArt.Shape.ROUND, art.icon("cog")) { on ->
            if (gear && !on) {
                TouchOverlay.nativeGear()
            }
            gear = on
        }
        if (mode == TouchOverlay.MODE_MENU || mode == TouchOverlay.MODE_EVENT) {
            inputs += TouchOverlay.DPAD to DPad(art, { bits -> dpadBits = bits }, box(TouchOverlay.DPAD))
            return
        }
        inputs += button(TouchOverlay.LOCK, art, ControlArt.Shape.ROUND, art.icon("target")) { on -> lockOn = on }
        val stickBox = box(TouchOverlay.STICK)
        inputs += TouchOverlay.STICK to Joystick(
            art,
            { x, y -> !onDeck(x, y) },
            { held, x, y ->
                stickHeld = held
                stickX = x
                stickY = y
            },
            stickBox,
        )
        if (edit != 0) {
            inputs.forEach { it.second.showBoundingRectangle(OUTLINE) }
        }
    }

    /** Says what is held now, when that changed; a press buzzes. */
    private fun publish() {
        val bits = faceBits or dpadBits or otherBits
        val pressed = (bits and saidBits.inv()) != 0 || (lockOn && !saidLock)
        if (bits == saidBits && lockOn == saidLock && stickHeld == saidHeld && stickX == saidX && stickY == saidY) {
            return
        }
        saidBits = bits
        saidLock = lockOn
        saidHeld = stickHeld
        saidX = stickX
        saidY = stickY
        if (pressed && vibration) {
            vibrator?.takeIf { it.hasVibrator() }?.vibrate(tick)
        }
        TouchOverlay.nativeHeld(bits, stickHeld, stickX, stickY, lockOn)
        invalidate()
    }

    private fun forward(event: MotionEvent, index: Int, action: Int) {
        forwardedDevice = event.deviceId
        SDLActivity.onNativeTouch(
            event.deviceId, event.getPointerId(index), action,
            event.getX(index) / max(width, 1), event.getY(index) / max(height, 1), event.getPressure(index).coerceAtMost(1f),
        )
    }

    private fun moveForwarded(event: MotionEvent) {
        for (i in 0 until event.pointerCount) {
            if (event.getPointerId(i) in forwarded) {
                forward(event, i, MotionEvent.ACTION_MOVE)
            }
        }
    }

    private fun cancel(event: MotionEvent) {
        for (i in 0 until event.pointerCount) {
            if (forwarded.remove(event.getPointerId(i))) {
                forward(event, i, MotionEvent.ACTION_CANCEL)
            }
        }
        gear = false
        inputs.forEach { it.second.reset() }
    }

    private fun onPlay(event: MotionEvent) {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                if (inputs.none { it.second.onTouch(event) }) {
                    val i = event.actionIndex
                    forwarded += event.getPointerId(i)
                    forward(event, i, event.actionMasked)
                }
            }

            MotionEvent.ACTION_MOVE -> {
                inputs.forEach { it.second.onTouch(event) }
                moveForwarded(event)
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val i = event.actionIndex
                if (forwarded.remove(event.getPointerId(i))) {
                    forward(event, i, event.actionMasked)
                } else {
                    inputs.forEach { it.second.onTouch(event) }
                }
            }

            MotionEvent.ACTION_CANCEL -> cancel(event)
        }
    }

    /** Moving or resizing a control with a finger; a finger on anything else is the layout editor's, in the game. */
    private fun onEdit(event: MotionEvent) {
        val i = event.actionIndex
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = event.getX(i)
                val y = event.getY(i)
                val under = if (editing != null) {
                    null
                } else {
                    inputs.firstOrNull { (_, input) -> if (input is Joystick) input.isOnBox(x, y) else input.isInside(x, y) }
                }
                if (under != null) {
                    editing = under
                    editingFrom = under.second.boundingRectangle()
                    editingPointer = event.getPointerId(i)
                    lastX = x
                    lastY = y
                    under.second.showBoundingRectangle(if (edit == EDIT_SIZE) OUTLINE_SIZE else OUTLINE_MOVE)
                    invalidate()
                } else {
                    forwarded += event.getPointerId(i)
                    forward(event, i, event.actionMasked)
                }
            }

            MotionEvent.ACTION_MOVE -> {
                val input = editing?.second
                val p = if (input == null) -1 else event.findPointerIndex(editingPointer)
                if (input != null && p >= 0) {
                    val dx = (event.getX(p) - lastX).toInt()
                    val dy = (event.getY(p) - lastY).toInt()
                    if (edit == EDIT_SIZE) {
                        val diff = if (abs(dx) > abs(dy)) dx else dy
                        if (diff != 0) {
                            input.resizeBy(diff, width, height, minSize)
                            lastX = event.getX(p)
                            lastY = event.getY(p)
                        }
                    } else if (dx != 0 || dy != 0) {
                        input.moveBy(dx, dy, width, height)
                        lastX += dx
                        lastY += dy
                    }
                    invalidate()
                }
                moveForwarded(event)
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = event.getPointerId(i)
                if (forwarded.remove(id)) {
                    forward(event, i, event.actionMasked)
                } else if (id == editingPointer) {
                    place()
                }
            }

            MotionEvent.ACTION_CANCEL -> {
                place()
                cancel(event)
            }
        }
    }

    /** Whether a box of this size fits here, in the view and on no control but `except`. */
    private fun freeAt(box: Rect, except: Input): Boolean =
        box.left >= 0 && box.top >= 0 && box.right <= width && box.bottom <= height &&
            inputs.none { it.second !== except && Rect.intersects(box, it.second.boundingRectangle()) }

    /** A control out from under another, where a thumb would press both: to `prefer`, else the nearest free place. */
    private fun makeWay(input: Input, prefer: Rect): Boolean {
        val from = input.boundingRectangle()
        val try0 = Rect(prefer.left, prefer.top, prefer.left + from.width(), prefer.top + from.height())
        if (freeAt(try0, input)) {
            input.moveTo(try0.left, try0.top)
            return true
        }
        var best: Rect? = null
        var bestDistance = Long.MAX_VALUE
        val step = max(minSize / 4, 1)
        // touching another's side too: a gap of exactly its size lies on none of the steps
        val others = inputs.filter { it.second !== input }.map { it.second.boundingRectangle() }
        val lefts = (0..(width - from.width()) step step).toList() + others.flatMap { listOf(it.left - from.width(), it.right) }
        val tops = (0..(height - from.height()) step step).toList() + others.flatMap { listOf(it.top - from.height(), it.bottom) }
        for (top in tops) {
            for (left in lefts) {
                val box = Rect(left, top, left + from.width(), top + from.height())
                if (freeAt(box, input)) {
                    val dx = (left - from.left).toLong()
                    val dy = (top - from.top).toLong()
                    val distance = dx * dx + dy * dy
                    if (distance < bestDistance) {
                        bestDistance = distance
                        best = box
                    }
                }
            }
        }
        val found = best ?: return false
        input.moveTo(found.left, found.top)
        return true
    }

    /** The control let go: those under it make way. With no room for one, it goes back and nothing else moves. */
    private fun settle(input: Input): List<Pair<Int, Input>> {
        val before = inputs.map { it.second to it.second.boundingRectangle() }
        val moved = mutableListOf<Pair<Int, Input>>()
        for ((control, other) in inputs) {
            if (other !== input && Rect.intersects(input.boundingRectangle(), other.boundingRectangle())) {
                if (!makeWay(other, editingFrom)) {
                    before.forEach { (i, box) -> i.restore(box) }
                    val from = editingFrom
                    val size = input.boundingRectangle()
                    if (size.width() == from.width()) {
                        input.restore(from)
                    } else {
                        input.restore(Rect(size.left, size.top, size.left + from.width(), size.top + from.height()))
                    }
                    return emptyList()
                }
                moved += control to other
            }
        }
        return moved
    }

    /** The control let go: the game keeps its new box, in its own pixels. */
    private fun place() {
        val (control, input) = editing ?: return
        val moved = settle(input)
        val r = input.boundingRectangle()
        val kx = gameWidth / max(width, 1)
        val ky = gameHeight / max(height, 1)
        input.showBoundingRectangle(OUTLINE)
        editing = null
        editingPointer = -1
        TouchOverlay.nativeControlBox(control, r.left * kx, r.top * ky, r.right * kx, r.bottom * ky)
        for ((other, input2) in moved) {
            val b = input2.boundingRectangle()
            TouchOverlay.nativeControlBox(other, b.left * kx, b.top * ky, b.right * kx, b.bottom * ky)
        }
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (edit != 0) {
            onEdit(event)
        } else {
            onPlay(event)
            publish()
        }
        return true
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        inputs.forEach { it.second.draw(canvas) }
    }

    private companion object {
        const val MIN_SIZE_DP = 28
        const val OPAQUE = 255
        const val PERCENT = 100
        const val REACH = 0.12f        // the small controls' hit grown by this share of their height
        const val WORD_SHARE = 0.42f   // Select and Start: of the button's height
        const val EDIT_SIZE = 2
        val OUTLINE = Color.argb(200, 90, 220, 255)
        val OUTLINE_MOVE = Color.argb(255, 255, 210, 60)
        val OUTLINE_SIZE = Color.argb(255, 255, 90, 90)
    }
}
