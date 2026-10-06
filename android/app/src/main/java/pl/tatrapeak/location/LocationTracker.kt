package pl.tatrapeak.location

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.Looper
import android.os.SystemClock
import androidx.core.content.ContextCompat
import java.io.Closeable

class LocationTracker(private val context: Context, private val changed: () -> Unit) : LocationListener, Closeable {
    private val manager = context.getSystemService(LocationManager::class.java)
    var fix: ObserverFix? = null
        private set
    var status = "GPS: oczekiwanie na uprawnienia"
        private set
    private var manual = false
    fun start() {
        if (manual) return
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            status = "Brak dokładnej lokalizacji; lokalizacja przybliżona nie wystarcza. Możesz podać pozycję ręcznie."
            changed(); return
        }
        try {
            if (!manager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                status = "GPS wyłączony/niedostępny — podaj pozycję ręcznie"; changed(); return
            }
            manager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0f, this, Looper.getMainLooper())
            status = "GPS: oczekiwanie na nowy pomiar; nie używam lastLocation"
        } catch (e: Exception) { status = "GPS niedostępny: ${e.message}" }
        changed()
    }
    fun useGps() { manual = false; fix = null; close(); start() }
    fun setManual(latitude: Double, longitude: Double, height: Double) {
        val value = ObserverFix(latitude, longitude, height, SystemClock.elapsedRealtimeNanos(), null, null, "ręczna WGS84", true)
        require(LocationPolicy.rejection(value, value.elapsedNs) == null) { "Nieprawidłowa pozycja ręczna" }
        close(); manual = true; fix = value; status = "Pozycja ręczna/symulowana; dokładność nieznana"; changed()
    }
    fun accepted(): ObserverFix? = fix?.takeIf { LocationPolicy.rejection(it, SystemClock.elapsedRealtimeNanos()) == null }
    fun report(): String {
        val f = fix ?: return status
        val rejection = LocationPolicy.rejection(f, SystemClock.elapsedRealtimeNanos())
        return "$status\n${f.latitude}, ${f.longitude}; h WGS84=${f.ellipsoidHeight} m\n" +
            "Dokładność XY=${f.horizontalAccuracy ?: "nieznana"} m, Z=${f.verticalAccuracy ?: "nieznana"} m\n" +
            (rejection?.let { "ODRZUCONE: $it" } ?: "Pomiar zaakceptowany (${f.source})")
    }
    override fun onLocationChanged(location: Location) {
        if (manual) return
        fix = ObserverFix(location.latitude, location.longitude,
            if (location.hasAltitude()) location.altitude else null, location.elapsedRealtimeNanos,
            if (location.hasAccuracy()) location.accuracy.toDouble() else null,
            if (location.hasVerticalAccuracy()) location.verticalAccuracyMeters.toDouble() else null,
            location.provider ?: "GPS", false)
        status = "GPS: nowy pomiar; altitude to wysokość elipsoidalna WGS84"
        changed()
    }
    override fun onProviderDisabled(provider: String) { fix = null; status = "GPS wyłączony"; changed() }
    override fun onProviderEnabled(provider: String) { status = "GPS włączony; oczekiwanie na pomiar"; changed() }
    @Deprecated("Legacy Android callback")
    override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) = Unit
    override fun close() { manager.removeUpdates(this) }
}
