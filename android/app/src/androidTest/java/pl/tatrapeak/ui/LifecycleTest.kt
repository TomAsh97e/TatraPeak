package pl.tatrapeak.ui

import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.Assert.*
import androidx.test.platform.app.InstrumentationRegistry
import pl.tatrapeak.data.DiagnosticRecorder
import pl.tatrapeak.data.SyntheticScene

@RunWith(AndroidJUnit4::class)
class LifecycleTest {
    @Test fun canRepeatedlyOpenRecreateAndCloseDiagnosticScreen() {
        repeat(10) {
            ActivityScenario.launch(MainActivity::class.java).use { scenario -> scenario.recreate() }
        }
    }

    @Test fun writesDesktopReplaySession() {
        val root = InstrumentationRegistry.getInstrumentation().targetContext.cacheDir.resolve("recorder-test")
        root.deleteRecursively(); root.mkdirs()
        val recorder = DiagnosticRecorder(root)
        recorder.start(); recorder.record(SyntheticScene.query())
        val session = checkNotNull(recorder.stop())
        assertTrue(session.resolve("session.json").readText().contains("camera_from_enu").not())
        assertTrue(session.resolve("poses.json").readText().contains("camera_from_enu"))
        assertTrue(session.resolve("frames.json").readText().contains("timestamp_ns"))
        root.deleteRecursively()
    }
}
