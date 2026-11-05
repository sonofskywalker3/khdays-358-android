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

    /** The box moved by this much, staying inside the view. */
    fun moveBy(dx: Int, dy: Int, maxWidth: Int, maxHeight: Int) {
        val width = rect.width()
        val height = rect.height()
        val left = min(max(rect.left + dx, 0), maxWidth - width)
        val top = min(max(rect.top + dy, 0), maxHeight - height)
        rect = Rect(left, top, left + width, top + height)
        configure()
    }

    /** The box's corner put here, its size kept. */
    fun moveTo(left: Int, top: Int) {
        rect = Rect(left, top, left + rect.width(), top + rect.height())
        configure()
    }

    /** The box as it was. */
    fun restore(box: Rect) {
        rect = Rect(box)
        configure()
    }

    /** The box grown by this much at its far corner, its shape kept, staying inside the view. */
    fun resizeBy(diff: Int, maxWidth: Int, maxHeight: Int, minSize: Int) {
        val newWidth = rect.width() + diff
        val newHeight = rect.height() + diff * rect.height() / max(rect.width(), 1)
        if (min(newWidth, newHeight) < minSize || rect.left + newWidth > maxWidth || rect.top + newHeight > maxHeight) {
            return
        }
        rect = Rect(rect.left, rect.top, rect.left + newWidth, rect.top + newHeight)
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
