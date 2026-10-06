package pl.tatrapeak.camera

import org.junit.Assert.*
import org.junit.Test

class ImageGeometryTest {
    private fun point(t: Transform2, x: Double, y: Double, ex: Double, ey: Double) {
        val p = t.point(x, y)
        assertEquals(ex, p.x, 1e-9); assertEquals(ey, p.y, 1e-9)
    }
    @Test fun fitCenterHasLetterboxAndMapsCornersAndCenter() {
        val t = Transform2.fitCenter(ImageRect(0.0, 0.0, 1280.0, 720.0), 0, 1000, 1000)
        point(t, 0.0, 0.0, 0.0, 218.75)
        point(t, 1280.0, 720.0, 1000.0, 781.25)
        point(t, 640.0, 360.0, 500.0, 500.0)
    }
    @Test fun cropAndAllQuarterTurnsUseIndependentCornerExpectations() {
        val c = ImageRect(10.0, 20.0, 210.0, 120.0)
        val expected = listOf(0 to Point2(0.0, 0.0), 90 to Point2(100.0, 0.0),
            180 to Point2(200.0, 100.0), 270 to Point2(0.0, 200.0))
        for ((rotation, p) in expected) {
            val t = Transform2.upright(c, rotation)
            point(t, 10.0, 20.0, p.x, p.y)
            point(t.inverse() * t, 83.0, 51.0, 83.0, 51.0)
        }
    }
    @Test fun sensorToViewTimesInverseSensorToBufferMapsSameSensorPoint() {
        val sensorToBuffer = Transform2.scaleTranslate(0.5, 0.25, -50.0, -25.0)
        val sensorToView = Transform2.scaleTranslate(0.2, 0.2, 30.0, 80.0)
        val bufferToView = sensorToView * sensorToBuffer.inverse()
        // Sensor (300, 500) -> buffer (100, 100) and view (90, 180).
        point(bufferToView, 100.0, 100.0, 90.0, 180.0)
    }
}
