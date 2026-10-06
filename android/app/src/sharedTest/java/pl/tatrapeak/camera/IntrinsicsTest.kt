package pl.tatrapeak.camera

import org.junit.Assert.*
import org.junit.Test

class IntrinsicsTest {
    @Test fun physicalFallbackUsesPixelPitchAndActiveCenter() {
        val k = Intrinsics.physical(4.0, 8.0, 6.0, 4000, 3000,
            ImageRect(10.0, 20.0, 3990.0, 2980.0))
        assertEquals(2000.0, k.fx, 1e-9); assertEquals(2000.0, k.fy, 1e-9)
        assertEquals(2000.0, k.cx, 1e-9); assertEquals(1500.0, k.cy, 1e-9)
        assertTrue(k.source.contains("FALLBACK"))
    }
    @Test fun cropScaleAndRotationTransformPrincipalPointAndSwapFocalLengths() {
        val k = Intrinsics(1000.0, 800.0, 600.0, 400.0, "test")
            .scaleToBuffer(Transform2.scaleTranslate(0.5, 0.25, -50.0, -25.0))
        assertEquals(500.0, k.fx, 1e-9); assertEquals(200.0, k.fy, 1e-9)
        assertEquals(250.0, k.cx, 1e-9); assertEquals(75.0, k.cy, 1e-9)
        val c = ImageRect(10.0, 20.0, 410.0, 220.0)
        val r = k.upright(c, 90)
        assertEquals(200.0, r.fx, 1e-9); assertEquals(500.0, r.fy, 1e-9)
        assertEquals(145.0, r.cx, 1e-9); assertEquals(240.0, r.cy, 1e-9)
        val r180 = k.upright(c, 180)
        assertEquals(160.0, r180.cx, 1e-9); assertEquals(145.0, r180.cy, 1e-9)
        val r270 = k.upright(c, 270)
        assertEquals(55.0, r270.cx, 1e-9); assertEquals(160.0, r270.cy, 1e-9)
    }
    @Test fun rejectsMissingMetadataAndUnsupportedSkew() {
        assertThrows(IllegalArgumentException::class.java) {
            Intrinsics.physical(0.0, 8.0, 6.0, 4000, 3000, ImageRect(0.0, 0.0, 4000.0, 3000.0))
        }
        assertThrows(IllegalArgumentException::class.java) {
            Intrinsics(1000.0, 1000.0, 600.0, 400.0, "test").scaleToBuffer(
                Transform2.of(doubleArrayOf(1.0, 0.1, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)))
        }
    }
}
