package pl.tatrapeak.location

import org.junit.Assert.*
import org.junit.Test

class LocationPolicyTest {
    private val now = 30_000_000_000L
    private val fix = ObserverFix(49.3, 19.98, 1500.0, now, 5.0, 8.0, "gps")
    @Test fun requiresRecentAccurateCoordinatesAndAltitude() {
        assertNull(LocationPolicy.rejection(fix, now))
        assertNotNull(LocationPolicy.rejection(fix.copy(ellipsoidHeight = null), now))
        assertNotNull(LocationPolicy.rejection(fix.copy(elapsedNs = 1), now))
        assertNotNull(LocationPolicy.rejection(fix.copy(horizontalAccuracy = 100.0), now))
        assertNotNull(LocationPolicy.rejection(fix.copy(verticalAccuracy = 100.0), now))
        assertNotNull(LocationPolicy.rejection(fix.copy(latitude = Double.NaN), now))
        assertNotNull(LocationPolicy.rejection(fix.copy(horizontalAccuracy = null), now))
    }
    @Test fun unknownVerticalAccuracyIsExplicitAndManualFixDoesNotExpire() {
        assertNull(LocationPolicy.rejection(fix.copy(verticalAccuracy = null), now))
        assertNull(LocationPolicy.rejection(fix.copy(manual = true, elapsedNs = 0), now))
        assertNotNull(LocationPolicy.rejection(fix.copy(manual = true, ellipsoidHeight = null), now))
    }
}
