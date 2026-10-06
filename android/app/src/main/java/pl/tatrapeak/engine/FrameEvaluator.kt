package pl.tatrapeak.engine

import java.io.Closeable
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** One worker owns the engine. Backpressure drops work instead of building a queue. */
class FrameEvaluator(private val main: Executor, private val create: () -> NativeEngine) : Closeable {
    private val worker = Executors.newSingleThreadExecutor()
    private val busy = AtomicBoolean(false)
    @Volatile private var closed = false
    private var engine: NativeEngine? = null // worker only
    private var createFailure: Throwable? = null

    // submit and close are called by the UI thread.
    fun submit(query: FrameQuery, result: (Result<Pair<FrameResult, Double>>) -> Unit): Boolean {
        if (closed || !busy.compareAndSet(false, true)) return false
        worker.execute {
            try {
                val value = runCatching {
                    createFailure?.let { throw it }
                    val e = engine ?: try { create().also { engine = it } }
                    catch (error: Throwable) { createFailure = error; throw error }
                    val start = System.nanoTime()
                    val frame = e.evaluate(query)
                    frame to ((System.nanoTime() - start) / 1e6)
                }
                main.execute { if (!closed) result(value) }
            } finally { busy.set(false) }
        }
        return true
    }
    override fun close() {
        if (closed) return
        closed = true
        // Destruction is queued behind a running evaluate and never blocks the UI thread.
        worker.execute { engine?.close(); engine = null }
        worker.shutdown()
    }
}
