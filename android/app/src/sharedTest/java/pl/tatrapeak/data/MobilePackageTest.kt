package pl.tatrapeak.data

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.io.File

class MobilePackageTest {
    private fun fixture(): ByteArray {
        val id = "synthetic-v1".toByteArray()
        val peakId = "żółć🏔".toByteArray(); val name = "Visible".toByteArray()
        val peakBytes = 48 + peakId.size + name.size
        val data = ByteBuffer.allocate(168 + id.size + 16 + 4 + peakBytes).order(ByteOrder.LITTLE_ENDIAN)
        data.put(byteArrayOf(84,65,84,82,65,80,75,0)).putInt(1).putInt(168).putLong(data.capacity().toLong())
        data.putInt(2).putInt(2).putDouble(0.1).putDouble(-0.1).putDouble(-0.2).putDouble(0.2)
        data.putInt(1).putInt(0).putInt(id.size).putInt(0).putLong(16).putLong(4).putLong(peakBytes.toLong())
        data.put(ByteArray(32)).put(ByteArray(32) { 0x11 }).put(id)
        repeat(4) { data.putFloat(0f) }; repeat(4) { data.put(0xff.toByte()) }
        data.putInt(peakId.size).putInt(name.size).putDouble(0.01).putDouble(0.0).putDouble(1000.0)
        data.put(1).put(ByteArray(7)).putDouble(999.0).put(peakId).put(name)
        val bytes = data.array()
        val hash = MessageDigest.getInstance("SHA-256").digest(bytes)
        hash.copyInto(bytes, 104)
        return bytes
    }

    @Test fun decodesApprovedV1AndCreatesRealNativeEngine() {
        val packageData = MobilePackageLoader.parse(fixture())
        assertEquals("synthetic-v1", packageData.datasetId)
        assertEquals("żółć🏔", packageData.peaks.single().id)
        packageData.createEngine().use { assertEquals(1, it.evaluate(SyntheticScene.query()).observations.size) }
    }

    @Test fun rejectsVersionHashMaskAndTrailingData() {
        fun corrupt(offset: Int, value: Byte) = fixture().also { it[offset] = value }
        assertThrows(IllegalArgumentException::class.java) { MobilePackageLoader.parse(corrupt(8, 2)) }
        assertThrows(IllegalArgumentException::class.java) { MobilePackageLoader.parse(corrupt(104, 1)) }
        assertThrows(IllegalArgumentException::class.java) { MobilePackageLoader.parse(fixture() + 0) }
        val badMask = fixture(); badMask[168 + "synthetic-v1".toByteArray().size + 16] = 7
        assertThrows(IllegalArgumentException::class.java) { MobilePackageLoader.parse(badMask) }
    }

    @Test fun validatesGeneratedTatrasAssetWhenPresent() {
        val file = File("src/main/assets/datasets/tatras-v1.tpk")
        if (!file.exists()) return
        val packageData = MobilePackageLoader.parse(file.readBytes())
        assertEquals("tatras-v1", packageData.datasetId)
        assertEquals(2160, packageData.grid.rows)
        assertEquals(4321, packageData.grid.columns)
        assertEquals(50, packageData.peaks.size)
    }
}
