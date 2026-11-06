package com.sonofskywalker3.khdays358.touch

import org.junit.Assert.assertEquals
import org.junit.Test

/** Which of the four buttons a point holds: a thumb on one, between two, and off them. */
class FaceButtonsTest {
    private val radius = 26f
    private val centres = floatArrayOf(100f, 0f, 0f, 100f, 0f, -100f, -100f, 0f) // A, B, X, Y

    @Test
    fun onAButtonHoldsIt() {
        assertEquals(DsButton.A, FaceButtons.faceBitsAt(100f, 0f, centres, radius))
        assertEquals(DsButton.B, FaceButtons.faceBitsAt(0f, 100f, centres, radius))
        assertEquals(DsButton.X, FaceButtons.faceBitsAt(0f, -100f, centres, radius))
        assertEquals(DsButton.Y, FaceButtons.faceBitsAt(-100f, 0f, centres, radius))
    }

    @Test
    fun betweenTwoNearbyButtonsHoldsBoth() {
        val close = floatArrayOf(30f, 0f, 0f, 30f, 0f, -30f, -30f, 0f)
        assertEquals(DsButton.A or DsButton.B, FaceButtons.faceBitsAt(17f, 17f, close, radius))
    }

    @Test
    fun farOffHoldsNothing() {
        assertEquals(0, FaceButtons.faceBitsAt(300f, 300f, centres, radius))
    }
}
