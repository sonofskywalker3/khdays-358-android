package com.sonofskywalker3.khdays358.touch

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.PorterDuffColorFilter
import android.graphics.Rect
import android.graphics.RectF
import java.io.IOException

/** A label over a control: a letter's picture, or a word in the game's own font (TouchOverlay.labels). */
class LabelPicture(private val bitmap: Bitmap?, private val paint: Paint, val share: Float = LETTER_SHARE) {
    val aspect: Float get() = if (bitmap != null && bitmap.height > 0) bitmap.width.toFloat() / bitmap.height else 1f

    fun draw(canvas: Canvas, box: RectF) {
        if (bitmap != null) {
            canvas.drawBitmap(bitmap, null, box, paint)
        }
    }

    private companion object {
        const val LETTER_SHARE = 0.62f
    }
}

/** One of Kenney's Mobile Controls styles (assets/touch, CC0), at the player's opacity. */
class ControlArt(private val context: Context, style: Int, private val alpha: Int) {
    enum class Shape { ROUND, WIDE }

    private val folder = "touch/style${style.coerceIn(0, INK.size - 1) + 1}"
    private val round = load("$folder/button_circle.png")
    private val wide = load("$folder/button_square_wide.png")
    private val dpad = load("$folder/dpad.png")
    private val pad = load("$folder/joystick_circle_pad_a.png")
    private val nub = load("$folder/joystick_circle_nub_a.png")
    private val roundLit = load("touch/highlight/button_circle_highlight.png")
    private val wideLit = load("touch/highlight/button_square_wide_highlight.png")
    private val nubLit = load("touch/highlight/joystick_circle_nub_highlight.png")
    private val north = load("touch/highlight/dpad_element_north_highlight.png")
    private val south = load("touch/highlight/dpad_element_south_highlight.png")
    private val east = load("touch/highlight/dpad_element_east_highlight.png")
    private val west = load("touch/highlight/dpad_element_west_highlight.png")

    private val paint = Paint(Paint.FILTER_BITMAP_FLAG).apply { this.alpha = this@ControlArt.alpha }
    private val litPaint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val ink = INK[style.coerceIn(0, INK.size - 1)]
    private val inkPaint = Paint(Paint.FILTER_BITMAP_FLAG).apply {
        colorFilter = PorterDuffColorFilter(ink, PorterDuff.Mode.SRC_IN)
        this.alpha = this@ControlArt.alpha
    }

    /** Words in the game's own font stay crisp pixels, as on the DS. */
    val wordPaint = Paint().apply {
        colorFilter = PorterDuffColorFilter(ink, PorterDuff.Mode.SRC_IN)
        this.alpha = this@ControlArt.alpha
    }

    fun icon(name: String) = LabelPicture(load("touch/icons/icon_$name.png"), inkPaint)

    private fun load(path: String): Bitmap? = try {
        context.assets.open(path).use { BitmapFactory.decodeStream(it) }
    } catch (_: IOException) {
        null
    }

    private fun Canvas.put(bitmap: Bitmap?, box: Rect, with: Paint) {
        if (bitmap != null) {
            drawBitmap(bitmap, null, box, with)
        }
    }

    fun draw(canvas: Canvas, shape: Shape, box: Rect, lit: Boolean) {
        canvas.put(if (shape == Shape.ROUND) round else wide, box, paint)
        if (lit) {
            canvas.put(if (shape == Shape.ROUND) roundLit else wideLit, box, litPaint)
        }
    }

    private val stickBox = Rect()

    fun drawStick(canvas: Canvas, cx: Float, cy: Float, r: Float, knobX: Float, knobY: Float, held: Boolean) {
        stickBox.set((cx - r).toInt(), (cy - r).toInt(), (cx + r).toInt(), (cy + r).toInt())
        canvas.put(pad, stickBox, paint)
        val k = r * KNOB
        stickBox.set((knobX - k).toInt(), (knobY - k).toInt(), (knobX + k).toInt(), (knobY + k).toInt())
        canvas.put(nub, stickBox, paint)
        if (held) {
            canvas.put(nubLit, stickBox, litPaint)
        }
    }

    private val armBox = Rect()

    fun drawDpad(canvas: Canvas, box: Rect, bits: Int) {
        canvas.put(dpad, box, paint)
        val w = box.width() / 2
        val h = box.height() / 2
        val x = box.centerX() - w / 2
        val y = box.centerY() - h / 2
        if ((bits and DsButton.UP) != 0) {
            armBox.set(x, box.top, x + w, box.top + h)
            canvas.put(north, armBox, litPaint)
        }
        if ((bits and DsButton.DOWN) != 0) {
            armBox.set(x, box.bottom - h, x + w, box.bottom)
            canvas.put(south, armBox, litPaint)
        }
        if ((bits and DsButton.LEFT) != 0) {
            armBox.set(box.left, y, box.left + w, y + h)
            canvas.put(west, armBox, litPaint)
        }
        if ((bits and DsButton.RIGHT) != 0) {
            armBox.set(box.right - w, y, box.right, y + h)
            canvas.put(east, armBox, litPaint)
        }
    }

    private companion object {
        const val KNOB = 0.49f // the knob's radius of the pad's, as the pack draws them
        val DARK = Color.rgb(45, 50, 60)
        val LIGHT = Color.WHITE

        // in the order the Controls page numbers them: Kenney's C, B, A, H, D, E, F, G
        val INK = intArrayOf(LIGHT, DARK, LIGHT, LIGHT, Color.BLACK, LIGHT, DARK, LIGHT)
    }
}
