package dev.lui.feasibility

import android.app.Application
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import java.lang.ref.WeakReference
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

// One process-owned session. The watchdog never calls JNI, frees the VM, or
// starts a replacement. All toolkit references remain on the main thread.
internal class Session(private val App: Application, private val BundleName: String = "Fixture.json") {
    private val StartedNanos = System.nanoTime()
    private var Visible = false
    private var StartupMs = 0.0
    private var InputStarted = 0L
    private var InputSamples = 0
    private val Latencies = java.util.ArrayDeque<Double>()
    // Written and sampled only on the owner. Count whole admitted jobs, including
    // asset reading/transport work outside JNI and failed jobs, under the soft target.
    private var OwnerJobs = 0L
    private var OwnerJobsOver8Ms = 0L
    private var OwnerJobTotalMs = 0.0
    private var OwnerJobMaxMs = 0.0
    private val Main = Handler(Looper.getMainLooper())
    private val Thread = HandlerThread("LuiOwner").apply { start() }
    private val Owner = Handler(Thread.looper)
    private val Pending = AtomicInteger(0)
    private val Terminals = AtomicInteger(0)
    private val Closed = AtomicBoolean(false)
    private val Poisoned = AtomicBoolean(false)
    private var Token = 0 // owner only
    private var FramePending = false // owner only
    private var Generation = 0 // main only
    private var Sink = WeakReference<MainActivity>(null) // main only
    private var Epoch = 0
    private var Retiring = false
    private var Event = 0
    private data class Input(val Id: Int, val Frame: Int, val Record: org.json.JSONObject, val Terminal: Boolean, val Started: Long)
    private val Inputs = java.util.ArrayDeque<Input>()
    private var InputBytes = 0
    private var TerminalBytes = 0
    private var NormalCount = 0
    private var TerminalCount = 0
    private var Nodes = emptyMap<Int, org.json.JSONObject>()
    private var Accepting = false
    private var DispatchStamp = 0L
    private val Tick = Runnable { Pump() }
    var Revision = 0; private set

