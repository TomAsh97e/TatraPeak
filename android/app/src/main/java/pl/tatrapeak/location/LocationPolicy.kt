package pl.tatrapeak.location

data class ObserverFix(val latitude: Double, val longitude: Double, val ellipsoidHeight: Double?,
    val elapsedNs: Long, val horizontalAccuracy: Double?, val verticalAccuracy: Double?,
    val source: String, val manual: Boolean = false)

object LocationPolicy {
    const val MAX_AGE_NS = 15_000_000_000L
    const val MAX_HORIZONTAL_M = 30.0
    const val MAX_VERTICAL_M = 50.0
    fun rejection(fix: ObserverFix, nowNs: Long): String? {
        if (!fix.latitude.isFinite() || fix.latitude !in -90.0..90.0 ||
            !fix.longitude.isFinite() || fix.longitude !in -180.0..180.0) return "Nieprawidłowe współrzędne"
        if (fix.ellipsoidHeight == null || !fix.ellipsoidHeight.isFinite()) return "Brak wysokości WGS84 (hasAltitude=false)"
        if (fix.manual) return null
        if (fix.elapsedNs < 0 || fix.elapsedNs > nowNs || nowNs-fix.elapsedNs > MAX_AGE_NS) return "Pomiar starszy niż 15 s lub błędny czas"
        val horizontal = fix.horizontalAccuracy
        if (horizontal == null || !horizontal.isFinite() || horizontal < 0 || horizontal > MAX_HORIZONTAL_M)
            return "Brak dokładności poziomej lub błąd > 30 m"
        val vertical = fix.verticalAccuracy
        if (vertical != null && (!vertical.isFinite() || vertical < 0 || vertical > MAX_VERTICAL_M))
            return "Błąd wysokości > 50 m lub nieprawidłowa dokładność"
        return null
    }
}
