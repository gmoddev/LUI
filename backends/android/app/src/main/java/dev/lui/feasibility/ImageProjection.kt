package dev.lui.feasibility

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Handler
import android.os.Looper
import android.widget.ImageView
import org.json.JSONObject
import java.lang.ref.WeakReference
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.Future
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit

internal class ImageProjection(private val Session: Session) {
    private val Main = Handler(Looper.getMainLooper())
    private val Executor = ThreadPoolExecutor(2, 2, 30, TimeUnit.SECONDS, ArrayBlockingQueue(8), ThreadPoolExecutor.AbortPolicy()).apply { allowCoreThreadTimeOut(true) }
    private class Entry(val Token: String, val Source: String, val Target: WeakReference<ImageView>, val RecreateReady: Boolean) {
        var Work: Future<*>? = null
        var Bitmap: Bitmap? = null
        var Done = false
        var Started = false
        var Slot = false
    }
    private val Entries = linkedMapOf<Int, Entry>()
    private val Publications = java.util.concurrent.Semaphore(2)
    private val PendingDecoded = java.util.concurrent.atomic.AtomicInteger(0)
    private var Loads = 0
    private var Bytes = 0
    private var PeakBytes = 0
    private var Refused = 0
    private var Retired = false
    private var Applying = false
    fun Begin() { Applying = true }
    fun End() { Applying = false; Schedule() }
    fun Apply(Id: Int, Node: JSONObject, Target: ImageView) {
        if (!Node.has("Image")) { Remove(Id); Target.setImageDrawable(null); return }
        val Image = Node.getJSONObject("Image")
        val Token = Image.getString("Token")
        val Existing = Entries[Id]
        if (Existing?.Token == Token) {
            if (Existing.Done && Node.getString("LoadState") == "Loading") Session.ImageCompleted(Id, Token, Existing.Bitmap != null)
            return
        }
        Remove(Id)
        Entries[Id] = Entry(Token, Image.getString("Source"), WeakReference(Target), Node.getString("LoadState") == "Ready").apply {
            // A failed settled request has no live token to retry. Reattachment
            // must not consume cache space reserved by already-ready siblings.
            if (Node.getString("LoadState") == "Failed") { Started = true; Done = true }
        }
        Schedule()
    }
    private fun Release(Current: Entry) {
        if (Current.Slot) { Current.Slot = false; Loads-- }
        Schedule()
    }
    private fun Schedule() {
        if (Retired || Applying) return
        // Desired targets are bounded by the projection. Only two acquire any
        // asset bytes or decode work at once; unstarted targets own metadata only.
        while (Loads < 2) {
            val Pair = Entries.entries.firstOrNull { !it.value.Started } ?: break
            val Id = Pair.key; val Current = Pair.value
            Current.Started = true; Current.Slot = true; Loads++
            val Weak = WeakReference(this)
            Session.Image(Id, Current.Source, Current.Token) { Data ->
                val Owner = Weak.get() ?: return@Image
                if (Owner.Retired || Owner.Entries[Id] !== Current) { Owner.Release(Current); return@Image }
                if (Data == null) { Owner.Complete(Id, Current, null); Owner.Release(Current); return@Image }
                try {
                    Current.Work = Owner.Executor.submit {
                        var Result: Bitmap? = null
                        var Held = false; var Posted = false
                        val Complete = java.util.concurrent.CountDownLatch(1)
                        try {
                            Owner.Publications.acquire(); Held = true
                            try {
                                val Bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true; inScaled = false }
                                BitmapFactory.decodeByteArray(Data, 0, Data.size, Bounds)
                                val Count = Bounds.outWidth.toLong() * Bounds.outHeight.toLong()
                                if (Bounds.outWidth > 0 && Bounds.outHeight > 0 && Count <= 1024L * 1024 && Bounds.outMimeType == "image/png") {
                                    Result = BitmapFactory.decodeByteArray(Data, 0, Data.size, BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888; inScaled = false })
                                    if ((Result?.allocationByteCount ?: 0) > 4 * 1024 * 1024) { Result?.recycle(); Result = null }
                                }
                            } catch (_: Throwable) { Result?.recycle(); Result = null }
                            val Decoded = Result
                            Owner.PendingDecoded.incrementAndGet()
                            Posted = Main.post {
                                try {
                                    val Live = Weak.get()
                                    if (Live == null || Live.Retired || Live.Entries[Id] !== Current) Decoded?.recycle()
                                    else Live.Complete(Id, Current, Decoded)
                                } finally {
                                    Owner.PendingDecoded.decrementAndGet(); Owner.Publications.release()
                                    Owner.Release(Current); Complete.countDown()
                                }
                            }
                            if (Posted) Complete.await() else Owner.PendingDecoded.decrementAndGet()
                        } catch (_: InterruptedException) { }
                        finally { if (Held && !Posted) { Result?.recycle(); Owner.Publications.release() } }
                    }
                } catch (_: java.util.concurrent.RejectedExecutionException) {
                    Owner.Refused++; Owner.Complete(Id, Current, null); Owner.Release(Current)
                }
            }
        }
    }
    private fun Complete(Id: Int, Entry: Entry, Bitmap: Bitmap?) {
        if (Bitmap != null && Bitmap.allocationByteCount > 32 * 1024 * 1024 - Bytes) {
            Refused++; Bitmap.recycle(); Entry.Done = true
            if (Entry.RecreateReady) Session.PresentationFailed() else Session.ImageCompleted(Id, Entry.Token, false)
            return
        }
        if (Bitmap == null) Refused++
        if (Bitmap == null && Entry.RecreateReady) { Session.PresentationFailed(); return }
        Entry.Done = true; Entry.Bitmap = Bitmap
        Bytes += Bitmap?.allocationByteCount ?: 0; PeakBytes = maxOf(PeakBytes, Bytes)
        Entry.Target.get()?.setImageBitmap(Bitmap)
        Session.ImageCompleted(Id, Entry.Token, Bitmap != null)
    }
    fun Remove(Id: Int) {
        val Entry = Entries.remove(Id) ?: return
        Entry.Work?.let { Work -> Work.cancel(true); if (Work is Runnable) Executor.remove(Work) }
        Entry.Target.get()?.setImageDrawable(null)
        Bytes -= Entry.Bitmap?.allocationByteCount ?: 0
        Entry.Bitmap = null // Android may still own a display-list reference.
        Release(Entry)
    }
    fun Retire() { Retired = true; Entries.keys.toList().forEach { Remove(it) }; Executor.shutdownNow() }
    fun Metrics(): JSONObject = JSONObject().put("Entries", Entries.size).put("Bytes", Bytes).put("PeakBytes", PeakBytes)
        .put("Active", Executor.activeCount).put("Queued", Executor.queue.size).put("PendingDecoded", PendingDecoded.get())
        .put("Loads", Loads).put("Refused", Refused).put("Terminated", Executor.isTerminated)
}
