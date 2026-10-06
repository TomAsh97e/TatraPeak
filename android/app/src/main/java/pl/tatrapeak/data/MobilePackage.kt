package pl.tatrapeak.data

import android.content.res.AssetManager
import pl.tatrapeak.engine.NativeEngine
import pl.tatrapeak.engine.Peak
import pl.tatrapeak.engine.TerrainGrid
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest

data class MobilePackage(val datasetId: String, val packageHash: String, val manifestHash: String,
    val grid: TerrainGrid, val peaks: List<Peak>) {
    fun createEngine() = NativeEngine.create(grid, peaks)
}

object MobilePackageLoader {
    private const val HEADER = 168
    private const val MAX_BYTES = 256 * 1024 * 1024
    private const val MAX_CELLS = 40_000_000L
    private const val MAX_PEAKS = 100_000L
    private const val MAX_TEXT = 4096

    fun load(assets: AssetManager, path: String): MobilePackage {
        val length = assets.openFd(path).use { it.length }
        require(length in HEADER..MAX_BYTES.toLong()) { "Nieprawidłowy rozmiar pakietu: $length" }
        val bytes = ByteArray(length.toInt())
        assets.open(path, AssetManager.ACCESS_STREAMING).use { input ->
            var offset = 0
            while (offset < bytes.size) {
                val read = input.read(bytes, offset, bytes.size - offset)
                require(read > 0) { "Pakiet jest ucięty" }
                offset += read
            }
            require(input.read() == -1) { "Pakiet ma dodatkowe bajty" }
        }
        return parse(bytes)
    }

    internal fun parse(bytes: ByteArray): MobilePackage {
        require(bytes.size in HEADER..MAX_BYTES) { "Nieprawidłowy rozmiar pakietu" }
        require(bytes.copyOfRange(0, 8).contentEquals(byteArrayOf(84,65,84,82,65,80,75,0))) { "Błędny magic TATRAPK" }
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).apply { position(8) }
        require(u32(b) == 1L && u32(b) == HEADER.toLong()) { "Nieobsługiwana wersja pakietu" }
        require(b.long == bytes.size.toLong()) { "Niezgodna długość pakietu" }
        val rows = u32(b); val columns = u32(b)
        require(rows >= 2 && columns >= 2 && rows <= MAX_CELLS / columns) { "Błędne wymiary lub overflow rastra" }
        val cells = rows * columns
        require(cells <= MAX_CELLS) { "Raster przekracza limit" }
        val geometry = DoubleArray(4) { b.double }
        require(geometry.all { it.isFinite() } && geometry[2] != 0.0 && geometry[3] != 0.0) { "Błędna geometria rastra" }
        val peakCount = u32(b)
        require(peakCount <= MAX_PEAKS && b.int == 0) { "Błędna liczba szczytów lub rezerwa" }
        val datasetLength = checkedLength(u32(b)); require(b.int == 0) { "Niezerowa rezerwa nagłówka" }
        val heightsLength = b.long; val maskLength = b.long; val peaksLength = b.long
        require(heightsLength == cells * 4 && maskLength == cells && peaksLength >= 0) { "Niezgodne długości sekcji" }
        require(datasetLength.toLong() + heightsLength + maskLength + peaksLength == bytes.size - HEADER.toLong()) {
            "Sekcje nie wypełniają dokładnie pakietu"
        }
        val expectedHash = bytes.copyOfRange(104, 136)
        val manifestHash = bytes.copyOfRange(136, 168)
        val digest = MessageDigest.getInstance("SHA-256").apply {
            update(bytes, 0, 104); update(ByteArray(32)); update(bytes, 136, bytes.size - 136)
        }.digest()
        require(digest.contentEquals(expectedHash)) { "Błędny SHA-256 pakietu" }
        b.position(HEADER)
        val datasetId = text(b, datasetLength)
        require(datasetId.isNotEmpty()) { "Pusty identyfikator datasetu" }
        val heights = FloatArray(cells.toInt()) { b.float }
        val mask = ByteArray(cells.toInt()).also { b.get(it) }
        mask.forEachIndexed { i, value ->
            require(value.toInt() == 0 || value.toInt() and 0xff == 255) { "Maska zawiera wartość inną niż 0/255" }
            if (value.toInt() != 0) require(heights[i].isFinite()) { "Ważna wysokość nie jest skończona" }
        }
        val peaksStart = b.position()
        val ids = HashSet<String>()
        val peaks = List(peakCount.toInt()) {
            require(b.remaining() >= 48) { "Ucięty rekord szczytu" }
            val idLength = checkedLength(u32(b)); val nameLength = checkedLength(u32(b))
            val latitude = b.double; val longitude = b.double; val height = b.double
            val flag = b.get().toInt(); require(flag == 0 || flag == 1) { "Błędna flaga wysokości opisowej" }
            repeat(7) { require(b.get().toInt() == 0) { "Niezerowa rezerwa rekordu" } }
            val display = b.double
            require(latitude.isFinite() && latitude in -90.0..90.0 && longitude.isFinite() && longitude in -180.0..180.0 && height.isFinite()) {
                "Błędna pozycja szczytu"
            }
            require(flag == 1 && display.isFinite() || flag == 0 && display == 0.0) { "Błędna wysokość opisowa" }
            val id = text(b, idLength); val name = text(b, nameLength)
            require(id.isNotEmpty() && ids.add(id)) { "Pusty lub powtórzony identyfikator szczytu" }
            Peak(id, name, latitude, longitude, height, display.takeIf { flag == 1 })
        }
        require(b.position() - peaksStart == peaksLength.toInt() && !b.hasRemaining()) { "Niezgodna długość katalogu lub końcowe bajty" }
        val hex = { data: ByteArray -> data.joinToString("") { "%02x".format(it.toInt() and 0xff) } }
        return MobilePackage(datasetId, hex(expectedHash), hex(manifestHash),
            TerrainGrid(rows.toInt(), columns.toInt(), geometry[0], geometry[1], geometry[2], geometry[3], heights, mask), peaks)
    }

    private fun u32(b: ByteBuffer) = b.int.toLong() and 0xffff_ffffL
    private fun checkedLength(value: Long): Int {
        require(value in 0..MAX_TEXT.toLong()) { "Tekst przekracza limit" }
        return value.toInt()
    }
    private fun text(b: ByteBuffer, length: Int): String {
        require(length <= b.remaining()) { "Ucięty tekst" }
        val bytes = ByteArray(length).also { b.get(it) }
        require(0.toByte() !in bytes) { "NUL w tekście" }
        return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
    }
}
