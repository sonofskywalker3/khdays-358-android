/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file (LICENSE-MPL-2.0.txt beside it), You can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Ported from Cemu Android's input overlay (https://github.com/SSimco/Cemu,
 * src/android/app/src/main/java/info/cemu/cemu/emulation/inputoverlay/inputs/DPad.kt) and changed for this
 * game: a picture in place of drawn shapes, the DS's buttons' bits in place of its own.
 */
package com.sonofskywalker3.khdays358.touch

import android.graphics.Canvas
import android.graphics.Rect
import android.view.MotionEvent
import kotlin.math.atan2
import kotlin.math.min

/** The cross: a finger goes on with whatever direction is under it. */
class DPad(
    private val art: ControlArt,
    private val onState: (bits: Int) -> Unit,
    rect: Rect,
) : Input(rect) {
    private var dpadState = 0
    private var centerX = 0f
    private var centerY = 0f
    private var radius2 = 0f
    private var currentPointerId = -1

    init {
        configure()
    }

    override fun configure() {
        centerX = rect.exactCenterX()
        centerY = rect.exactCenterY()
        val radius = min(rect.width(), rect.height()) * 0.5f
        radius2 = radius * radius
    }

    private fun updateState(next: Int) {
        if (next != dpadState) {
            dpadState = next
            onState(next)
        }
    }

    private fun stateAt(x: Float, y: Float): Int {
        val dx = x - centerX
        val dy = y - centerY
        if (dx * dx + dy * dy <= radius2 * 0.1f) {
            return 0
        }
        return when (atan2(dy, dx).toDouble()) {
            in -0.875 * Math.PI..<-0.625 * Math.PI -> DsButton.UP or DsButton.LEFT
            in -0.625 * Math.PI..<-0.375 * Math.PI -> DsButton.UP
            in -0.375 * Math.PI..<-0.125 * Math.PI -> DsButton.UP or DsButton.RIGHT
            in -0.125 * Math.PI..<0.125 * Math.PI -> DsButton.RIGHT
            in 0.125 * Math.PI..<0.375 * Math.PI -> DsButton.DOWN or DsButton.RIGHT
            in 0.375 * Math.PI..<0.625 * Math.PI -> DsButton.DOWN
            in 0.625 * Math.PI..<0.875 * Math.PI -> DsButton.DOWN or DsButton.LEFT
            else -> DsButton.LEFT
        }
    }

    override fun onTouch(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                if (currentPointerId == -1 && isInside(event.getX(i), event.getY(i))) {
                    currentPointerId = event.getPointerId(i)
                    updateState(stateAt(event.getX(i), event.getY(i)))
                    return true
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                if (event.getPointerId(event.actionIndex) == currentPointerId) {
                    currentPointerId = -1
                    updateState(0)
                    return true
                }
            }

            MotionEvent.ACTION_MOVE -> {
                val i = if (currentPointerId == -1) -1 else event.findPointerIndex(currentPointerId)
                if (i >= 0) {
                    updateState(stateAt(event.getX(i), event.getY(i)))
                    return true
                }
            }
        }
        return false
    }

    override fun reset() {
        currentPointerId = -1
        updateState(0)
    }

    override fun isInside(x: Float, y: Float): Boolean {
        val dx = x - centerX
        val dy = y - centerY
        return dx * dx + dy * dy <= radius2
    }

    override fun drawInput(canvas: Canvas) {
        art.drawDpad(canvas, rect, dpadState)
    }
}
