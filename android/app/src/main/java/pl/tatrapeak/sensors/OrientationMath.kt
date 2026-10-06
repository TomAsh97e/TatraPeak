package pl.tatrapeak.sensors

import kotlin.math.*

/** Rotations operate on column vectors; row-major storage. Platform axis adapter only. */
class Rotation3(val v: DoubleArray) {
    init { require(v.size == 9 && v.all { it.isFinite() }) }
    operator fun times(b: Rotation3) = Rotation3(DoubleArray(9) { i ->
        (0..2).sumOf { k -> v[i / 3 * 3 + k] * b.v[k * 3 + i % 3] }
    })
    fun transpose() = Rotation3(DoubleArray(9) { v[it % 3 * 3 + it / 3] })
    fun quaternion(): Quaternion {
        val trace = v[0] + v[4] + v[8]
        val q = if (trace > 0) {
            val s = sqrt(trace + 1) * 2
            Quaternion(s/4, (v[7]-v[5])/s, (v[2]-v[6])/s, (v[3]-v[1])/s)
        } else if (v[0] > v[4] && v[0] > v[8]) {
            val s = sqrt(1+v[0]-v[4]-v[8])*2
            Quaternion((v[7]-v[5])/s, s/4, (v[1]+v[3])/s, (v[2]+v[6])/s)
        } else if (v[4] > v[8]) {
            val s = sqrt(1+v[4]-v[0]-v[8])*2
            Quaternion((v[2]-v[6])/s, (v[1]+v[3])/s, s/4, (v[5]+v[7])/s)
        } else {
            val s = sqrt(1+v[8]-v[0]-v[4])*2
            Quaternion((v[3]-v[1])/s, (v[2]+v[6])/s, (v[5]+v[7])/s, s/4)
        }
        val n = sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z)
        require(n > 0 && n.isFinite())
        return Quaternion(q.w/n, q.x/n, q.y/n, q.z/n)
    }
    companion object {
        fun z(degrees: Double): Rotation3 {
            val c = cos(Math.toRadians(degrees)); val s = sin(Math.toRadians(degrees))
            return Rotation3(doubleArrayOf(c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0))
        }
    }
}
data class Quaternion(val w: Double, val x: Double, val y: Double, val z: Double)
data class EulerDiagnostics(val heading: Double?, val pitch: Double, val roll: Double?)

object OrientationMath {
    // D: Android device (+x right, +y top, +z out of screen).
    // B: rear camera aligned to natural portrait (+x right, +y down, +z into scene).
    private val rearFromDevice = Rotation3(doubleArrayOf(1.0,0.0,0.0, 0.0,-1.0,0.0, 0.0,0.0,-1.0))
    fun cameraFromEnu(magneticFromDevice: Rotation3, declinationDegrees: Double,
        sensorOrientation: Int, bufferRotation: Int): Rotation3 {
        require(sensorOrientation in listOf(0,90,180,270) && bufferRotation in listOf(0,90,180,270))
        // C <- raw buffer <- natural rear camera <- device <- magnetic ENU <- true ENU.
        // Positive declination: magnetic north is east of true north.
        return Rotation3.z(bufferRotation.toDouble()) * Rotation3.z(-sensorOrientation.toDouble()) *
            rearFromDevice * magneticFromDevice.transpose() * Rotation3.z(declinationDegrees)
    }
    fun diagnostics(cameraFromEnu: Rotation3): EulerDiagnostics {
        val r = cameraFromEnu.v
        val pitch = asin(r[8].coerceIn(-1.0, 1.0))
        if (hypot(r[6], r[7]) < 1e-6) return EulerDiagnostics(null, Math.toDegrees(pitch), null)
        val h = atan2(r[6], r[7])
        val rightDot = r[0]*cos(h) - r[1]*sin(h)
        val downDot = r[0]*sin(h)*sin(pitch) + r[1]*cos(h)*sin(pitch) - r[2]*cos(pitch)
        return EulerDiagnostics((Math.toDegrees(h)+360)%360, Math.toDegrees(pitch), Math.toDegrees(atan2(downDot, rightDot)))
    }
}

data class RotationSample(val timestampNs: Long, val magneticFromDevice: Rotation3, val accuracy: Int)
class RotationBuffer(private val capacity: Int = 256) {
    init { require(capacity > 0) }
    private val samples = ArrayDeque<RotationSample>()
    @Synchronized fun add(sample: RotationSample) {
        if (sample.timestampNs < 0 || (samples.lastOrNull()?.timestampNs ?: -1) >= sample.timestampNs) return
        samples.addLast(sample)
        while (samples.size > capacity) samples.removeFirst()
    }
    @Synchronized fun nearest(timestampNs: Long, toleranceNs: Long = 25_000_000): RotationSample? {
        require(timestampNs >= 0 && toleranceNs >= 0)
        return samples.minByOrNull { abs(it.timestampNs - timestampNs) }
            ?.takeIf { abs(it.timestampNs - timestampNs) <= toleranceNs }
    }
    @Synchronized fun clear() = samples.clear()
}
