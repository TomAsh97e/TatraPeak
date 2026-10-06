package pl.tatrapeak.sensors

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.sqrt

class OrientationTest {
    // Columns independently describe device axes in world ENU, not generated from the adapter.
    private val north = Rotation3(doubleArrayOf(1.0,0.0,0.0, 0.0,0.0,-1.0, 0.0,1.0,0.0))
    @Test fun northPortraitHasExpectedCameraAxesAndQuaternion() {
        val c = OrientationMath.cameraFromEnu(north, 0.0, 90, 90)
        assertArrayEquals(doubleArrayOf(1.0,0.0,0.0, 0.0,0.0,-1.0, 0.0,1.0,0.0), c.v, 1e-9)
        val q = c.quaternion()
        assertEquals(sqrt(0.5), q.w, 1e-9); assertEquals(sqrt(0.5), q.x, 1e-9)
        assertEquals(0.0, q.y, 1e-9); assertEquals(0.0, q.z, 1e-9)
        assertEquals(0.0, OrientationMath.diagnostics(c).heading!!, 1e-9)
    }
    @Test fun eastMapsEastToForwardNorthToLeftAndUpToTop() {
        val device = Rotation3(doubleArrayOf(0.0,0.0,-1.0, -1.0,0.0,0.0, 0.0,1.0,0.0))
        val c = OrientationMath.cameraFromEnu(device, 0.0, 90, 90)
        assertArrayEquals(doubleArrayOf(0.0,-1.0,0.0, 0.0,0.0,-1.0, 1.0,0.0,0.0), c.v, 1e-9)
        assertEquals(90.0, OrientationMath.diagnostics(c).heading!!, 1e-9)
    }
    @Test fun zenithHasUndefinedHeadingAndRoll() {
        val device = Rotation3(doubleArrayOf(1.0,0.0,0.0, 0.0,-1.0,0.0, 0.0,0.0,-1.0))
        val c = OrientationMath.cameraFromEnu(device, 0.0, 270, 270)
        assertArrayEquals(doubleArrayOf(1.0,0.0,0.0, 0.0,1.0,0.0, 0.0,0.0,1.0), c.v, 1e-9)
        val d = OrientationMath.diagnostics(c)
        assertNull(d.heading); assertNull(d.roll); assertEquals(90.0, d.pitch, 1e-9)
    }
    @Test fun landscapeDeviceAxesAndDisplayRotationCancel() {
        // Natural device x is up, y is west, z is south. Display rotation=90,
        // sensorOrientation=90 -> CameraX buffer rotation=0.
        val landscape = Rotation3(doubleArrayOf(0.0,-1.0,0.0, 0.0,0.0,-1.0, 1.0,0.0,0.0))
        val c = OrientationMath.cameraFromEnu(landscape, 0.0, 90, 0)
        assertArrayEquals(doubleArrayOf(1.0,0.0,0.0, 0.0,0.0,-1.0, 0.0,1.0,0.0), c.v, 1e-9)
    }
    @Test fun positiveDeclinationMovesMagneticNorthEastOfTrueNorth() {
        val c = OrientationMath.cameraFromEnu(north, 10.0, 90, 90)
        assertEquals(10.0, OrientationMath.diagnostics(c).heading!!, 1e-9)
    }
    @Test fun rollDiagnosticUsesTheSameSignAsCppHeadingPitchRoll() {
        val s = sqrt(0.5)
        val device = Rotation3(doubleArrayOf(s,s,0.0, 0.0,0.0,-1.0, -s,s,0.0))
        val c = OrientationMath.cameraFromEnu(device, 0.0, 90, 90)
        assertEquals(45.0, OrientationMath.diagnostics(c).roll!!, 1e-9)
    }
    @Test fun bufferRejectsOldMissingAndTooDistantSamples() {
        val b = RotationBuffer(2)
        b.add(RotationSample(10, north, 3)); b.add(RotationSample(20, north, 3))
        b.add(RotationSample(30, north, 3)); b.add(RotationSample(15, north, 3))
        assertNull(b.nearest(10, 0)); assertEquals(20L, b.nearest(22, 2)!!.timestampNs)
        assertNull(b.nearest(100, 25)); b.clear(); assertNull(b.nearest(30))
    }
}
