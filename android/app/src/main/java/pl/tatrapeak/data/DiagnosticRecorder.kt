package pl.tatrapeak.data

import org.json.JSONArray
import org.json.JSONObject
import pl.tatrapeak.engine.FrameQuery
import java.io.File

/** Writes the existing T16 replay format. UI thread only; capped to a short session. */
class DiagnosticRecorder(private val filesDir: File) {
    private val queries = ArrayList<FrameQuery>()
    var recording = false
        private set
    fun start() { queries.clear(); recording = true }
    fun record(query: FrameQuery) {
        if (!recording || queries.size >= 100 || queries.lastOrNull()?.timestampNs ?: -1 >= query.timestampNs) return
        if (queries.firstOrNull()?.let { query.timestampNs - it.timestampNs > 10_000_000_000L } == true) return
        queries += query
    }
    fun stop(): File? {
        if (!recording) return null
        recording = false
        val first = queries.firstOrNull() ?: return null
        val directory = File(filesDir, "sessions/${System.currentTimeMillis()}").apply { mkdirs() }
        val options = JSONObject().put("min_range_m", first.minRange).put("max_range_m", first.maxRange)
            .put("near_z_m", first.nearZ).put("visibility", JSONObject()
                .put("sample_step_m", first.sampleStep).put("clearance_band_m", first.clearanceBand)
                .put("start_guard_m", first.startGuard).put("target_guard_m", first.targetGuard)
                .put("max_samples", first.maxSamples))
        val session = JSONObject().put("schema_version", 1)
            .put("observer", JSONObject().put("latitude_deg", first.latitude).put("longitude_deg", first.longitude)
                .put("height", JSONObject().put("mode", "ellipsoid").put("metres", first.ellipsoidHeight))
                .put("simulated", first.simulated))
            .put("camera", JSONObject().put("width_px", first.width).put("height_px", first.height)
                .put("fx_px", first.fx).put("fy_px", first.fy).put("cx_px", first.cx).put("cy_px", first.cy))
            .put("options", options).put("max_gap_ns", 1_000_000_000L)
            .put("poses_file", "poses.json").put("frames_file", "frames.json")
        val poses = JSONArray(); val frames = JSONArray()
        queries.forEach { q ->
            poses.put(JSONObject().put("timestamp_ns", q.timestampNs).put("camera_from_enu",
                JSONObject().put("w", q.qw).put("x", q.qx).put("y", q.qy).put("z", q.qz)))
            frames.put(JSONObject().put("timestamp_ns", q.timestampNs))
        }
        File(directory, "session.json").writeText(session.toString(2))
        File(directory, "poses.json").writeText(poses.toString(2))
        File(directory, "frames.json").writeText(frames.toString(2))
        queries.clear()
        return directory
    }
}
