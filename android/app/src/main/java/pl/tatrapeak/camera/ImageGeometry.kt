package pl.tatrapeak.camera

import kotlin.math.abs

data class Point2(val x: Double, val y: Double)
data class ImageRect(val left: Double, val top: Double, val right: Double, val bottom: Double) {
    val width get() = right - left
    val height get() = bottom - top
}

/** Row-major homogeneous matrix; column vectors. (a * b)(p) = a(b(p)). */
class Transform2 private constructor(val values: DoubleArray) {
    operator fun times(b: Transform2) = of(DoubleArray(9) { i ->
        (0..2).sumOf { k -> values[i / 3 * 3 + k] * b.values[k * 3 + i % 3] }
    })
    fun point(x: Double, y: Double): Point2 {
        val w = values[6] * x + values[7] * y + values[8]
        require(abs(w) > 1e-12)
        return Point2((values[0] * x + values[1] * y + values[2]) / w,
            (values[3] * x + values[4] * y + values[5]) / w)
    }
    fun inverse(): Transform2 {
        val a = values
        val cof = doubleArrayOf(a[4]*a[8]-a[5]*a[7], a[2]*a[7]-a[1]*a[8], a[1]*a[5]-a[2]*a[4],
            a[5]*a[6]-a[3]*a[8], a[0]*a[8]-a[2]*a[6], a[2]*a[3]-a[0]*a[5],
            a[3]*a[7]-a[4]*a[6], a[1]*a[6]-a[0]*a[7], a[0]*a[4]-a[1]*a[3])
        val det = a[0]*cof[0] + a[1]*cof[3] + a[2]*cof[6]
        require(abs(det) > 1e-12) { "Singular image transform" }
        return of(cof.map { it / det }.toDoubleArray())
    }
    companion object {
        fun of(v: DoubleArray): Transform2 {
            require(v.size == 9 && v.all { it.isFinite() })
            return Transform2(v.copyOf())
        }
        fun scaleTranslate(sx: Double, sy: Double, tx: Double, ty: Double) =
            of(doubleArrayOf(sx, 0.0, tx, 0.0, sy, ty, 0.0, 0.0, 1.0))
        fun upright(crop: ImageRect, rotation: Int): Transform2 {
            require(crop.width > 0 && crop.height > 0)
            val translate = scaleTranslate(1.0, 1.0, -crop.left, -crop.top)
            val rotate = when (rotation) {
                0 -> scaleTranslate(1.0, 1.0, 0.0, 0.0)
                90 -> of(doubleArrayOf(0.0, -1.0, crop.height, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0))
                180 -> of(doubleArrayOf(-1.0, 0.0, crop.width, 0.0, -1.0, crop.height, 0.0, 0.0, 1.0))
                270 -> of(doubleArrayOf(0.0, 1.0, 0.0, -1.0, 0.0, crop.width, 0.0, 0.0, 1.0))
                else -> error("Only quarter-turn rotations are supported")
            }
            return rotate * translate
        }
        fun fitCenter(crop: ImageRect, rotation: Int, viewWidth: Int, viewHeight: Int): Transform2 {
            require(viewWidth > 0 && viewHeight > 0)
            val w = if (rotation % 180 == 0) crop.width else crop.height
            val h = if (rotation % 180 == 0) crop.height else crop.width
            val s = minOf(viewWidth / w, viewHeight / h)
            return scaleTranslate(s, s, (viewWidth - s*w)/2, (viewHeight - s*h)/2) * upright(crop, rotation)
        }
    }
}
