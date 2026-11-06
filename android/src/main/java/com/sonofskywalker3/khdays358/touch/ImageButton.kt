/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file (LICENSE-MPL-2.0.txt beside it), You can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Ported from Cemu Android's input overlay (https://github.com/SSimco/Cemu,
 * src/android/app/src/main/java/info/cemu/cemu/emulation/inputoverlay/inputs/Button.kt, RoundButton.kt and
 * RectangleButton.kt) and changed for this game: pictures in place of drawn shapes, a small reach around it.
 */
package com.sonofskywalker3.khdays358.touch

import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.RectF
import android.view.MotionEvent
import kotlin.math.min

/** A picture that holds a button while the finger that landed on it stays down. */
class ImageButton(
    private val art: ControlArt,
    private val face: ControlArt.Shape,
    private val label: LabelPicture,
    private val reach: Float,
    private val onState: (Boolean) -> Unit,
    rect: Rect,
) : Input(rect) {
    private var state = false
    private var currentPointerId = -1
    private val labelBox = RectF()

    init {
        configure()
    }

    override fun configure() {
        val width = min(rect.height() * label.share * label.aspect, rect.width() * LABEL_WIDTH)
        val height = width / label.aspect
        val cx = rect.exactCenterX()
        val cy = rect.exactCenterY()
        labelBox.set(cx - width / 2f, cy - height / 2f, cx + width / 2f, cy + height / 2f)
    }

    override fun onTouch(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                if (isInside(event.getX(i), event.getY(i))) {
                    currentPointerId = event.getPointerId(i)
                    updateState(true)
                    return true
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                if (event.getPointerId(event.actionIndex) == currentPointerId) {
                    currentPointerId = -1
                    updateState(false)
                    return true
                }
            }
        }
        return false
    }

    override fun reset() {
        currentPointerId = -1
        if (state) {
            updateState(false)
        }
    }

    private fun updateState(state: Boolean) {
        this.state = state
        onState(state)
    }

    override fun drawInput(canvas: Canvas) {
        art.draw(canvas, face, rect, state)
        label.draw(canvas, labelBox)
    }

    override fun isInside(x: Float, y: Float): Boolean {
        val r = rect.height() * reach
        return x >= rect.left - r && x < rect.right + r && y >= rect.top - r && y < rect.bottom + r
    }

    private companion object {
        const val LABEL_WIDTH = 0.84f
    }
}
