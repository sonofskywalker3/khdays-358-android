/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file (LICENSE-MPL-2.0.txt beside it), You can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Ported from Cemu Android's input overlay (https://github.com/SSimco/Cemu,
 * src/android/app/src/main/java/info/cemu/cemu/emulation/inputoverlay/inputs/Input.kt) and changed for
 * this game: moved by the finger's own motion, resized with its shape kept, no colours of its own.
 */
package com.sonofskywalker3.khdays358.touch

import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.view.MotionEvent
import kotlin.math.max
import kotlin.math.min

/** One control: its box in the view's pixels, moved and resized as a whole. */
abstract class Input protected constructor(protected var rect: Rect) {
    private var drawBoundingRectangle = false
    private val boundingRectanglePaint = Paint().apply {
        style = Paint.Style.STROKE
        strokeWidth = 3f
    }

    fun boundingRectangle() = Rect(rect)

    abstract fun onTouch(event: MotionEvent): Boolean

    /**
     * The box moved by this much, staying inside the view and stopping at `others`: across, then down, so that it
     * slides along what it meets. One already over another (from an older layout) can leave it, and is not held by it.
     */
    fun moveBy(dx: Int, dy: Int, maxWidth: Int, maxHeight: Int, others: List<Rect>) {
        val width = rect.width()
        val height = rect.height()
        var left = min(max(rect.left + dx, 0), maxWidth - width)
        var top = min(max(rect.top + dy, 0), maxHeight - height)
        val clear = others.filterNot { Rect.intersects(rect, it) }
        for (o in clear) {
            if (rect.top < o.bottom && rect.bottom > o.top) {
                if (rect.right <= o.left && left + width > o.left) {
                    left = o.left - width
                } else if (rect.left >= o.right && left < o.right) {
                    left = o.right
                }
            }
        }
        for (o in clear) {
            if (left < o.right && left + width > o.left) {
                if (rect.bottom <= o.top && top + height > o.top) {
                    top = o.top - height
                } else if (rect.top >= o.bottom && top < o.bottom) {
                    top = o.bottom
                }
            }
        }
        rect = Rect(left, top, left + width, top + height)
        configure()
    }

    /** The box grown by this much at its far corner, its shape kept, staying inside the view and off `others`. */
    fun resizeBy(diff: Int, maxWidth: Int, maxHeight: Int, minSize: Int, others: List<Rect>) {
        val newWidth = rect.width() + diff
        val newHeight = rect.height() + diff * rect.height() / max(rect.width(), 1)
        if (min(newWidth, newHeight) < minSize || rect.left + newWidth > maxWidth || rect.top + newHeight > maxHeight) {
            return
        }
        val grown = Rect(rect.left, rect.top, rect.left + newWidth, rect.top + newHeight)
        if (diff > 0 && others.any { !Rect.intersects(rect, it) && Rect.intersects(grown, it) }) {
            return
        }
        rect = grown
        configure()
    }

    /** Nothing held any more. */
    abstract fun reset()

    protected abstract fun configure()

    protected abstract fun drawInput(canvas: Canvas)

    fun draw(canvas: Canvas) {
        if (drawBoundingRectangle) {
            canvas.drawRect(rect, boundingRectanglePaint)
        }
        drawInput(canvas)
    }

    fun showBoundingRectangle(color: Int?) {
        drawBoundingRectangle = color != null
        if (color != null) {
            boundingRectanglePaint.color = color
        }
    }

    abstract fun isInside(x: Float, y: Float): Boolean
}