    init {
        Submit {
            check(BundleName in setOf("Fixture.json", "Conversation.json", "Modular.json"))
            val Bundle = App.assets.open(BundleName).use { Input ->
                val Output = java.io.ByteArrayOutputStream()
                val Buffer = ByteArray(4096)
                while (true) {
                    val Read = Input.read(Buffer)
                    if (Read < 0) break
                    check(Output.size() + Read <= 10 * 1024 * 1024)
                    Output.write(Buffer, 0, Read)
                }
                Output.toByteArray()
            }
            val Storage = java.io.File(App.noBackupFilesDir, "LuiStorage")
            check(Storage.isDirectory || Storage.mkdir())
            Token = NativeBridge.Start(Bundle, Storage.absolutePath.toByteArray(Charsets.UTF_8))
            check(Token != 0)
            val StartedToken = Token
            Main.post { Generation = StartedToken }
        }
    }
    private fun Submit(Work: () -> Unit): Boolean = Post(Work, false)
    private fun Post(Work: () -> Unit, Reserved: Boolean): Boolean {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (Retiring || Closed.get() || Poisoned.get()) return false
        val Count = if (Reserved) Terminals else Pending
        if (Count.incrementAndGet() > if (Reserved) 64 else 256) { Count.decrementAndGet(); Quarantine(); return false }
        val Complete = AtomicBoolean(false)
        val Watchdog = Runnable { if (!Complete.get()) Quarantine() }
        Main.postDelayed(Watchdog, 5000)
        val Posted = Owner.post {
            val Began = System.nanoTime()
            try { if (!Poisoned.get()) Work() }
            catch (Failure: Throwable) {
                val Line = Failure.stackTrace.firstOrNull { it.className.startsWith("dev.lui.feasibility.") && it.lineNumber > 0 }?.lineNumber ?: 0
                android.util.Log.e("LUI", "[LUI:Mobile] OwnerFailure $Line")
                Poisoned.set(true); Main.post { Quarantine() }
            }
            finally {
                val Elapsed = (System.nanoTime() - Began) / 1000000.0
                OwnerJobs++; if (Elapsed > 8.0) OwnerJobsOver8Ms++
                OwnerJobTotalMs += Elapsed; OwnerJobMaxMs = maxOf(OwnerJobMaxMs, Elapsed)
                Complete.set(true); Count.decrementAndGet(); Main.removeCallbacks(Watchdog)
            }
        }
        if (!Posted) {
            Complete.set(true); Count.decrementAndGet(); Main.removeCallbacks(Watchdog); Quarantine()
        }
        return Posted
    }
    private fun ClearInputs() {
        Inputs.clear(); InputBytes = 0; TerminalBytes = 0; NormalCount = 0; TerminalCount = 0
        InputStarted = 0
    }
    fun Attach(Activity: MainActivity, Width: Double, Height: Double, Theme: Int) {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (Retiring || Poisoned.get() || Epoch == Int.MAX_VALUE) { Activity.Fail(); return }
        Main.removeCallbacks(Tick); ClearInputs(); Nodes = emptyMap(); Accepting = false
        Sink = WeakReference(Activity); Revision = 0; Event = 0; Epoch += 1
        val Surface = Epoch
        Submit {
            check(NativeBridge.Theme(Token, Theme)); check(NativeBridge.Attach(Token, Surface, Width, Height))
            FramePending = false; Deliver(Surface)
        }
    }
    fun Detach(Activity: MainActivity) {
        if (Sink.get() !== Activity) return
        Main.removeCallbacks(Tick); ClearInputs(); Nodes = emptyMap(); Accepting = false
        Sink.clear(); Revision = 0
        val Surface = Epoch
        Post({ NativeBridge.Detach(Token, Surface) }, true)
    }
    private fun CloseAdmission(): Long {
        check(Looper.myLooper() == Looper.getMainLooper())
        // Keep the existing native projection able to enqueue observed input
        // (including quick pointer-up/activation). Only owner dispatch waits.
        Accepting = false
        return ++DispatchStamp
    }
    private fun Ready(Surface: Int, Stamp: Long) {
        if (FramePending) return
        Main.post {
            if (Surface == Epoch && Stamp == DispatchStamp && Sink.get() != null && !Retiring && !Poisoned.get()) {
                val Now = System.nanoTime()
                if (Visible && StartupMs == 0.0) StartupMs = (Now - StartedNanos) / 1000000.0
                if (InputStarted != 0L) {
                    if (Latencies.size == 256) Latencies.removeFirst()
                    Latencies.add((Now - InputStarted) / 1000000.0); InputSamples++; InputStarted = 0
                }
                Accepting = true; Sink.get()?.Admission(true); Drain(); Schedule()
            }
        }
    }
    private fun Deliver(Surface: Int) {
        val Bytes = NativeBridge.Take(Token) ?: return
        FramePending = true
        check(Bytes.size <= 256 * 1024)
        Main.post {
            val Activity = Sink.get()
            if (!Poisoned.get() && !Retiring && Surface == Epoch && Activity != null) {
                try {
                    // Apply is atomic on the UI thread. An already-interactive
                    // surface can keep queuing observed native input while its
                    // owner acknowledgment is pending; Drain still waits.
                    Accepting = false
                    val Text = Charsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
                        .decode(java.nio.ByteBuffer.wrap(Bytes)).toString()
                    val Frame = org.json.JSONObject(Text)
                    check(!Frame.optBoolean("Failed"))
                    check(Frame.getInt("Version") == 1 && Frame.getInt("SurfaceEpoch") == Surface)
                    check(Frame.getInt("Generation") == Generation)
                    val Array = Frame.getJSONArray("Nodes")
                    Nodes = (0 until Array.length()).map { Array.getJSONObject(it) }.associateBy { it.getInt("Id") }
                    Visible = Nodes.values.single { it.getString("Class") == "Window" }.getBoolean("Shown") &&
                        Nodes.values.none { it.getString("Class") == "ImageLabel" && it.getString("LoadState") == "Loading" }
                    Activity.Apply(Frame)
                    Revision = Frame.getInt("Revision")
                    val Applied = Revision
                    val Stamp = CloseAdmission()
                    Submit {
                        check(NativeBridge.Acknowledge(Token, Surface, Applied, true))
                        FramePending = false
                        Deliver(Surface)
                        Ready(Surface, Stamp)
                    }
                } catch (Failure: Throwable) {
                    val Line = Failure.stackTrace.firstOrNull { it.className.startsWith("dev.lui.feasibility.") && it.lineNumber > 0 }?.lineNumber ?: 0
                    android.util.Log.e("LUI", "[LUI:Mobile] ProjectionFailure $Line")
                    Quarantine()
                }
            }
        }
    }
    fun Activate(Id: Int) {
        Send(Id, org.json.JSONObject().put("Kind", "Activate"))
    }
    fun Send(Id: Int, Record: org.json.JSONObject) {
        val Began = System.nanoTime()
        check(Looper.myLooper() == Looper.getMainLooper())
        if (Sink.get() == null || Revision == 0 || Retiring || Poisoned.get()) return
        val Kind = Record.getString("Kind")
        val Terminal = (Kind == "Pointer" && Record.optInt("Phase") == 3) ||
            (Kind == "Key" && Record.optInt("Phase") == 2) || (Kind == "Focus" && !Record.getBoolean("Value")) ||
            (Kind == "Composition" && Record.optInt("Phase") == 2)
        val Node = Nodes[Id]
        if (Node == null && !Terminal) return
        if (Node != null) {
            if (!Record.has("Observation")) Record.put("Observation", Node.getString("Observation"))
            if (Kind in listOf("Activate", "Checked", "Value")) Record.put("Content", Node.getString("Content"))
        }
        val Last = Inputs.peekLast()
        if (Last != null && Last.Id == Id && !Terminal && !Last.Terminal &&
            Last.Record.optString("Observation") == Record.optString("Observation")) {
            val Previous = Last.Record
            val Replace = Kind == "Text" && Previous.optString("Kind") == Kind &&
                Previous.optString("InputRevision") == Record.optString("InputRevision")
            val Move = Kind == "Pointer" && Previous.optString("Kind") == Kind && Record.optInt("Phase") == 1 &&
                Previous.optInt("Phase") == 1 && Previous.optInt("PointerId") == Record.optInt("PointerId")
            if (Replace || Move) {
                Inputs.removeLast(); NormalCount--; InputBytes -= Previous.toString().toByteArray(Charsets.UTF_8).size
            }
        }
        val Size = Record.toString().toByteArray(Charsets.UTF_8).size
        if (Size > 128 * 1024 || (if (Terminal) TerminalCount >= 64 || TerminalBytes + Size > 64 * 1024
            else NormalCount >= 256 || InputBytes + Size > 1024 * 1024)) { Quarantine(); return }
        Inputs.add(Input(Id, Revision, Record, Terminal, Began))
        if (Terminal) { TerminalCount++; TerminalBytes += Size } else { NormalCount++; InputBytes += Size }
        Drain()
    }
    private fun Drain() {
        if (!Accepting || Revision == 0 || Inputs.isEmpty() || Retiring || Poisoned.get()) return
        if (Event == Int.MAX_VALUE) { Quarantine(); return }
        Main.removeCallbacks(Tick)
        val Input = Inputs.removeFirst()
        val Size = Input.Record.toString().toByteArray(Charsets.UTF_8).size
        if (Input.Terminal) { TerminalCount--; TerminalBytes -= Size } else { NormalCount--; InputBytes -= Size }
        val Surface = Epoch; val Frame = Input.Frame; val Sequence = ++Event
        InputStarted = Input.Started
        val Stamp = CloseAdmission()
        Post({
            check(NativeBridge.Event(Token, Surface, Frame, Sequence, Input.Id, Input.Record.toString().toByteArray(Charsets.UTF_8)) >= 0)
            Deliver(Surface)
            Ready(Surface, Stamp)
        }, Input.Terminal)
    }
    private fun Schedule() {
        Main.removeCallbacks(Tick)
        if (Sink.get() != null && Accepting && !Retiring && !Poisoned.get()) Main.postDelayed(Tick, 16)
    }
    private fun Pump() {
        if (Sink.get() == null || Retiring || Poisoned.get() || !Accepting) return
        val Surface = Epoch
        val Stamp = CloseAdmission()
        Submit {
            check(NativeBridge.Pump(Token)); Deliver(Surface)
            Ready(Surface, Stamp)
        }
    }
    fun Image(Id: Int, Path: String, TokenValue: String, Complete: (ByteArray?) -> Unit) {
        Submit {
            val Bytes = NativeBridge.Asset(Token, Path.toByteArray(Charsets.UTF_8))
            // Captured bytes belong to the generation, not a viewport epoch.
            // The renderer separately validates its node/token and lifetime.
            Main.post { Complete(if (!Retiring && !Poisoned.get()) Bytes else null) }
        }
    }
    fun PresentationFailed() { Quarantine() }
    fun ResetMeasurements() {
        check(Looper.myLooper() == Looper.getMainLooper() && Accepting && Inputs.isEmpty() && InputStarted == 0L)
        Latencies.clear(); InputSamples = 0
    }
    fun Metrics(Complete: (org.json.JSONObject) -> Unit) {
        Submit {
            val Bytes = NativeBridge.Metrics(Token) ?: error("[LUI:Mobile] Metrics")
            val Native = org.json.JSONObject(String(Bytes, Charsets.UTF_8))
            Native.put("OwnerJobs", OwnerJobs).put("OwnerJobsOver8Ms", OwnerJobsOver8Ms)
                .put("OwnerJobTotalMs", OwnerJobTotalMs).put("OwnerJobMaxMs", OwnerJobMaxMs)
            Main.post {
                val Sorted = Latencies.sorted()
                fun Percentile(Fraction: Double) = if (Sorted.isEmpty()) 0.0 else Sorted[kotlin.math.ceil(Sorted.size * Fraction).toInt().coerceIn(1, Sorted.size) - 1]
                Complete(Native.put("StartupMs", StartupMs).put("InputSamples", InputSamples)
                    .put("P95Ms", Percentile(0.95)).put("P99Ms", Percentile(0.99))
                    .put("MaxMs", Sorted.lastOrNull() ?: 0.0))
            }
        }
    }
    fun ImageCompleted(Id: Int, TokenValue: String, Success: Boolean) {
        val Surface = Epoch
        if (Sink.get() == null || Retiring || Poisoned.get()) return
        val Stamp = CloseAdmission()
        Submit {
            val Record = org.json.JSONObject().put("Kind", "Image").put("Token", TokenValue).put("Success", Success)
            check(NativeBridge.Event(Token, Surface, 0, 0, Id, Record.toString().toByteArray(Charsets.UTF_8)) >= 0)
            Deliver(Surface)
            Ready(Surface, Stamp)
        }
    }
    private fun Quarantine() {
        check(Looper.myLooper() == Looper.getMainLooper())
        Poisoned.set(true); Sink.get()?.Fail(); Sink.clear(); Revision = 0
        Retire() // queued behind the failed owner; never a synchronous UI join
    }
    fun Retire() {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (Retiring) return
        Retiring = true; Sink.clear(); Revision = 0
        Main.removeCallbacks(Tick); ClearInputs(); Nodes = emptyMap(); Accepting = false
        // One reserved terminal job, independent of the ordinary admission cap.
        val RetireDeadline = android.os.SystemClock.uptimeMillis() + 5000
        val Complete = object : Runnable {
            override fun run() {
                var Retired = false
                try {
                    Retired = Token == 0 || NativeBridge.Close(Token)
                    if (Retired) Token = 0
                } catch (_: Throwable) { Poisoned.set(true) }
                if (Retired) { Closed.set(true); Thread.quitSafely() }
                else if (android.os.SystemClock.uptimeMillis() < RetireDeadline) Owner.postDelayed(this, 10)
                else { Poisoned.set(true); Thread.quitSafely() } // registry retained; no replacement
            }
        }
        Owner.post(Complete)
    }
    fun IsClosed(): Boolean = Closed.get() && !Thread.isAlive
    fun MatchesBundle(Name: String): Boolean = BundleName == Name
    companion object {
        private var Current: Session? = null
        fun GetSession(App: Application, BundleName: String = "Fixture.json"): Session {
            check(Looper.myLooper() == Looper.getMainLooper())
            val Existing = Current
            if (Existing != null && !Existing.IsClosed()) {
                return Existing
            }
            return Session(App, BundleName).also { Current = it }
        }
    }
}
