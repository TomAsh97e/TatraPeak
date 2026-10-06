package pl.tatrapeak.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.View
import pl.tatrapeak.camera.CameraFrame
import pl.tatrapeak.camera.Transform2
import pl.tatrapeak.engine.FrameResult
import pl.tatrapeak.engine.ProjectionState
import pl.tatrapeak.engine.VisibilityState
import kotlin.math.roundToInt

class OverlayView(context: Context) : View(context) {
    private val density = resources.displayMetrics.density
    private val scaledDensity = resources.displayMetrics.scaledDensity
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        strokeWidth = 2f * density
        textSize = 12f * scaledDensity
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    var frame: CameraFrame? = null
    var bufferToView: Transform2? = null
    private var result: FrameResult? = null
    private var resultToView: Transform2? = null
    fun showResult(value: FrameResult, transform: Transform2) {
        result = value; resultToView = transform; invalidate()
    }
    fun clearResult() { result = null; resultToView = null; invalidate() }
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val f = frame ?: return
        val transform = bufferToView ?: return
        val c = f.crop
        val marks = listOf(Triple(c.left, c.top, "TL"), Triple(c.right, c.top, "TR"),
            Triple(c.right, c.bottom, "BR"), Triple(c.left, c.bottom, "BL"),
            Triple((c.left+c.right)/2, (c.top+c.bottom)/2, "+"))
        paint.color = Color.rgb(112, 218, 230)
        for ((x, y, label) in marks) {
            val p = transform.point(x, y)
            val u = p.x.toFloat(); val v = p.y.toFloat()
            canvas.drawLine(u - 8f * density, v, u + 8f * density, v, paint)
            canvas.drawLine(u, v - 8f * density, u, v + 8f * density, paint)
            canvas.drawText(label, u.coerceIn(4f * density, (width - 28f * density).coerceAtLeast(4f * density)),
                v.coerceIn(18f * density, (height - 4f * density).coerceAtLeast(18f * density)), paint)
        }
        val observations = result?.observations ?: return
        val resultTransform = resultToView ?: return
        paint.textSize = 13f * scaledDensity
        for (observation in observations) {
            if (observation.projection != ProjectionState.InFrame) continue
            val pixel = observation.pixel ?: continue
            val p = resultTransform.point(pixel.u, pixel.v)
            val accent = when (observation.visibility) {
                VisibilityState.VisibleInModel -> Color.rgb(101, 215, 171)
                VisibilityState.OccludedInModel -> Color.rgb(255, 142, 161)
                VisibilityState.Uncertain -> Color.rgb(255, 214, 107)
                VisibilityState.MissingTerrain -> Color.rgb(220, 167, 255)
                VisibilityState.NotEvaluated -> Color.rgb(207, 217, 230)
            }
            val heightText = observation.displayElevation?.let { " ${it.roundToInt()} m" } ?: ""
            val label = "${observation.name}$heightText [${observation.visibility}]"
            val textWidth = paint.measureText(label)
            val x = (p.x.toFloat() + 8f * density).coerceIn(6f * density,
                (width - textWidth - 12f * density).coerceAtLeast(6f * density))
            val baseline = (p.y.toFloat() - 7f * density).coerceIn(
                paint.textSize + 9f * density, height - 7f * density)
            labelPaint.color = accent
            canvas.drawRoundRect(x - 5f * density, baseline - paint.textSize - 5f * density,
                x + textWidth + 5f * density, baseline + 4f * density,
                8f * density, 8f * density, labelPaint)
            paint.color = accent
            canvas.drawCircle(p.x.toFloat(), p.y.toFloat(), 5f * density, paint)
            canvas.drawLine(p.x.toFloat(), p.y.toFloat(), x - 2f * density,
                baseline - 7f * density, paint)
            paint.color = Color.rgb(22, 55, 65)
            canvas.drawText(label, x, baseline, paint)
        }
    }
}
