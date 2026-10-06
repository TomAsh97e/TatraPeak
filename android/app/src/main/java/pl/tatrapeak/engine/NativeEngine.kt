package pl.tatrapeak.engine

import java.io.Closeable

data class TerrainGrid(
    val rows: Int, val columns: Int,
    val firstLatitude: Double, val firstLongitude: Double,
    val latitudeStep: Double, val longitudeStep: Double,
    val heightsEllipsoid: FloatArray, val mask: ByteArray,
)

data class Peak(val id: String, val name: String, val latitude: Double,
    val longitude: Double, val ellipsoidHeight: Double, val displayElevation: Double? = null)

/** All orientation and projection inputs use the unchanged mobile.h contract. */
data class FrameQuery(
    val timestampNs: Long,
    val latitude: Double, val longitude: Double, val ellipsoidHeight: Double,
    val simulated: Boolean,
    val qw: Double, val qx: Double, val qy: Double, val qz: Double,
    val width: Int, val height: Int,
    val fx: Double, val fy: Double, val cx: Double, val cy: Double,
    val minRange: Double = 20.0, val maxRange: Double = 5000.0,
    val nearZ: Double = 0.01, val sampleStep: Double = 100.0,
    val clearanceBand: Double = 0.5, val startGuard: Double = 2.0,
    val targetGuard: Double = 30.0, val maxSamples: Long = 100,
) {
    internal fun packed() = doubleArrayOf(latitude, longitude, ellipsoidHeight,
        qw, qx, qy, qz, fx, fy, cx, cy, minRange, maxRange, nearZ,
        sampleStep, clearanceBand, startGuard, targetGuard)
}

enum class ProjectionState { InFrame, OutsideFrame, BehindNearPlane }
enum class VisibilityState { NotEvaluated, VisibleInModel, OccludedInModel, Uncertain, MissingTerrain }
data class Pixel(val u: Double, val v: Double)
data class Observation(
    val id: String, val name: String, val displayElevation: Double?,
    val azimuth: Double, val elevation: Double, val slantRange: Double,
    val projection: ProjectionState, val pixel: Pixel?, val cameraDepth: Double,
    val visibility: VisibilityState, val minSampledClearance: Double?,
    val firstBlockerDistance: Double?, val sampleCount: Long,
)
data class Diagnostics(val catalogCount: Long, val outsideRangeCount: Long,
    val projectedCount: Long, val visibilitySampleCount: Long)
data class FrameResult(val observations: List<Observation>, val diagnostics: Diagnostics)

enum class NativeStatus { InvalidArgument, BufferTooSmall, CoreError, InternalError }
class NativeEngineException(val statusCode: Int, message: String) : RuntimeException(message) {
    val status: NativeStatus get() = when (statusCode) {
        1 -> NativeStatus.InvalidArgument
        2 -> NativeStatus.BufferTooSmall
        3 -> NativeStatus.CoreError
        else -> NativeStatus.InternalError
    }
}

/** Private transport, not a dataset format or a change to the public C ABI.
 * Doubles: nine per observation; counters: four diagnostics then three per observation.
 * Text uses standard UTF-8 byte arrays, never JNI's modified UTF-8.
 */
internal class JniFrame(val text: Array<ByteArray>, val values: DoubleArray, val counters: LongArray) {
    fun decoded(): FrameResult {
        fun optional(v: Double) = v.takeUnless { it.isNaN() }
        val observations = List(text.size / 2) { i ->
            val d = i * 9
            val c = 4 + i * 3
            Observation(text[2 * i].toString(Charsets.UTF_8), text[2 * i + 1].toString(Charsets.UTF_8),
                optional(values[d]), values[d + 1], values[d + 2], values[d + 3],
                ProjectionState.entries[counters[c].toInt()],
                if (values[d + 4].isNaN()) null else Pixel(values[d + 4], values[d + 5]),
                values[d + 6], VisibilityState.entries[counters[c + 1].toInt() + 1],
                optional(values[d + 7]), optional(values[d + 8]), counters[c + 2])
        }
        return FrameResult(observations, Diagnostics(counters[0], counters[1], counters[2], counters[3]))
    }
}

internal object NativeBindings {
    init { System.loadLibrary("tatra_jni") }
    external fun abiVersion(): Int
    external fun version(): String
    external fun create(rows: Int, columns: Int, geometry: DoubleArray,
        heights: FloatArray, mask: ByteArray, text: Array<ByteArray>, peaks: DoubleArray): Long
    external fun evaluate(handle: Long, timestamp: Long, simulated: Boolean,
        width: Int, height: Int, maxSamples: Long, values: DoubleArray): JniFrame
    external fun destroy(handle: Long)
}

/** Owns exactly one native engine; evaluate and close are serialized, including decoding.
 * Use `use {}` for scoped work, or close from the owning lifecycle on its worker thread.
 */
class NativeEngine private constructor(private var handle: Long) : Closeable {
    @Synchronized
    fun evaluate(query: FrameQuery): FrameResult {
        check(handle != 0L) { "NativeEngine is closed" }
        return NativeBindings.evaluate(handle, query.timestampNs, query.simulated,
            query.width, query.height, query.maxSamples, query.packed()).decoded()
    }

    @Synchronized
    override fun close() {
        if (handle != 0L) {
            NativeBindings.destroy(handle)
            handle = 0L
        }
    }

    companion object {
        const val EXPECTED_ABI = 1
        val abiVersion get() = NativeBindings.abiVersion()
        val version get() = NativeBindings.version()
        fun create(grid: TerrainGrid, peaks: List<Peak>): NativeEngine {
            check(abiVersion == EXPECTED_ABI) { "Unsupported mobile ABI: $abiVersion" }
            val text = peaks.flatMap { listOf(it.id, it.name) }.map {
                require('\u0000' !in it) { "Peak text cannot contain NUL" }
                it.toByteArray(Charsets.UTF_8)
            }.toTypedArray()
            val positions = DoubleArray(Math.multiplyExact(peaks.size, 4))
            peaks.forEachIndexed { i, p ->
                require(p.displayElevation == null || p.displayElevation.isFinite())
                positions[4 * i] = p.latitude
                positions[4 * i + 1] = p.longitude
                positions[4 * i + 2] = p.ellipsoidHeight
                positions[4 * i + 3] = p.displayElevation ?: Double.NaN
            }
            // Allocate the owner before create, so JVM allocation failure cannot orphan a handle.
            val owner = NativeEngine(0)
            owner.handle = NativeBindings.create(grid.rows, grid.columns,
                doubleArrayOf(grid.firstLatitude, grid.firstLongitude, grid.latitudeStep, grid.longitudeStep),
                grid.heightsEllipsoid, grid.mask, text, positions)
            check(owner.handle != 0L)
            return owner
        }
    }
}
