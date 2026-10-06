package pl.tatrapeak.engine

import org.junit.Assert.*
import org.junit.Test
import pl.tatrapeak.data.SyntheticScene
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** Runs unchanged on the Linux JVM and on arm64 Android; always loads real JNI. */
class NativeEngineTest {
    @Test fun matchesCIntegrationFixtureAndOwnsInputCopies() {
        val grid = SyntheticScene.grid()
        val peaks = SyntheticScene.peaks().toMutableList()
        val result = NativeEngine.create(grid, peaks).use { engine ->
            grid.heightsEllipsoid.fill(2000f)
            grid.mask.fill(0)
            peaks[0] = peaks[0].copy(id = "Xisible", name = "Changed")
            assertEquals(1, NativeEngine.abiVersion)
            assertTrue(NativeEngine.version.isNotBlank())
            engine.evaluate(SyntheticScene.query()).also(SyntheticScene::verify)
        }
        // All returned text and numeric values remain valid after native destruction.
        SyntheticScene.verify(result)
        assertEquals(360.08726646, result.observations.single().pixel!!.v, 0.001)
        assertTrue(result.diagnostics.visibilitySampleCount > 0)
    }

    @Test fun invalidQuaternionMapsCoreStatusAndEngineRemainsUsable() {
        SyntheticScene.createEngine().use { engine ->
            val error = assertThrows(NativeEngineException::class.java) {
                engine.evaluate(SyntheticScene.query().copy(qw = 0.0, qx = 0.0))
            }
            assertEquals(NativeStatus.InvalidArgument, error.status)
            assertTrue(error.message!!.contains("quaternion"))
            SyntheticScene.verify(engine.evaluate(SyntheticScene.query()))
        }
    }

    @Test fun rejectsBadCreateAndArrayLengthsWithoutCrashing() {
        assertThrows(IllegalArgumentException::class.java) {
            NativeEngine.create(SyntheticScene.grid().copy(rows = Int.MAX_VALUE), SyntheticScene.peaks())
        }
        assertThrows(NativeEngineException::class.java) {
            NativeEngine.create(SyntheticScene.grid(), SyntheticScene.peaks() + SyntheticScene.peaks())
        }
        assertThrows(IllegalArgumentException::class.java) {
            NativeBindings.create(2, 2, DoubleArray(3), FloatArray(4), ByteArray(4), emptyArray(), DoubleArray(0))
        }
    }

    @Test fun unicodeAndOptionalFieldsSurviveJni() {
        val peak = SyntheticScene.peaks().single().copy(id = "żółć🏔", name = "Łomnica 🏔️", displayElevation = null)
        NativeEngine.create(SyntheticScene.grid(), listOf(peak)).use { engine ->
            val result = engine.evaluate(SyntheticScene.query()).observations.single()
            assertEquals(peak.id, result.id)
            assertEquals(peak.name, result.name)
            assertNull(result.displayElevation)
            assertNull(result.firstBlockerDistance)
        }
        assertThrows(NativeEngineException::class.java) {
            NativeEngine.create(SyntheticScene.grid(), listOf(peak, peak))
        }
    }

    @Test fun projectionAndVisibilityStatesAreNotCollapsed() {
        SyntheticScene.createEngine().use { engine ->
            val behind = engine.evaluate(SyntheticScene.query().copy(nearZ = 5000.0)).observations.single()
            assertEquals(ProjectionState.BehindNearPlane, behind.projection)
            assertNull(behind.pixel)
            assertEquals(VisibilityState.NotEvaluated, behind.visibility)
            val outside = engine.evaluate(SyntheticScene.query().copy(cx = -1000.0)).observations.single()
            assertEquals(ProjectionState.OutsideFrame, outside.projection)
        }
        for ((height, mask, expected) in listOf(
            Triple(2000f, 255, VisibilityState.OccludedInModel),
            Triple(1000f, 255, VisibilityState.Uncertain),
            Triple(0f, 0, VisibilityState.MissingTerrain))) {
            // A 2000 m ridge halfway along the 1000 m sight line, with both endpoints
            // above zero terrain. A flat 2000 m DEM would put the observer underground
            // and is correctly Uncertain rather than evidence of an occluding ridge.
            val grid = if (expected == VisibilityState.OccludedInModel)
                TerrainGrid(5, 2, 0.02, -0.1, -0.005, 0.2,
                    floatArrayOf(0f,0f, 0f,0f, 0f,0f, 2000f,2000f, 0f,0f), ByteArray(10) { 255.toByte() })
            else SyntheticScene.grid().copy(heightsEllipsoid = FloatArray(4) { height }, mask = ByteArray(4) { mask.toByte() })
            NativeEngine.create(grid, SyntheticScene.peaks()).use { engine ->
                assertEquals(expected, engine.evaluate(SyntheticScene.query()).observations.single().visibility)
            }
        }
    }

    @Test fun repeatedCloseAndEvaluateAfterCloseAreSafe() {
        repeat(200) {
            val engine = SyntheticScene.createEngine()
            SyntheticScene.verify(engine.evaluate(SyntheticScene.query()))
            engine.close()
            engine.close()
            assertThrows(IllegalStateException::class.java) { engine.evaluate(SyntheticScene.query()) }
        }
        NativeBindings.destroy(0)
        NativeBindings.destroy(Long.MAX_VALUE)
        assertThrows(IllegalStateException::class.java) {
            val q = SyntheticScene.query()
            NativeBindings.evaluate(Long.MAX_VALUE, 0, true, q.width, q.height, q.maxSamples, q.packed())
        }
    }

    @Test fun concurrentEvaluateAndCloseAreSerialized() {
        val pool = Executors.newFixedThreadPool(2)
        try {
            repeat(30) {
                val engine = SyntheticScene.createEngine()
                val evaluation = pool.submit {
                    try { SyntheticScene.verify(engine.evaluate(SyntheticScene.query())) }
                    catch (e: IllegalStateException) { assertEquals("NativeEngine is closed", e.message) }
                }
                val close = pool.submit { engine.close() }
                evaluation.get(5, TimeUnit.SECONDS)
                close.get(5, TimeUnit.SECONDS)
                engine.close()
            }
        } finally { pool.shutdownNow() }
    }

    @Test fun emptyCatalogProducesEmptyOwnedResult() {
        NativeEngine.create(SyntheticScene.grid(), emptyList()).use { engine ->
            val result = engine.evaluate(SyntheticScene.query())
            assertTrue(result.observations.isEmpty())
            assertEquals(0L, result.diagnostics.catalogCount)
        }
    }
}
