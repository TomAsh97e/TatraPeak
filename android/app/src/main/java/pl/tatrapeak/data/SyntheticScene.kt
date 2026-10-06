package pl.tatrapeak.data

import pl.tatrapeak.engine.*

/** Same inputs as tests/integration/mobile_c_api_test.c; no geographic algorithms here. */
object SyntheticScene {
    const val DATASET = "synthetic-c-api-v1"
    fun grid() = TerrainGrid(2, 2, 0.1, -0.1, -0.2, 0.2,
        FloatArray(4), ByteArray(4) { 255.toByte() })
    fun peaks() = listOf(Peak("visible", "Visible", 0.01, 0.0, 1000.0, 999.0))
    fun query() = FrameQuery(123456L, 0.0, 0.0, 1000.0, true,
        0.7071067811865476, 0.7071067811865476, 0.0, 0.0,
        1280, 720, 1000.0, 1000.0, 640.0, 360.0)
    fun createEngine() = NativeEngine.create(grid(), peaks())
    fun verify(result: FrameResult) {
        val p = result.observations.single()
        check(p.id == "visible" && p.name == "Visible")
        check(p.displayElevation == 999.0)
        check(p.projection == ProjectionState.InFrame)
        check(kotlin.math.abs(checkNotNull(p.pixel).u - 640.0) < 0.001)
        check(p.visibility == VisibilityState.VisibleInModel)
        check(result.diagnostics.catalogCount == 1L && result.diagnostics.projectedCount == 1L)
    }
}
