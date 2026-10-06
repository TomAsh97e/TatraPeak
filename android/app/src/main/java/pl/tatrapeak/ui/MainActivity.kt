package pl.tatrapeak.ui

import android.os.Build
import android.os.Bundle
import android.Manifest
import android.app.AlertDialog
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.hardware.GeomagneticField
import android.content.pm.PackageManager
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.EditText
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import pl.tatrapeak.data.SyntheticScene
import pl.tatrapeak.data.MobilePackageLoader
import pl.tatrapeak.data.DiagnosticRecorder
import pl.tatrapeak.engine.NativeEngine
import pl.tatrapeak.engine.FrameEvaluator
import pl.tatrapeak.engine.FrameQuery
import pl.tatrapeak.camera.Intrinsics
import pl.tatrapeak.camera.CameraController
import pl.tatrapeak.camera.CameraFrame
import pl.tatrapeak.camera.CameraIntrinsics
import pl.tatrapeak.sensors.OrientationTracker
import pl.tatrapeak.sensors.OrientationMath
import pl.tatrapeak.location.LocationTracker
import java.util.Locale

class MainActivity : ComponentActivity() {
    private lateinit var preview: PreviewView
    private lateinit var overlay: OverlayView
    private lateinit var diagnostics: TextView
    private lateinit var diagnosticsPanel: View
    private lateinit var diagnosticsToggle: Button
    private lateinit var statusSummary: TextView
    private var camera: CameraController? = null
    private var evaluator: FrameEvaluator? = null
    private var liveEvaluation = "Evaluate bieżących wejść: oczekiwanie na kamerę, intrinsics, pozę i lokalizację"
    @Volatile private var datasetStatus = "Dataset: sprawdzanie assetów"
    private var packageAvailable = false
    private lateinit var recorder: DiagnosticRecorder
    private lateinit var recordButton: Button
    private var recordingStatus = "Sesja diagnostyczna: zatrzymana"
    private lateinit var orientation: OrientationTracker
    private lateinit var location: LocationTracker
    private var declinationDegrees: Double? = null
    private var smoke = "JNI: obliczanie…"
    private var cameraStatus = "Kamera: oczekuje na uprawnienie"
    private val permissions = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        cameraStatus = if (granted) "Kamera: uruchamianie" else "Odmowa CAMERA — test JNI nadal działa"
        updateText()
        if (granted) startCamera()
    }
    private val locationPermissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        location.useGps()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        orientation = OrientationTracker(this)
        location = LocationTracker(this) { updateText() }
        recorder = DiagnosticRecorder(filesDir)
        if (savedInstanceState?.getBoolean("manual_location") == true) {
            location.setManual(savedInstanceState.getDouble("manual_lat"), savedInstanceState.getDouble("manual_lon"),
                savedInstanceState.getDouble("manual_height"))
        }
        setContentView(buildUi())
        packageAvailable = assets.list("datasets")?.contains("tatras-v1.tpk") == true
        datasetStatus = if (packageAvailable) "Dataset: tatras-v1.tpk; walidacja nastąpi przed JNI"
            else "Dataset: ${SyntheticScene.DATASET}; brak assets/datasets/tatras-v1.tpk"
        updateText()
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                while (isActive) { delay(1000); updateText() }
            }
        }
        lifecycleScope.launch {
            val report = withContext(Dispatchers.Default) {
                try {
                    SyntheticScene.createEngine().use { engine ->
                        val start = System.nanoTime()
                        val result = engine.evaluate(SyntheticScene.query())
                        val elapsed = (System.nanoTime() - start) / 1e6
                        SyntheticScene.verify(result)
                        val peak = result.observations.single()
                        """
                            TatraPeak — JNI PASS

                            Telefon: ${Build.MANUFACTURER} ${Build.MODEL}
                            ABI urządzenia: ${Build.SUPPORTED_ABIS.joinToString()}
                            ABI silnika: ${NativeEngine.abiVersion}
                            Silnik: ${NativeEngine.version}
                            Dataset: ${SyntheticScene.DATASET}

                            ${peak.name} (${peak.id})
                            Wysokość opisowa: ${peak.displayElevation} m
                            Projekcja: ${peak.projection}
                            Piksel: ${peak.pixel}
                            Widoczność: ${peak.visibility}
                            Próbki: ${result.diagnostics.visibilitySampleCount}
                            Evaluate: ${String.format(Locale.ROOT, "%.3f", elapsed)} ms

                            Dane i poza są syntetyczne.
                            Ten test JNI korzysta wyłącznie z danych syntetycznych.
                            Kalibracja Galaxy S10e: niezweryfikowana.
                        """.trimIndent()
                    }
                } catch (error: Exception) {
                    "JNI FAIL\n${error.javaClass.simpleName}: ${error.message}"
                } catch (error: LinkageError) {
                    "JNI LOAD FAIL\n${error.message}"
                }
            }
            smoke = report
            updateText()
        }
    }

    private fun buildUi(): View {
        window.statusBarColor = Color.rgb(232, 247, 242)
        window.navigationBarColor = Color.rgb(238, 246, 255)
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or
            View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR

        preview = PreviewView(this).apply {
            scaleType = PreviewView.ScaleType.FIT_CENTER
            setBackgroundColor(Color.rgb(221, 239, 234))
        }
        overlay = OverlayView(this)
        diagnostics = TextView(this).apply {
            textSize = 12f
            setTextColor(Color.rgb(39, 70, 82))
            typeface = Typeface.MONOSPACE
            setTextIsSelectable(true)
            setPadding(dp(18), dp(16), dp(18), dp(20))
        }

        val root = FrameLayout(this).apply {
            background = GradientDrawable(GradientDrawable.Orientation.TL_BR,
                intArrayOf(Color.rgb(228, 247, 239), Color.rgb(229, 240, 255)))
            addView(preview, FrameLayout.LayoutParams(-1, -1))
            addView(this@MainActivity.overlay, FrameLayout.LayoutParams(-1, -1))
            addView(View(this@MainActivity).apply {
                background = GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                    intArrayOf(0x330B2633, Color.TRANSPARENT, 0x66102635))
            }, FrameLayout.LayoutParams(-1, -1))
        }

        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(18), dp(12), dp(14), dp(12))
            background = glassPanel()
            elevation = dp(8).toFloat()
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                addView(TextView(this@MainActivity).apply {
                    text = "TatraPeak"
                    textSize = 24f
                    typeface = Typeface.DEFAULT_BOLD
                    setTextColor(Color.rgb(24, 61, 72))
                })
                addView(TextView(this@MainActivity).apply {
                    text = "Szczyty w Twoim kadrze"
                    textSize = 12f
                    setTextColor(Color.rgb(73, 107, 118))
                })
            }, LinearLayout.LayoutParams(0, -2, 1f))
            addView(TextView(this@MainActivity).apply {
                text = "OFFLINE"
                textSize = 11f
                typeface = Typeface.DEFAULT_BOLD
                gravity = Gravity.CENTER
                setTextColor(Color.rgb(29, 111, 93))
                setPadding(dp(12), dp(7), dp(12), dp(7))
                background = rounded(Color.rgb(218, 246, 235), Color.rgb(121, 208, 177), 18)
            })
        }
        root.addView(header, FrameLayout.LayoutParams(-1, -2).apply {
            gravity = Gravity.TOP
            setMargins(dp(14), dp(14), dp(14), 0)
        })

        diagnosticsPanel = FrameLayout(this).apply {
            visibility = View.GONE
            background = rounded(0xF7F7FCFF.toInt(), Color.rgb(157, 201, 214), 24)
            elevation = dp(12).toFloat()
            addView(ScrollView(this@MainActivity).apply {
                isFillViewport = true
                addView(diagnostics)
            }, FrameLayout.LayoutParams(-1, -1))
        }
        root.addView(diagnosticsPanel, FrameLayout.LayoutParams(-1, -1).apply {
            setMargins(dp(14), dp(94), dp(14), dp(194))
        })

        val bottom = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14), dp(12), dp(14), dp(12))
            background = glassPanel()
            elevation = dp(10).toFloat()
        }
        statusSummary = TextView(this).apply {
            textSize = 13f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(Color.rgb(30, 68, 80))
            maxLines = 2
            setPadding(dp(4), 0, dp(4), dp(9))
        }
        bottom.addView(statusSummary)

        val actions = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
        }
        actions.addView(glassButton("Kamera", Color.rgb(79, 166, 190)).apply {
            contentDescription = "Włącz kamerę"
            setOnClickListener {
                camera?.close(); camera = null
                permissions.launch(Manifest.permission.CAMERA)
            }
        }, actionParams())
        actions.addView(glassButton("GPS", Color.rgb(75, 174, 139)).apply {
            contentDescription = "Użyj bieżącej lokalizacji GPS"
            setOnClickListener { locationPermissions.launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)) }
        }, actionParams(dp(7)))
        actions.addView(glassButton("Ręcznie", Color.rgb(80, 139, 197)).apply {
            contentDescription = "Wpisz pozycję ręcznie"
            setOnClickListener { manualLocation() }
        }, actionParams(dp(7)))
        recordButton = glassButton("Nagrywaj", Color.rgb(73, 169, 132)).apply {
            contentDescription = "Nagraj sesję diagnostyczną"
            setOnClickListener {
                if (recorder.recording) {
                    finishRecording()
                } else {
                    recorder.start(); text = "Zatrzymaj"
                    recordingStatus = "Nagrywanie: maks. 10 s / 100 klatek"
                }
                updateText()
            }
        }
        actions.addView(recordButton, actionParams(dp(7)))
        bottom.addView(actions)

        diagnosticsToggle = glassButton("Pokaż diagnostykę", Color.rgb(92, 141, 176)).apply {
            contentDescription = "Pokaż lub ukryj szczegóły diagnostyczne"
            setOnClickListener {
                val show = diagnosticsPanel.visibility != View.VISIBLE
                diagnosticsPanel.visibility = if (show) View.VISIBLE else View.GONE
                text = if (show) "Ukryj diagnostykę" else "Pokaż diagnostykę"
            }
        }
        bottom.addView(diagnosticsToggle, LinearLayout.LayoutParams(-1, dp(42)).apply { topMargin = dp(8) })
        root.addView(bottom, FrameLayout.LayoutParams(-1, -2).apply {
            gravity = Gravity.BOTTOM
            setMargins(dp(14), 0, dp(14), dp(14))
        })
        return root
    }

    private fun actionParams(startMargin: Int = 0) = LinearLayout.LayoutParams(0, dp(54), 1f).apply {
        marginStart = startMargin
    }

    private fun glassButton(label: String, accent: Int) = Button(this).apply {
        text = label
        isAllCaps = false
        textSize = 12f
        typeface = Typeface.DEFAULT_BOLD
        setTextColor(Color.rgb(25, 66, 78))
        minHeight = dp(48)
        minimumWidth = 0
        setPadding(dp(4), 0, dp(4), 0)
        stateListAnimator = null
        elevation = dp(2).toFloat()
        background = RippleDrawable(ColorStateList.valueOf(0x334B90A8),
            rounded(0xDDF5FFFC.toInt(), accent, 18), rounded(Color.WHITE, Color.WHITE, 18))
    }

    private fun glassPanel() = rounded(0xEFFFFFFF.toInt(), Color.rgb(173, 216, 211), 26)

    private fun rounded(fill: Int, stroke: Int, radiusDp: Int) = GradientDrawable().apply {
        shape = GradientDrawable.RECTANGLE
        cornerRadius = dp(radiusDp).toFloat()
        setColor(fill)
        setStroke(dp(1), stroke)
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density + 0.5f).toInt()

    override fun onStart() {
        super.onStart()
        orientation.start()
        location.start()
        evaluator = FrameEvaluator(ContextCompat.getMainExecutor(this)) {
            if (packageAvailable) {
                val packageData = MobilePackageLoader.load(assets, "datasets/tatras-v1.tpk")
                datasetStatus = "Dataset: ${packageData.datasetId}; TATRAPK v1; SHA-256=${packageData.packageHash}"
                packageData.createEngine()
            } else SyntheticScene.createEngine()
        }
        preview.post { if (!isFinishing) startCamera() }
    }

    private fun startCamera() {
        if (!lifecycle.currentState.isAtLeast(androidx.lifecycle.Lifecycle.State.STARTED) || camera != null) return
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return
        camera = CameraController(this, this, preview, ::cameraFrame) {
            cameraStatus = it; updateText()
        }.also { it.start() }
    }

    private fun cameraFrame(frame: CameraFrame) {
        val fix = location.accepted()
        declinationDegrees = fix?.let { GeomagneticField(it.latitude.toFloat(), it.longitude.toFloat(),
            checkNotNull(it.ellipsoidHeight).toFloat(), System.currentTimeMillis()).declination.toDouble() }
        val sensorToView = preview.sensorToViewTransform
        if (sensorToView == null) { cameraStatus = "Oczekiwanie na transformację PreviewView"; updateText(); return }
        try {
            val transform = CameraController.fromMatrix(sensorToView) * frame.sensorToBuffer.inverse()
            val uprightToView = transform * pl.tatrapeak.camera.Transform2.upright(frame.crop, frame.rotation).inverse()
            overlay.frame = frame
            overlay.bufferToView = transform
            overlay.invalidate()
            var uprightIntrinsics: Intrinsics? = null
            val intrinsicsReport = try {
                val raw = CameraIntrinsics.sensor(frame).scaleToBuffer(frame.sensorToBuffer)
                val upright = raw.upright(frame.crop, frame.rotation)
                uprightIntrinsics = upright
                "Intrinsics bufora: fx=${raw.fx}, fy=${raw.fy}, cx=${raw.cx}, cy=${raw.cy}\n" +
                    "Po crop/obrocie: fx=${upright.fx}, fy=${upright.fy}, cx=${upright.cx}, cy=${upright.cy}\n${raw.source}"
            } catch (e: Exception) { "Intrinsics niedostępne: ${e.message}" }
            val poseReport = try {
                val c = frame.lens.characteristics
                check(c.get(android.hardware.camera2.CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE) ==
                    android.hardware.camera2.CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME) {
                    "Zegar kamery nie ma potwierdzonej wspólnej podstawy z IMU"
                }
                val sample = checkNotNull(orientation.buffer.nearest(frame.timestampNs)) { "Brak próbki IMU w granicy 25 ms" }
                val declination = checkNotNull(declinationDegrees) { "Brak lokalizacji/deklinacji — brak pozy true ENU" }
                val sensorOrientation = checkNotNull(c.get(android.hardware.camera2.CameraCharacteristics.SENSOR_ORIENTATION))
                val pose = OrientationMath.cameraFromEnu(sample.magneticFromDevice, declination, sensorOrientation, frame.rotation)
                val q = pose.quaternion()
                val k = checkNotNull(uprightIntrinsics) { "Brak intrinsics" }
                val observer = checkNotNull(fix)
                val width = (if (frame.rotation % 180 == 0) frame.crop.width else frame.crop.height).toInt()
                val height = (if (frame.rotation % 180 == 0) frame.crop.height else frame.crop.width).toInt()
                val query = FrameQuery(frame.timestampNs, observer.latitude, observer.longitude,
                    checkNotNull(observer.ellipsoidHeight), observer.manual, q.w, q.x, q.y, q.z,
                    width, height, k.fx, k.fy, k.cx, k.cy)
                val submitted = evaluator?.submit(query) { result ->
                    liveEvaluation = result.fold({ (value, ms) ->
                        overlay.showResult(value, uprightToView)
                        "Evaluate bieżących wejść: ${"%.3f".format(Locale.ROOT, ms)} ms; " +
                            "wyniki=${value.observations.size}; próbki=${value.diagnostics.visibilitySampleCount}"
                    }, { overlay.clearResult(); "Evaluate odrzucone: ${it.message}" })
                    updateText()
                } == true
                if (submitted) recorder.record(query)
                "camera_from_enu (w,x,y,z)=${pose.quaternion()}\n${OrientationMath.diagnostics(pose)}\n" +
                    "Δt klatka−IMU=${(frame.timestampNs-sample.timestampNs)/1e6} ms; deklinacja=$declination°"
            } catch (e: Exception) {
                liveEvaluation = "Evaluate wstrzymane: ${e.message}"
                "Poza niedostępna: ${e.message}"
            }
            cameraStatus = """
                cameraId=${frame.lens.cameraId}, physical=${frame.lens.physicalId}
                f=${frame.capture?.focalMm ?: frame.lens.focalMm} mm; główny 1×: kandydat niezweryfikowany
                Sensor orientation=${frame.lens.characteristics.get(android.hardware.camera2.CameraCharacteristics.SENSOR_ORIENTATION)}°
                Bufor=${frame.width}×${frame.height}; PreviewView=${preview.width}×${preview.height}
                Crop=${frame.crop}; rotacja=${frame.rotation}°; FIT_CENTER
                Capture crop=${frame.capture?.crop ?: "brak dopasowanych metadanych"}
                Active physical=${frame.capture?.activePhysicalId}
                EIS=${frame.capture?.videoStabilization}; OIS=${frame.capture?.opticalStabilization}
                Bufor→widok=${transform.values.joinToString { "%.3f".format(Locale.ROOT, it) }}
                $intrinsicsReport
                $poseReport
            """.trimIndent()
        } catch (e: Exception) { cameraStatus = "Transformacja odrzucona: ${e.message}" }
        updateText()
    }

    private fun updateText() {
        if (!::diagnostics.isInitialized) return
        diagnostics.text = "$datasetStatus\n$recordingStatus\n${location.report()}\n${orientation.status}\n\n$cameraStatus\n\n$liveEvaluation\n\n$smoke"
        if (::statusSummary.isInitialized) {
            statusSummary.text = when {
                camera == null -> "Włącz kamerę, aby rozpocząć"
                location.accepted() == null -> "Ustaw GPS lub pozycję ręczną"
                liveEvaluation.startsWith("Evaluate bieżących wejść:") && liveEvaluation.contains("wyniki=") ->
                    liveEvaluation.removePrefix("Evaluate bieżących wejść: ")
                else -> "Łączenie kamery, pozycji i mapy terenu…"
            }
        }
    }

    private fun manualLocation() {
        val fields = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 8, 32, 8) }
        val lat = EditText(this).apply { hint = "Latitude WGS84 (np. 49.3)" }
        val lon = EditText(this).apply { hint = "Longitude WGS84 (np. 19.98)" }
        val height = EditText(this).apply { hint = "Wysokość elipsoidalna WGS84 w m" }
        listOf(lat, lon, height).forEach {
            it.inputType = android.text.InputType.TYPE_CLASS_NUMBER or android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL or android.text.InputType.TYPE_NUMBER_FLAG_SIGNED
            fields.addView(it)
        }
        location.fix?.let { lat.setText(it.latitude.toString()); lon.setText(it.longitude.toString()); height.setText(it.ellipsoidHeight?.toString() ?: "") }
        val dialog = AlertDialog.Builder(this).setTitle("Pozycja testowa — wysokość elipsoidalna")
            .setView(fields).setPositiveButton("Zastosuj", null).setNegativeButton("Anuluj", null).create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                try {
                    fun EditText.number() = text.toString().replace(',', '.').toDouble()
                    location.setManual(lat.number(), lon.number(), height.number()); dialog.dismiss()
                } catch (e: Exception) { height.error = "Wpisz poprawne współrzędne i wysokość WGS84" }
            }
        }
        dialog.show()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        location.fix?.takeIf { it.manual }?.let {
            outState.putBoolean("manual_location", true)
            outState.putDouble("manual_lat", it.latitude)
            outState.putDouble("manual_lon", it.longitude)
            outState.putDouble("manual_height", checkNotNull(it.ellipsoidHeight))
        }
    }

    private fun finishRecording() {
        recordButton.text = "Nagrywaj"
        recordingStatus = try {
            recorder.stop()?.let { "Sesja: ${it.absolutePath}" } ?: "Sesja pusta — nic nie zapisano"
        } catch (error: Exception) { "Błąd zapisu sesji: ${error.message}" }
        updateText()
    }

    override fun onStop() {
        if (recorder.recording) {
            finishRecording()
        }
        camera?.close(); camera = null
        evaluator?.close(); evaluator = null
        orientation.close()
        location.close()
        overlay.frame = null; overlay.invalidate()
        overlay.clearResult()
        super.onStop()
    }
}
