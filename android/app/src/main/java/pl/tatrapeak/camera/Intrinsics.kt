package pl.tatrapeak.camera

import kotlin.math.abs

data class Intrinsics(val fx: Double, val fy: Double, val cx: Double, val cy: Double,
    val source: String) {
    init { require(listOf(fx, fy, cx, cy).all { it.isFinite() } && fx > 0 && fy > 0) }
    fun scaleToBuffer(sensorToBuffer: Transform2): Intrinsics {
        val m = sensorToBuffer.values
        require(abs(m[1]) < 1e-6 && abs(m[3]) < 1e-6 && abs(m[6]) < 1e-9 && abs(m[7]) < 1e-9 && abs(m[8]-1) < 1e-9) {
            "Sensor→buffer zawiera obrót/skew; oczekiwany surowy bufor ImageAnalysis"
        }
        require(m[0] > 0 && m[4] > 0)
        val center = sensorToBuffer.point(cx, cy)
        return copy(fx = fx*m[0], fy = fy*m[4], cx = center.x, cy = center.y)
    }
    fun upright(crop: ImageRect, rotation: Int): Intrinsics {
        val p = Transform2.upright(crop, rotation).point(cx, cy)
        return copy(fx = if (rotation % 180 == 0) fx else fy,
            fy = if (rotation % 180 == 0) fy else fx, cx = p.x, cy = p.y)
    }
    companion object {
        fun physical(focalMm: Double, sensorWidthMm: Double, sensorHeightMm: Double,
            pixelWidth: Int, pixelHeight: Int, active: ImageRect): Intrinsics {
            require(focalMm > 0 && sensorWidthMm > 0 && sensorHeightMm > 0 && pixelWidth > 0 && pixelHeight > 0)
            return Intrinsics(focalMm * pixelWidth / sensorWidthMm, focalMm * pixelHeight / sensorHeightMm,
                (active.left+active.right)/2, (active.top+active.bottom)/2,
                "FALLBACK: ogniskowa/rozmiar sensora/pixel array; środek active array; przybliżenie")
        }
    }
}
