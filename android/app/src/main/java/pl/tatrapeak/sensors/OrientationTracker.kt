package pl.tatrapeak.sensors

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import java.io.Closeable

class OrientationTracker(context: Context) : SensorEventListener, Closeable {
    private val manager = context.getSystemService(SensorManager::class.java)
    private val sensor = manager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
    val buffer = RotationBuffer()
    var status = "Orientacja: zatrzymana"
        private set
    private val matrix = FloatArray(9)
    fun start() {
        if (sensor == null || manager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD) == null) {
            status = "Brak TYPE_ROTATION_VECTOR lub magnetometru — brak orientacji"; return
        }
        status = if (manager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME))
            "Orientacja: oczekiwanie na próbkę" else "Nie udało się uruchomić sensora"
    }
    override fun onSensorChanged(event: SensorEvent) {
        if (event.accuracy == SensorManager.SENSOR_STATUS_UNRELIABLE) {
            status = "Orientacja odrzucona: SENSOR_STATUS_UNRELIABLE"
            buffer.clear(); return
        }
        SensorManager.getRotationMatrixFromVector(matrix, event.values)
        buffer.add(RotationSample(event.timestamp, Rotation3(matrix.map { it.toDouble() }.toDoubleArray()), event.accuracy))
        status = "TYPE_ROTATION_VECTOR; accuracy=${event.accuracy}; t=${event.timestamp} ns"
    }
    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {
        if (accuracy == SensorManager.SENSOR_STATUS_UNRELIABLE) buffer.clear()
    }
    override fun close() { manager.unregisterListener(this); buffer.clear(); status = "Orientacja: zatrzymana" }
}
