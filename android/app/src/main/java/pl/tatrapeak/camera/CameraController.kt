package pl.tatrapeak.camera

import android.content.Context
import android.graphics.Matrix
import android.hardware.camera2.CameraCharacteristics as CC
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.TotalCaptureResult
import android.os.Build
import androidx.camera.camera2.interop.Camera2CameraInfo
import androidx.camera.camera2.interop.Camera2Interop
import androidx.camera.camera2.interop.ExperimentalCamera2Interop
import androidx.camera.core.*
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import java.io.Closeable
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.hypot

data class LensChoice(val cameraId: String, val physicalId: String?, val focalMm: Float,
    val equivalentMm: Double, val characteristics: CC, val sensorCoordinatesCompatible: Boolean)
data class CaptureMetadata(val timestampNs: Long, val focalMm: Float?, val crop: String,
    val activePhysicalId: String?, val videoStabilization: Int?, val opticalStabilization: Int?)
data class CameraFrame(val timestampNs: Long, val width: Int, val height: Int,
    val crop: ImageRect, val rotation: Int, val sensorToBuffer: Transform2,
    val lens: LensChoice, val capture: CaptureMetadata?)

@androidx.annotation.OptIn(ExperimentalCamera2Interop::class)
class CameraController(private val context: Context, private val owner: LifecycleOwner,
    private val previewView: PreviewView,
    private val onFrame: (CameraFrame) -> Unit, private val onStatus: (String) -> Unit) : Closeable {
    private val main = ContextCompat.getMainExecutor(context)
    private val worker = Executors.newSingleThreadExecutor()
    private var provider: ProcessCameraProvider? = null
    @Volatile private var closed = false
    private val metadata = ArrayDeque<CaptureMetadata>()
    private var lastFrame = Long.MIN_VALUE

    fun start() {
        previewView.scaleType = PreviewView.ScaleType.FIT_CENTER
        previewView.implementationMode = PreviewView.ImplementationMode.COMPATIBLE
        val future = ProcessCameraProvider.getInstance(context)
        future.addListener({
            if (closed) return@addListener
            try {
                val p = future.get()
                provider = p
                val lens = chooseLens(p)
                val selector = CameraSelector.Builder().addCameraFilter { infos ->
                    infos.filter { Camera2CameraInfo.from(it).cameraId == lens.cameraId }
                }.build()
                val rotation = previewView.display.rotation
                val pb = Preview.Builder().setTargetRotation(rotation)
                val ab = ImageAnalysis.Builder().setTargetRotation(rotation)
                    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                configure(Camera2Interop.Extender(pb), lens)
                configure(Camera2Interop.Extender(ab), lens)
                Camera2Interop.Extender(pb).setSessionCaptureCallback(object : CameraCaptureSession.CaptureCallback() {
                    override fun onCaptureCompleted(session: CameraCaptureSession, request: CaptureRequest, result: TotalCaptureResult) {
                        val timestamp = result.get(CaptureResult.SENSOR_TIMESTAMP) ?: return
                        val active = if (Build.VERSION.SDK_INT >= 29) result.get(CaptureResult.LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID) else null
                        val physical = if (Build.VERSION.SDK_INT >= 28 && lens.physicalId != null)
                            result.physicalCameraResults[lens.physicalId] else null
                        val r = physical ?: result
                        val item = CaptureMetadata(timestamp, r.get(CaptureResult.LENS_FOCAL_LENGTH),
                            r.get(CaptureResult.SCALER_CROP_REGION).toString(), active,
                            r.get(CaptureResult.CONTROL_VIDEO_STABILIZATION_MODE), r.get(CaptureResult.LENS_OPTICAL_STABILIZATION_MODE))
                        synchronized(metadata) {
                            metadata.addLast(item)
                            while (metadata.size > 60) metadata.removeFirst()
                        }
                    }
                })
                val preview = pb.build().also { it.setSurfaceProvider(previewView.surfaceProvider) }
                val analysis = ab.build()
                analysis.setAnalyzer(worker) { image ->
                    try {
                        val time = image.imageInfo.timestamp
                        if (closed || (lastFrame != Long.MIN_VALUE && time - lastFrame < 125_000_000L)) return@setAnalyzer
                        lastFrame = time
                        val crop = image.cropRect
                        val capture = synchronized(metadata) { metadata.firstOrNull { it.timestampNs == time } }
                        val snapshot = CameraFrame(time, image.width, image.height,
                            ImageRect(crop.left.toDouble(), crop.top.toDouble(), crop.right.toDouble(), crop.bottom.toDouble()),
                            image.imageInfo.rotationDegrees, fromMatrix(image.imageInfo.sensorToBufferTransformMatrix), lens, capture)
                        main.execute { if (!closed) onFrame(snapshot) }
                    } catch (e: Exception) {
                        main.execute { if (!closed) onStatus("Klatka odrzucona: ${e.message}") }
                    } finally { image.close() }
                }
                val viewport = previewView.viewPort ?: error("PreviewView nie ma jeszcze rozmiaru")
                val group = UseCaseGroup.Builder().setViewPort(viewport).addUseCase(preview).addUseCase(analysis).build()
                val camera = p.bindToLifecycle(owner, selector, group)
                camera.cameraControl.setZoomRatio(1f).addListener({
                    if (!closed) onStatus("Zoom 1× zażądany; wybór głównego obiektywu wymaga odbioru na Galaxy S10e")
                }, main)
                onStatus("cameraId=${lens.cameraId}, physical=${lens.physicalId}, f=${lens.focalMm} mm, eq≈${lens.equivalentMm} mm")
            } catch (e: Exception) { onStatus("Kamera niedostępna: ${e.message}") }
        }, main)
    }

    private fun <T> configure(ext: Camera2Interop.Extender<T>, lens: LensChoice) {
        if (Build.VERSION.SDK_INT >= 28 && lens.physicalId != null) ext.setPhysicalCameraId(lens.physicalId)
        ext.setCaptureRequestOption(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_OFF)
        ext.setCaptureRequestOption(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE_OFF)
        ext.setCaptureRequestOption(CaptureRequest.LENS_FOCAL_LENGTH, lens.focalMm)
    }

    private fun chooseLens(p: ProcessCameraProvider): LensChoice {
        val manager = context.getSystemService(CameraManager::class.java)
        val choices = mutableListOf<LensChoice>()
        for (info in p.availableCameraInfos) {
            val c2 = Camera2CameraInfo.from(info)
            val logical = manager.getCameraCharacteristics(c2.cameraId)
            if (logical.get(CC.LENS_FACING) != CC.LENS_FACING_BACK) continue
            val physicalIds = if (Build.VERSION.SDK_INT >= 28) logical.physicalCameraIds else emptySet()
            val ids = if (physicalIds.isEmpty()) listOf<String?>(null) else physicalIds.map { it }
            for (physicalId in ids) {
                val c = if (physicalId == null) logical else manager.getCameraCharacteristics(physicalId)
                val size = c.get(CC.SENSOR_INFO_PHYSICAL_SIZE) ?: continue
                for (f in c.get(CC.LENS_INFO_AVAILABLE_FOCAL_LENGTHS) ?: continue) {
                    val equivalent = f * hypot(36.0, 24.0) / hypot(size.width.toDouble(), size.height.toDouble())
                    if (equivalent.isFinite()) choices += LensChoice(c2.cameraId, physicalId, f, equivalent, c,
                        c.get(CC.SENSOR_INFO_ACTIVE_ARRAY_SIZE) == logical.get(CC.SENSOR_INFO_ACTIVE_ARRAY_SIZE))
                }
            }
        }
        // Lens class heuristic, NOT a camera ID assumption. Reported as unverified until device acceptance.
        return choices.filter { it.equivalentMm in 20.0..35.0 }
            .minByOrNull { abs(it.equivalentMm - 26.0) }
            ?: error("Brak jednoznacznego kandydata głównego obiektywu 1× (20–35 mm eq)")
    }

    override fun close() {
        closed = true
        provider?.unbindAll()
        worker.shutdown()
    }
    companion object {
        fun fromMatrix(m: Matrix): Transform2 {
            val values = FloatArray(9)
            m.getValues(values)
            return Transform2.of(values.map { it.toDouble() }.toDoubleArray())
        }
    }
}
