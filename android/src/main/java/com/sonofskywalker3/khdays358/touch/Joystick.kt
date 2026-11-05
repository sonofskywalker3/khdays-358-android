/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file (LICENSE-MPL-2.0.txt beside it), You can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Ported from Cemu Android's input overlay (https://github.com/SSimco/Cemu,
 * src/android/app/src/main/java/info/cemu/cemu/emulation/inputoverlay/inputs/Joystick.kt) and changed for
 * this game: pictures in place of drawn shapes, a push from where the thumb lands, its middle following the thumb.
 */
package com.sonofskywalker3.khdays358.touch

import android.graphics.Canvas
import android.graphics.Rect
import android.view.MotionEvent
import kotlin.math.min
import kotlin.math.sqrt

/**
 * The stick: a thumb landing on it pushes it that way at once, and its middle follows a thumb gone past its edge,
 * so turning back answers at once. It takes no finger where `free` says no. x and y run from -1 to 1, y growing downwards.
 */
class Joystick(
    private val art: ControlArt,
    private val free: (Float, Float) -> Boolean,
    private val onStick: (held: Boolean, x: Float, y: Float) -> Unit,
    rect: Rect,
) : Input(rect) {
    private var currentPointerId = -1
    private var pressed = false
    private var centerX = 0f
    private var centerY = 0f
    private var radius = 0f
    private var innerX = 0f
    private var innerY = 0f

    init {
        configure()
    }

    private fun updateState(pressed: Boolean, x: Float, y: Float) {
        this.pressed = pressed
        onStick(pressed, x, y)
        innerX = centerX + radius * x
        innerY = centerY + radius * y
    }

    override fun configure() {
        radius = min(rect.width(), rect.height()) * 0.5f
        rest()
    }

    private fun rest() {
        centerX = rect.exactCenterX()
        centerY = rect.exactCenterY()
        innerX = centerX
        innerY = centerY
    }

    override fun onTouch(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                if (currentPointerId == -1 && isInside(event.getX(i), event.getY(i))) {
                    currentPointerId = event.getPointerId(i)
                    pushTo(event.getX(i), event.getY(i))
                    return true
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                if (currentPointerId == event.getPointerId(event.actionIndex)) {
                    currentPointerId = -1
                    rest()
                    updateState(false, 0f, 0f)
                    return true
                }
            }

            MotionEvent.ACTION_MOVE -> {
                val i = if (currentPointerId == -1) -1 else event.findPointerIndex(currentPointerId)
                if (i >= 0) {
                    pushTo(event.getX(i), event.getY(i))
                    return true
                }
            }
        }
        return false
    }

    private fun pushTo(fingerX: Float, fingerY: Float) {
        var x = (fingerX - centerX) / radius
        var y = (fingerY - centerY) / radius
        val norm = sqrt(x * x + y * y)
        if (norm > 1f) {
            centerX += x * radius * (1f - 1f / norm)
            centerY += y * radius * (1f - 1f / norm)
            x /= norm
            y /= norm
        }
        updateState(true, x, y)
    }

    override fun reset() {
        if (currentPointerId != -1 || pressed) {
            currentPointerId = -1
            rest()
            updateState(false, 0f, 0f)
        }
    }

    override fun drawInput(canvas: Canvas) {
        art.drawStick(canvas, centerX, centerY, radius, innerX, innerY, pressed)
    }

    /** Where the stick takes a finger in play: anywhere else is the camera's. */
    override fun isInside(x: Float, y: Float) = isOnBox(x, y) && free(x, y)

    /** Its own box, for moving it about. */
    fun isOnBox(x: Float, y: Float) = rect.contains(x.toInt(), y.toInt())
}
