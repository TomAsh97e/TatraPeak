package pl.tatrapeak.camera

import android.hardware.camera2.CameraCharacteristics as CC
import kotlin.math.abs

object CameraIntrinsics {
    fun sensor(frame: CameraFrame): Intrinsics {
        require(frame.lens.sensorCoordinatesCompatible) {
            "Fizyczny i logiczny active array różnią się — brak zweryfikowanego mapowania intrinsics"
        }
        val c = frame.lens.characteristics
        val active = c.get(CC.SENSOR_INFO_ACTIVE_ARRAY_SIZE) ?: error("Brak active array")
        val calibration = c.get(CC.LENS_INTRINSIC_CALIBRATION)
        if (calibration != null) {
            require(calibration.size == 5 && abs(calibration[4]) < 1e-5) { "Skew kalibracji nieobsługiwany przez pinhole C ABI" }
            val pre = c.get(CC.SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE) ?: error("Brak preCorrection active array")
            // Calibration origin is the top-left of preCorrectionActiveArray. CameraX maps
            // absolute active-array sensor coordinates. Lens distortion is not modeled by C ABI.
            return Intrinsics(calibration[0].toDouble(), calibration[1].toDouble(),
                calibration[2] + pre.left.toDouble(), calibration[3] + pre.top.toDouble(),
                "LENS_INTRINSIC_CALIBRATION; pinhole przybliżony — dystorsja/ISP niezweryfikowane")
        }
        val size = c.get(CC.SENSOR_INFO_PHYSICAL_SIZE) ?: error("Brak fizycznego rozmiaru sensora")
        val pixels = c.get(CC.SENSOR_INFO_PIXEL_ARRAY_SIZE) ?: error("Brak pixel array")
        val focal = frame.capture?.focalMm ?: frame.lens.focalMm
        return Intrinsics.physical(focal.toDouble(), size.width.toDouble(), size.height.toDouble(),
            pixels.width, pixels.height,
            ImageRect(active.left.toDouble(), active.top.toDouble(), active.right.toDouble(), active.bottom.toDouble()))
    }
}
