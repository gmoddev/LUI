package dev.lui.feasibility

import android.content.Intent
import android.test.InstrumentationTestCase
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import org.json.JSONObject
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

@Suppress("DEPRECATION")
class WorkloadTest : InstrumentationTestCase() {
    private val ProcessStartNanos = System.nanoTime()
    private fun Main(Work: () -> Unit) {
        var Failure: Throwable? = null
        instrumentation.runOnMainSync { try { Work() } catch (Problem: Throwable) { Failure = Problem } }
        Failure?.let { throw it }
    }
    private fun Await(Condition: () -> Boolean) {
        val Until = System.nanoTime() + TimeUnit.SECONDS.toNanos(25)
        while (System.nanoTime() < Until) {
            var Ready = false; Main { Ready = Condition() }; if (Ready) return
            Thread.sleep(10)
        }
        fail("[LUI:MobileWorkloadTest] Timeout")
    }
    private fun Walk(Root: View): List<View> = listOf(Root) + if (Root is ViewGroup) (0 until Root.childCount).flatMap { Walk(Root.getChildAt(it)) } else emptyList()
    private fun Named(Activity: MainActivity, Name: String) = Walk(Activity.window.decorView).singleOrNull { it.tag == Name }
    private fun Text(Activity: MainActivity, Name: String) = (Named(Activity, Name) as? TextView)?.text?.toString()
    private fun Field(Name: String, Owner: Any): Any? = Owner.javaClass.getDeclaredField(Name).apply { isAccessible = true }.get(Owner)
    private fun Owner(Activity: MainActivity) = MainActivity::class.java.getDeclaredField("OwnerSession").apply { isAccessible = true }.get(Activity) as Session
    private fun Projection(Activity: MainActivity) = MainActivity::class.java.getDeclaredField("Renderer").apply { isAccessible = true }.get(Activity) as Projection
    private fun Admitted(Activity: MainActivity) = Field("Accepting", Owner(Activity)) as Boolean
    private fun Launch(Modular: Boolean = false): MainActivity = instrumentation.startActivitySync(Intent(instrumentation.targetContext,
        if (Modular) ModularActivity::class.java else ConversationActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
    private fun Ready(Activity: MainActivity) = Admitted(Activity) && Text(Activity, "WorkloadStatus")?.startsWith("10000 items") == true &&
        Activity.PresentationMetrics().getJSONObject("Images").let { it.getInt("Loads") == 0 && it.getInt("PendingDecoded") == 0 }
    private fun Node(Activity: MainActivity, Name: String): JSONObject {
        @Suppress("UNCHECKED_CAST") val Nodes = Field("Nodes", Owner(Activity)) as Map<Int, JSONObject>
        return Nodes.values.single { it.getString("Name") == Name }
    }
    private fun Click(Activity: MainActivity, Name: String) {
        // Check admission and dispatch in the same UI turn: a scheduled owner
        // pump may legitimately close admission between separate Main calls.
        Await {
            if (Admitted(Activity) && Named(Activity, Name)?.isEnabled == true) {
                assertTrue((Named(Activity, Name) as Button).performClick()); true
            } else false
        }
        Await { Admitted(Activity) }
    }
    private fun Metrics(Activity: MainActivity): JSONObject {
        val Done = CountDownLatch(1); var Result: JSONObject? = null
        Main { Owner(Activity).Metrics { Value -> Result = Value.put("Presentation", Activity.PresentationMetrics()); Done.countDown() } }
        assertTrue(Done.await(10, TimeUnit.SECONDS))
        val Memory = android.os.Debug.MemoryInfo(); android.os.Debug.getMemoryInfo(Memory)
        return Result!!.put("ProcessPssKiB", Memory.totalPss).put("Gfx", Frames(Result!!.getJSONObject("Presentation").getDouble("RefreshHz")))
    }
    private fun Gfx(Reset: Boolean = false): String {
        // Test-APK-only shell authority, fixed to this synthetic application's
        // renderer. No app permission or in-process frame observer is installed.
        val Pipe = instrumentation.uiAutomation.executeShellCommand("dumpsys gfxinfo dev.lui.feasibility " + if (Reset) "reset" else "framestats")
        return android.os.ParcelFileDescriptor.AutoCloseInputStream(Pipe).bufferedReader().use { it.readText() }
    }
    private fun Frames(Refresh: Double): JSONObject {
        val Dump = Gfx(); val Now = System.nanoTime()
        var Profile = false; var Header = emptyList<String>(); var Samples = 0; var Unknown = 0; var Over = 0; var MaxMs = 0.0
        for (Line in Dump.lineSequence()) {
            if (Line.trim() == "---PROFILEDATA---") { Profile = !Profile; Header = emptyList(); continue }
            if (!Profile) continue
            val Fields = Line.trim().split(',')
            if (Fields.firstOrNull() == "Flags" && Fields.contains("IntendedVsync")) { Header = Fields; continue }
            if (Header.isEmpty() || Fields.firstOrNull()?.toLongOrNull() == null) continue
            fun Value(Name: String) = Fields.getOrNull(Header.indexOf(Name))?.toLongOrNull() ?: -1L
            val Start = Value("IntendedVsync"); val End = Value("FrameCompleted")
            if (Value("Flags") != 0L || Start < ProcessStartNanos || End < Start || End > Now) { Unknown++; continue }
            val Duration = (End - Start) / 1000000.0
            Samples++; if (Duration > 1000.0 / Refresh) Over++; MaxMs = maxOf(MaxMs, Duration)
        }
        val Total = Regex("Total frames rendered: (\\d+)").find(Dump)?.groupValues?.get(1)?.toInt() ?: -1
        val Janky = Regex("Janky frames: (\\d+)").find(Dump)?.groupValues?.get(1)?.toInt() ?: -1
        assertTrue("[LUI:MobileWorkloadTest] gfxinfo samples", Samples > 0 && Total >= Samples)
        return JSONObject().put("TotalFrames", Total).put("JankyFrames", Janky).put("TrailingSamples", Samples)
            .put("TrailingOverRefresh", Over).put("UnknownSamples", Unknown).put("TrailingMaxMs", MaxMs)
    }
    private fun Emit(Name: String, Value: JSONObject) {
        instrumentation.sendStatus(1, android.os.Bundle().apply { putString(Name, Value.toString()) })
    }
    private fun Finish(Activity: MainActivity) {
        lateinit var Session: Session; lateinit var Renderer: Projection
        Main { Session = Owner(Activity); Renderer = Projection(Activity); Activity.finish() }
        Await { Session.IsClosed() && Renderer.Metrics().getJSONObject("Images").let { it.getBoolean("Terminated") && it.getInt("PendingDecoded") == 0 } }
        Main {
            val UI = Renderer.Metrics(); val Images = UI.getJSONObject("Images")
            assertEquals(0, UI.getInt("Views")); assertEquals(0, Images.getInt("Entries")); assertEquals(0, Images.getInt("Bytes"))
            assertEquals(0, Images.getInt("Queued")); assertEquals(0, Images.getInt("Loads"))
            assertEquals(0, Field("Pending", Session).let { it as java.util.concurrent.atomic.AtomicInteger }.get())
            assertEquals(0, Field("Terminals", Session).let { it as java.util.concurrent.atomic.AtomicInteger }.get())
            assertEquals(0, (Field("Nodes", Session) as Map<*, *>).size)
        }
    }
    fun testConversationTransitionsAndLatency() {
        val Activity = Launch()
        try {
            Await { Ready(Activity) }
            // Native editor focus without invoking a keyboard keeps the measured
            // viewport fixed. Real physical IME/provider usability stays open.
            lateinit var Editor: EditText
            Gfx(true) // Connect test-only automation before establishing focus.
            Main {
                Editor = Named(Activity, "Composer") as EditText; Editor.showSoftInputOnFocus = false
                (Activity.getSystemService(android.content.Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager)
                    .hideSoftInputFromWindow(Editor.windowToken, 0)
            }
            Await { Admitted(Activity) && Activity.window.decorView.rootWindowInsets?.isVisible(android.view.WindowInsets.Type.ime()) == false }
            Main {
                // Android may have assigned initial focus while the projection
                // was being applied. Generate an explicit observed transition.
                Editor.clearFocus(); Editor.requestFocus()
            }
            Await { Admitted(Activity) && Node(Activity, "Composer").getBoolean("Focused") }
            Await { if (Admitted(Activity)) { Owner(Activity).ResetMeasurements(); true } else false }
            for (Index in 0 until 200) {
                Click(Activity, if (Index % 2 == 0) "Insert" else "Edit")
                Main {
                    assertTrue("[LUI:MobileWorkloadTest] EditorIdentity $Index", Editor === Named(Activity, "Composer"))
                    assertTrue("[LUI:MobileWorkloadTest] NativeFocus $Index Window=${Activity.hasWindowFocus()}", Editor.hasFocus())
                    assertTrue("[LUI:MobileWorkloadTest] CoreFocus $Index", Node(Activity, "Composer").getBoolean("Focused"))
                }
            }
            val Result = Metrics(Activity)
            Main {
                val Counts = Regex("^(\\d+) items \\| (\\d+) rows").find(Text(Activity, "WorkloadStatus")!!)!!
                val Items = Counts.groupValues[1].toInt(); val Rows = Counts.groupValues[2].toInt()
                assertEquals(10100, Items); assertTrue(Rows in 1..48)
                Result.put("ModelItems", Items).put("ResidentRows", Rows)
            }
            Emit("Conversation", Result)
            assertTrue(Result.getDouble("StartupMs") > 0 && Result.getDouble("StartupMs") <= 5000)
            assertTrue(Result.getInt("InputSamples") >= 200)
            assertTrue(Result.getDouble("P95Ms") <= 50); assertTrue(Result.getDouble("P99Ms") <= 250)
            assertTrue(Result.getJSONObject("Runtime").getLong("LuaMemoryBytes") <= 32L * 1024 * 1024)
            assertTrue(Result.getInt("NativeNodes") <= 128 && Result.getLong("RetainedBytes") <= 192 * 1024)
            Main { Editor.clearFocus() }; Await { Admitted(Activity) }
            val Anchor = Node(Activity, "WorkloadStatus").getJSONObject("Properties").getString("AccessibilityLabel")
            val Before = Node(Activity, Anchor)
            Main { (Named(Activity, Anchor) as EditText).requestFocus() }
            Await { Admitted(Activity) && Node(Activity, Anchor).getBoolean("Focused") }
            Click(Activity, "Insert")
            Main {
                val After = Node(Activity, Anchor)
                assertEquals(Before.getInt("Id"), After.getInt("Id")); assertTrue(After.getBoolean("Focused"))
                assertEquals(Before.getJSONArray("Bounds").toString(), After.getJSONArray("Bounds").toString())
            }
            Click(Activity, "Bottom")
            Main { assertNull(Named(Activity, Anchor)) }
            Click(Activity, "Top")
            Click(Activity, "Delete"); Click(Activity, "Append")
        } finally { Finish(Activity) }
    }
    fun testModularNavigationAndRetirement() {
        val Activity = Launch(true)
        try {
            Await { Admitted(Activity) && Text(Activity, "CounterStatus") == "Actions: 0" }
            Click(Activity, "Increment"); Await { Text(Activity, "CounterStatus") == "Actions: 1" }
            val Retired = Named(Activity, "Increment")!!
            Click(Activity, "ShowLibrary"); Click(Activity, "Project_Workshop")
            Main {
                assertEquals("Selected: Workshop", Text(Activity, "Selection"))
                assertNull(Retired.accessibilityDelegate); assertNull((Retired.parent as View).accessibilityDelegate)
            }
            Click(Activity, "ShowHome")
            Main { assertEquals("Actions: 1", Text(Activity, "CounterStatus")); Retired.performClick() }
            Await { Admitted(Activity) }
            Main { assertEquals("Actions: 1", Text(Activity, "CounterStatus")) }
            Emit("Modular", Metrics(Activity))
        } finally { Finish(Activity) }
    }
    fun testInputStormAndConflictingBundle() {
        val Activity = Launch()
        val Release = CountDownLatch(1); val Entered = CountDownLatch(1)
        try {
            Await { Ready(Activity) }
            val Existing = Owner(Activity)
            val Busy = Launch(true)
            Main {
                assertSame(Existing, Owner(Busy)); assertFalse(Existing.IsClosed())
                assertTrue(Walk(Busy.window.decorView).filterIsInstance<TextView>().any { it.text == "LUI session stopped" })
                Busy.finish()
            }
            Await { Activity.hasWindowFocus() && Admitted(Activity) }
            Main {
                val Submit = Session::class.java.getDeclaredMethod("Submit", kotlin.jvm.functions.Function0::class.java).apply { isAccessible = true }
                val Work: () -> Unit = { Entered.countDown(); check(Release.await(4, TimeUnit.SECONDS)) }
                assertEquals(true, Submit.invoke(Existing, Work))
            }
            assertTrue(Entered.await(2, TimeUnit.SECONDS))
            Main {
                // Queue an older Ready callback, then a later image job while
                // the owner is held. The old callback must not reopen input.
                val Stamp = Field("DispatchStamp", Existing) as Long
                val Epoch = Field("Epoch", Existing) as Int
                Session::class.java.getDeclaredMethod("Ready", Int::class.javaPrimitiveType, Long::class.javaPrimitiveType)
                    .apply { isAccessible = true }.invoke(Existing, Epoch, Stamp)
                @Suppress("UNCHECKED_CAST") val Nodes = Field("Nodes", Existing) as Map<Int, JSONObject>
                val Image = Nodes.values.first { it.getString("Class") == "ImageLabel" && it.has("Image") }
                Existing.ImageCompleted(Image.getInt("Id"), Image.getJSONObject("Image").getString("Token"), true)
            }
            instrumentation.waitForIdleSync()
            Main { assertFalse(Admitted(Activity)) }
            Main {
                assertTrue((Named(Activity, "Insert") as Button).performClick())
                assertTrue((Field("NormalCount", Existing) as Int) > 0)
                // Deliberate test-only queue flood, independent of the native
                // control's input rate.
                val Id = Node(Activity, "Insert").getInt("Id")
                repeat(300) { Existing.Send(Id, JSONObject().put("Kind", "Activate")) }
                assertFalse(Existing.IsClosed())
                assertTrue(Walk(Activity.window.decorView).filterIsInstance<TextView>().any { it.text == "LUI session stopped" })
            }
        } finally { Release.countDown(); Finish(Activity) }
    }
    private fun Recreate(Activity: MainActivity): MainActivity {
        val Session = Owner(Activity); val Old = Projection(Activity)
        val Monitor = instrumentation.addMonitor(ConversationActivity::class.java.name, null, false)
        try {
            Main { Activity.recreate() }
            val Next = instrumentation.waitForMonitorWithTimeout(Monitor, 10000) as MainActivity
            Await { Admitted(Next) && Text(Next, "WorkloadStatus")?.startsWith("10000 items") == true }
            Await { Old.Metrics().getInt("Views") == 0 && Old.Metrics().getJSONObject("Images").getBoolean("Terminated") }
            Main { assertSame(Session, Owner(Next)) }; return Next
        } finally { instrumentation.removeMonitor(Monitor) }
    }
    fun testImagePressureCancellationAndTimerStorm() {
        var Activity = Launch()
        try {
            Await { Ready(Activity) }; Click(Activity, "SettingsPageButton"); Click(Activity, "Pressure")
            Await { Admitted(Activity) && Activity.PresentationMetrics().getJSONObject("Images").getInt("Loads") == 0 }
            Emit("Images", Metrics(Activity))
            Main {
                val UI = Activity.PresentationMetrics().getJSONObject("Images")
                assertTrue(UI.getLong("Bytes") <= 32L * 1024 * 1024); assertTrue(UI.getInt("Refused") > 0)
                assertTrue((1..10).any { Node(Activity, "Pressure$it").getString("LoadState") == "Ready" })
                assertTrue((1..10).any { Node(Activity, "Pressure$it").getString("LoadState") == "Failed" })
            }
            Activity = Recreate(Activity)
            Await { Admitted(Activity) && Activity.PresentationMetrics().getJSONObject("Images").getInt("Loads") == 0 }
            Click(Activity, "Oversize")
            Await { Admitted(Activity) && (1..10).all { Node(Activity, "Pressure$it").getString("LoadState") == "Failed" } }
            Click(Activity, "Broken")
            Await { Admitted(Activity) && (1..10).all { Node(Activity, "Pressure$it").getString("LoadState") == "Failed" } }
            for (Index in 1..20) { Click(Activity, "Pressure"); Click(Activity, "ClearImages") }
            Await { Activity.PresentationMetrics().getJSONObject("Images").let { it.getInt("Loads") == 0 && it.getInt("PendingDecoded") == 0 } }
            Click(Activity, "Storm")
            Await { Admitted(Activity) && Text(Activity, "Storm") == "Storm 1024" }
            Main { assertTrue(Activity.PresentationMetrics().getJSONObject("Images").getLong("Bytes") < 1024 * 1024) }
        } finally { Finish(Activity) }
    }
    fun testRepeatedOwnershipAndProcessMemory() {
        val Pss = mutableListOf<Int>(); var Peak = 0
        for (Cycle in 0 until 110) {
            var Activity = Launch()
            try {
                Await { Ready(Activity) }; Activity = Recreate(Activity); Await { Ready(Activity) }
                val Active = android.os.Debug.MemoryInfo(); android.os.Debug.getMemoryInfo(Active); Peak = maxOf(Peak, Active.totalPss)
            }
            finally { Finish(Activity) }
            instrumentation.waitForIdleSync(); Runtime.getRuntime().gc(); System.runFinalization(); Thread.sleep(50)
            val Memory = android.os.Debug.MemoryInfo(); android.os.Debug.getMemoryInfo(Memory)
            Peak = maxOf(Peak, Memory.totalPss)
            if (Cycle >= 10) Pss.add(Memory.totalPss)
            if ((Cycle + 1) % 10 == 0) Emit("OwnershipProgress", JSONObject().put("CompletedCycles", Cycle + 1).put("RetiredPssKiB", Memory.totalPss))
        }
        fun Median(Values: List<Int>): Int { val Sorted = Values.sorted(); return (Sorted[9] + Sorted[10]) / 2 }
        val First = Median(Pss.take(20)); val Last = Median(Pss.takeLast(20))
        Emit("Ownership", JSONObject().put("WarmupCycles", 10).put("MeasuredCycles", Pss.size)
            .put("First20MedianPssKiB", First).put("Last20MedianPssKiB", Last).put("SampledPeakPssKiB", Peak))
        assertTrue(Last - First <= 16 * 1024)
    }
}
