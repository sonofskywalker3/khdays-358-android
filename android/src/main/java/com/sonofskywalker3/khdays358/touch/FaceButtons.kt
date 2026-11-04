package com.sonofskywalker3.khdays358.touch

import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.RectF
import android.view.MotionEvent
import kotlin.math.hypot

/** The DS's buttons, each the bit port/host/app/profile.h's KhdaysAction gives it. */
object DsButton {
    const val A = 1 shl 0
    const val B = 1 shl 1
    const val SELECT = 1 shl 2
    const val START = 1 shl 3
    const val RIGHT = 1 shl 4
    const val LEFT = 1 shl 5
    const val UP = 1 shl 6
    const val DOWN = 1 shl 7
    const val R = 1 shl 8
    const val L = 1 shl 9
    const val X = 1 shl 10
    const val Y = 1 shl 11
}

/**
 * A, B, X and Y as one control: any finger on them goes on with whatever is under it, so a thumb slides from one to
 * the next without lifting, and between two it holds both.
 */
class FaceButtons(
    private val art: ControlArt,
    private val letters: Array<LabelPicture>,
    private val onState: (bits: Int) -> Unit,
    rect: Rect,
) : Input(rect) {
    private val fingers = HashMap<Int, Int>() // a pointer's id, and the bits it holds
    private val centres = FloatArray(BUTTONS * 2)
    private var radius = 0f
    private var bits = 0
    private val box = Rect()
    private val letterBox = RectF()

    init {
        configure()
    }

    override fun configure() {
        radius = rect.width() * RADIUS_OF_WIDTH
        val away = rect.height() / 2f - radius
        val cx = rect.exactCenterX()
        val cy = rect.exactCenterY()
        floatArrayOf(cx + away, cy, cx, cy + away, cx, cy - away, cx - away, cy).copyInto(centres)
    }

    private fun publish() {
        var now = 0
        fingers.values.forEach { now = now or it }
        if (now != bits) {
            bits = now
            onState(now)
        }
    }

    override fun onTouch(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                if (isInside(event.getX(i), event.getY(i))) {
                    fingers[event.getPointerId(i)] = faceBitsAt(event.getX(i), event.getY(i), centres, radius)
                    publish()
                    return true
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                if (fingers.remove(event.getPointerId(event.actionIndex)) != null) {
                    publish()
                    return true
                }
            }

            MotionEvent.ACTION_MOVE -> {
                if (fingers.isEmpty()) {
                    return false
                }
                for (i in 0 until event.pointerCount) {
                    val id = event.getPointerId(i)
                    if (fingers.containsKey(id)) {
                        fingers[id] = faceBitsAt(event.getX(i), event.getY(i), centres, radius)
                    }
                }
                publish()
                return true
            }
        }
        return false
    }

    override fun reset() {
        fingers.clear()
        publish()
    }

    override fun isInside(x: Float, y: Float) =
        x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom && faceBitsAt(x, y, centres, radius) != 0

    override fun drawInput(canvas: Canvas) {
        for (b in 0 until BUTTONS) {
            val x = centres[2 * b]
            val y = centres[2 * b + 1]
            box.set((x - radius).toInt(), (y - radius).toInt(), (x + radius).toInt(), (y + radius).toInt())
            art.draw(canvas, ControlArt.Shape.ROUND, box, (bits and BITS[b]) != 0)
            letterBox.set(x - radius * LETTER, y - radius * LETTER, x + radius * LETTER, y + radius * LETTER)
            letters[b].draw(canvas, letterBox)
        }
    }

    companion object {
        private const val BUTTONS = 4
        private const val RADIUS_OF_WIDTH = 0.17f
        private const val LETTER = 0.62f
        private const val BOTH_WITHIN = 1.35f // a second button, when it is this much further than the nearest
        private const val REACH = 1.55f       // of a button's radius: still that button
        private const val FAR = 1.6f          // the box's far corners are nobody's
        private val BITS = intArrayOf(DsButton.A, DsButton.B, DsButton.X, DsButton.Y)

        /** The bits of the button or two under a point; `centres` holds x and y of A, B, X, Y. */
        fun faceBitsAt(x: Float, y: Float, centres: FloatArray, radius: Float): Int {
            val d = FloatArray(BUTTONS) { hypot(x - centres[2 * it], y - centres[2 * it + 1]) }
            var nearest = 0
            for (i in 1 until BUTTONS) {
                if (d[i] < d[nearest]) {
                    nearest = i
                }
            }
            if (d[nearest] > radius * REACH * FAR) {
                return 0
            }
            var bits = BITS[nearest]
            for (i in 0 until BUTTONS) {
                if (i != nearest && d[i] < d[nearest] * BOTH_WITHIN && d[i] < radius * REACH) {
                    bits = bits or BITS[i]
                }
            }
            return bits
        }
    }
}
