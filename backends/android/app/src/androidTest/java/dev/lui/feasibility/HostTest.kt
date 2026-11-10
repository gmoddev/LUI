package dev.lui.feasibility

import android.app.Activity
import android.content.Intent
import android.test.InstrumentationTestCase
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

@Suppress("DEPRECATION")
class HostTest : InstrumentationTestCase() {
    private fun Main(Work: () -> Unit) {
        var Failure: Throwable? = null
        instrumentation.runOnMainSync { try { Work() } catch (Problem: Throwable) { Failure = Problem } }
        Failure?.let { throw it }
    }
    private fun Launch(): MainActivity = instrumentation.startActivitySync(
        Intent(instrumentation.targetContext, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    ) as MainActivity
    private fun Walk(Root: View): List<View> = listOf(Root) +
        if (Root is ViewGroup) (0 until Root.childCount).flatMap { Walk(Root.getChildAt(it)) } else emptyList()
    private fun Await(Condition: () -> Boolean) {
        val Deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
        while (System.nanoTime() < Deadline) {
            var Ready = false
            Main { Ready = Condition() }
            if (Ready) return
            Thread.sleep(40)
        }
        fail("[LUI:MobileTest] Timeout")
    }
    private fun ButtonOf(Activity: Activity): Button? = Walk(Activity.window.decorView).filterIsInstance<Button>().singleOrNull { it.tag == "CounterButton" }
    private fun Named(Activity: Activity, Name: String): View? = Walk(Activity.window.decorView).singleOrNull { it.tag == Name }
    private fun Admitted(Activity: Activity): Boolean {
        val SessionField = MainActivity::class.java.getDeclaredField("OwnerSession").apply { isAccessible = true }
        val ReadyField = Session::class.java.getDeclaredField("Accepting").apply { isAccessible = true }
        return ReadyField.getBoolean(SessionField.get(Activity))
    }
    private fun Status(Activity: Activity): String? = if (Admitted(Activity))
        (Named(Activity, "TaskStatus") as? TextView)?.text?.toString() else null
    private fun Text(Activity: Activity, Name: String): String? = (Named(Activity, Name) as? TextView)?.text?.toString()
    private fun Owner(Activity: Activity): Session = MainActivity::class.java.getDeclaredField("OwnerSession").apply { isAccessible = true }.get(Activity) as Session
    private fun Live(Activity: MainActivity): MainActivity {
        @Suppress("UNCHECKED_CAST")
        val Sink = Session::class.java.getDeclaredField("Sink").apply { isAccessible = true }.get(Owner(Activity)) as java.lang.ref.WeakReference<MainActivity>
        return Sink.get()?.takeIf { !it.isDestroyed } ?: Activity
    }
    private fun Node(Activity: Activity, Name: String): org.json.JSONObject {
        @Suppress("UNCHECKED_CAST")
        val Nodes = Session::class.java.getDeclaredField("Nodes").apply { isAccessible = true }.get(Owner(Activity)) as Map<Int, org.json.JSONObject>
        return Nodes.values.single { it.getString("Name") == Name }
    }
    private fun State(Activity: Activity, Name: String) = Node(Activity, Name).getJSONObject("TextState")
    private fun HoldOwner(Activity: Activity): CountDownLatch {
        val Entered = CountDownLatch(1); val Release = CountDownLatch(1)
        Main {
            val Method = Session::class.java.getDeclaredMethod("Submit", kotlin.jvm.functions.Function0::class.java).apply { isAccessible = true }
            val Work: () -> Unit = { Entered.countDown(); check(Release.await(4, TimeUnit.SECONDS)) }
            assertEquals(true, Method.invoke(Owner(Activity), Work))
        }
        assertTrue(Entered.await(2, TimeUnit.SECONDS)); return Release
    }
    private fun Ready(Activity: Activity): Boolean = Status(Activity) == "ABC" &&
        (Named(Activity, "Pixel") as? android.widget.ImageView)?.drawable != null && ButtonOf(Activity)?.isEnabled == true
    private fun Tap(Activity: Activity, Name: String, Fraction: Float = 0.5f) {
        val Position = IntArray(2)
        var X = 0f; var Y = 0f
        Main {
            val View = Named(Activity, Name)!!; View.getLocationOnScreen(Position)
            X = Position[0] + View.width * Fraction; Y = Position[1] + View.height / 2f
        }
        val Time = android.os.SystemClock.uptimeMillis()
        val Down = android.view.MotionEvent.obtain(Time, Time, android.view.MotionEvent.ACTION_DOWN, X, Y, 0)
        val Up = android.view.MotionEvent.obtain(Time, Time + 20, android.view.MotionEvent.ACTION_UP, X, Y, 0)
        instrumentation.sendPointerSync(Down); instrumentation.sendPointerSync(Up)
        Down.recycle(); Up.recycle()
    }
    private fun Finish(Activity: Activity) {
        lateinit var Owner: Session
        Main {
            val Field = MainActivity::class.java.getDeclaredField("OwnerSession").apply { isAccessible = true }
            Owner = Field.get(Activity) as Session
            Activity.finish()
        }
        Await { Owner.IsClosed() }
    }
    fun testOfflineCheckpointRecovery() {
        val Root = java.io.File(instrumentation.targetContext.noBackupFilesDir, "LuiStorage")
        val Record = java.io.File(Root, "K-checkpoint.bin")
        val Previous = if (Record.isFile) Record.readBytes() else null
        var Activity: MainActivity? = null
        try {
            if (Record.exists()) assertTrue(Record.delete())
            var Current = Launch().also { Activity = it }
            Await { Text(Current, "CheckpointState") == "Committed 0" && Admitted(Current) }
            Main { assertTrue((Named(Current, "CheckpointButton") as Button).performClick()) }
            Await { Text(Current, "CheckpointState") == "Committed 1" && Admitted(Current) }
            assertEquals("LUI-CHECKPOINT\n2\n1\n", Record.readText())
            Main { (Named(Current, "Password") as android.widget.EditText).setText("Synthetic password") }
            Finish(Current); Activity = null
            Current = Launch().also { Activity = it }
            Await { Text(Current, "CheckpointState") == "Committed 1" && Admitted(Current) }
            Main {
                assertTrue(Text(Current, "Password") == "")
                assertEquals("Count 0", ButtonOf(Current)?.text?.toString())
            }
            Finish(Current); Activity = null
            Record.writeText("LUI-CHECKPOINT\n1\n7\n")
            val Temporary = java.io.File(Root, ".lui-write"); Temporary.writeText("uncommitted partial bytes")
            Current = Launch().also { Activity = it }
            Await { Text(Current, "CheckpointState") == "Committed 7" && Admitted(Current) }
            assertFalse(Temporary.exists()); assertEquals("LUI-CHECKPOINT\n2\n7\n", Record.readText())
            Finish(Current); Activity = null
            val Unknown = "LUI-CHECKPOINT\n99\n7\n"; Record.writeText(Unknown)
            Current = Launch().also { Activity = it }
            Await { Text(Current, "CheckpointState") == "Checkpoint refused" && Admitted(Current) }
            Main { assertFalse(Named(Current, "CheckpointButton")!!.isEnabled) }
            assertEquals(Unknown, Record.readText())
        } finally {
            Activity?.let { Finish(Live(it)) }
            if (Previous == null) { if (Record.exists()) assertTrue(Record.delete()) } else Record.writeBytes(Previous)
        }
    }
    fun testBackgroundResumeAndScreenLock() {
        var Activity = Launch()
        try {
            Await { Ready(Activity) }
            val Session = Owner(Activity)
            Main { assertTrue(ButtonOf(Activity)!!.performClick()) }
            Await { ButtonOf(Activity)?.text == "Count 1" && Admitted(Activity) }
            lateinit var Timer: TextView
            Main { Timer = Named(Activity, "LifecycleTimer") as TextView; assertTrue(Timer.performClick()) }
            Await { Timer.text == "Timer pending" && Admitted(Activity) }
            for (Cycle in 1..50) {
                Shell("input keyevent KEYCODE_HOME")
                Await { !Activity.hasWindowFocus() && !Admitted(Activity) }
                if (Cycle == 1) { Thread.sleep(1200); Main { assertEquals("Timer pending", Timer.text.toString()) } }
                Shell("am start -W -n dev.lui.feasibility/.MainActivity")
                Await { Activity = Live(Activity); Activity.hasWindowFocus() && Admitted(Activity) }
                Main { assertSame(Session, Owner(Activity)); assertEquals("Count 1", ButtonOf(Activity)?.text?.toString()) }
            }
            Await { Text(Activity, "LifecycleTimer") == "Timer fired" }
            Shell("input keyevent KEYCODE_SLEEP")
            Await { !Activity.hasWindowFocus() && !Admitted(Activity) }
            Shell("input keyevent KEYCODE_WAKEUP"); Shell("wm dismiss-keyguard")
            Shell("am start -W -n dev.lui.feasibility/.MainActivity")
            Await { Activity = Live(Activity); Activity.hasWindowFocus() && Admitted(Activity) }
            Main { assertSame(Session, Owner(Activity)); assertEquals("Count 1", ButtonOf(Activity)?.text?.toString()) }
        } finally {
            Shell("input keyevent KEYCODE_WAKEUP"); Shell("wm dismiss-keyguard")
            Finish(Live(Activity))
        }
    }
    fun testCounterRecreationAndRetirement() {
        var Activity = Launch()
        try {
            Await { ButtonOf(Activity)?.text == "Count 0" }
            Await { Ready(Activity) }
            Main {
                val ButtonView = ButtonOf(Activity)!!
                val Displayed = ButtonView.transformationMethod?.getTransformation(ButtonView.text, ButtonView) ?: ButtonView.text
                assertEquals("Count 0", Displayed.toString())
                val Owner = Session.GetSession(Activity.application)
                val TokenField = Session::class.java.getDeclaredField("Token").apply { isAccessible = true }
                val Token = TokenField.getInt(Owner)
                // Deliberate foreign-thread misuse must neither mutate nor free
                // the runtime. Production UI handlers never enter this bridge.
                assertFalse(NativeBridge.Detach(Token, 1))
                assertFalse(NativeBridge.Close(Token))
                try { NativeBridge.Take(Token); fail("Foreign owner accepted") }
                catch (_: IllegalStateException) { }
            }
            Main { assertTrue(ButtonOf(Activity)!!.performClick()) }
            Await { ButtonOf(Activity)?.text == "Count 1" && Admitted(Activity) }
            val Monitor = instrumentation.addMonitor(MainActivity::class.java.name, null, false)
            Main { Activity.recreate() }
            Activity = instrumentation.waitForMonitorWithTimeout(Monitor, 10000) as MainActivity
            instrumentation.removeMonitor(Monitor)
            Await { ButtonOf(Activity)?.text == "Count 1" && Admitted(Activity) }
            Main { assertTrue(ButtonOf(Activity)!!.performClick()) }
            Await { ButtonOf(Activity)?.text == "Count 2" }
        } finally { Finish(Activity) }
        instrumentation.waitForIdleSync()
    }
    fun testBlockedOwnerIsQuarantinedWithoutReplacement() {
        val Activity = Launch()
        val Entered = CountDownLatch(1)
        val Release = CountDownLatch(1)
        lateinit var Owner: Session
        try {
            Await { ButtonOf(Activity) != null }
            Main {
                Owner = Session.GetSession(Activity.application)
                // Test-only reflection blocks a host job, not Luau or a native
                // provider. Production contains no public stall/test command.
                val Method = Session::class.java.getDeclaredMethod("Submit", kotlin.jvm.functions.Function0::class.java)
                Method.isAccessible = true
                val Work: () -> Unit = { Entered.countDown(); check(Release.await(15, TimeUnit.SECONDS)) }
                assertEquals(true, Method.invoke(Owner, Work))
            }
            assertTrue(Entered.await(5, TimeUnit.SECONDS))
            Await { Walk(Activity.window.decorView).filterIsInstance<TextView>().any { it.text == "LUI session stopped" } }
            Main { assertSame(Owner, Session.GetSession(Activity.application)) }
            assertFalse(Owner.IsClosed())
        } finally {
            Release.countDown()
            Main { Activity.finish() }
        }
        Await { Owner.IsClosed() }
        val Fresh = Launch()
        try { Await { ButtonOf(Fresh)?.text == "Count 0" } }
        finally { Finish(Fresh) }
    }
    fun testAckInFlightQueuesNativeActivation() {
        val Activity = Launch()
        val Entered = CountDownLatch(1); val Release = CountDownLatch(1)
        try {
            Await {
                if (!Ready(Activity)) false else {
                    assertTrue(ButtonOf(Activity)!!.performClick())
                    val Method = Session::class.java.getDeclaredMethod("Submit", kotlin.jvm.functions.Function0::class.java).apply { isAccessible = true }
                    val Work: () -> Unit = { Entered.countDown(); check(Release.await(4, TimeUnit.SECONDS)) }
                    assertEquals(true, Method.invoke(Owner(Activity), Work)); true
                }
            }
            assertTrue(Entered.await(2, TimeUnit.SECONDS))
            instrumentation.waitForIdleSync()
            Main {
                assertEquals("Count 1", ButtonOf(Activity)?.text?.toString())
                assertFalse(Admitted(Activity)); assertTrue(ButtonOf(Activity)!!.performClick())
                assertTrue(Session::class.java.getDeclaredField("NormalCount").apply { isAccessible = true }.getInt(Owner(Activity)) > 0)
            }
            Release.countDown()
            Await { Admitted(Activity) && ButtonOf(Activity)?.text == "Count 2" }
        } finally { Release.countDown(); Finish(Activity) }
    }
    fun testControlsAssetsAndViewport() {
        val Activity = Launch()
        try {
            Await { Status(Activity) == "ABC" }
            Await { (Named(Activity, "Pixel") as? android.widget.ImageView)?.drawable != null }
            Main { (Named(Activity, "Toggle") as android.widget.CheckBox).performClick() }
            Await { Status(Activity) == "Checked" }
            Await { Named(Activity, "Toggle")?.contentDescription == "Toggle 1" }
            Main { assertTrue((Named(Activity, "Toggle") as android.widget.CheckBox).isChecked) }
            Main {
                val Slider = Named(Activity, "Range") as android.widget.SeekBar
                val Down = android.view.MotionEvent.obtain(android.os.SystemClock.uptimeMillis(), android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_DOWN, Slider.width * 0.9f, Slider.height / 2f, 0)
                Slider.dispatchTouchEvent(Down)
                val Up = android.view.MotionEvent.obtain(Down).apply { action = android.view.MotionEvent.ACTION_UP }
                Slider.dispatchTouchEvent(Up); Down.recycle(); Up.recycle()
            }
            Await { ((Named(Activity, "Progress") as? android.widget.ProgressBar)?.progress ?: 0) > 8000 }
            Main { (Named(Activity, "Editor") as android.widget.EditText).setText("Hi\uD83D\uDE00") }
            Await { Status(Activity) == "Hi\uD83D\uDE00" }
            Tap(Activity, "FrontButton")
            Await { Status(Activity) == "Front activated" }
            Tap(Activity, "OverflowButton")
            Await { Status(Activity) == "Overflow activated" }
            Tap(Activity, "ClippedButton", 0.1f)
            Await { Status(Activity) == "Clipped activated" }
            Main {
                val ClipSlot = Named(Activity, "Slot:ClippedButton")!!
                assertEquals(96, (ClipSlot.width / Activity.resources.displayMetrics.density).toInt())
                val Event = android.view.MotionEvent.obtain(0, 0, android.view.MotionEvent.ACTION_DOWN, ClipSlot.width - 2f, 8f, 0)
                assertFalse(ClipSlot.dispatchTouchEvent(Event)); Event.recycle()
                (Named(Activity, "ResizeButton") as Button).performClick()
            }
            Await { Status(Activity) == "Size requested" }
            Main {
                val Slot = Named(Activity, "Slot:ResizeButton")!!
                val Host = Slot.parent as ViewGroup
                assertTrue(Host.width / Activity.resources.displayMetrics.density > 100)
            }
        } finally { Finish(Activity) }
    }
    fun testRotationAndKeyboardOcclusion() {
        var Activity = Launch()
        fun Rotate(Orientation: Int) {
            val Monitor = instrumentation.addMonitor(MainActivity::class.java.name, null, false)
            Main { Activity.requestedOrientation = Orientation }
            Activity = instrumentation.waitForMonitorWithTimeout(Monitor, 10000) as MainActivity
            instrumentation.removeMonitor(Monitor)
            Await { (Named(Activity, "Slot:MobileWindow")?.width ?: 0) > 0 }
            Await { Activity.hasWindowFocus() && Admitted(Activity) }
        }
        try {
            Await { ButtonOf(Activity)?.text == "Count 0" }
            Await { Ready(Activity) }
            Main { ButtonOf(Activity)!!.performClick() }
            Await { ButtonOf(Activity)?.text == "Count 1" }
            Rotate(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
            Await { val Window = Named(Activity, "Slot:MobileWindow"); Window != null && Window.width > Window.height }
            Await { ButtonOf(Activity)?.text == "Count 1" }
            Rotate(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
            Await { val Window = Named(Activity, "Slot:MobileWindow"); Window != null && Window.height > Window.width }
            Main { Activity.window.insetsController!!.hide(android.view.WindowInsets.Type.ime()) }
            Await { !Activity.window.decorView.rootWindowInsets.isVisible(android.view.WindowInsets.Type.ime()) }
            lateinit var Editor: android.widget.EditText
            val Input = Activity.getSystemService(android.content.Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager
            Main { Editor = Named(Activity, "Editor") as android.widget.EditText; Editor.clearFocus(); Editor.requestFocus() }
            // Rotation can lay out the replacement window before its editor is
            // served. A show request during that transition is aborted by WM.
            Await { Activity.hasWindowFocus() && Editor.hasFocus() && Admitted(Activity) && Input.isActive(Editor) }
            Main { assertTrue(Input.showSoftInput(Editor, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT)) }
            Await { Activity.window.decorView.rootWindowInsets.isVisible(android.view.WindowInsets.Type.ime()) }
            Await {
                val Slot = Named(Activity, "Slot:MobileWindow") ?: return@Await false
                val Position = IntArray(2); Slot.getLocationOnScreen(Position)
                val Ime = Activity.window.decorView.rootWindowInsets.getInsets(android.view.WindowInsets.Type.ime()).bottom
                val Expected = Activity.windowManager.currentWindowMetrics.bounds.bottom - Ime
                kotlin.math.abs(Position[1] + Slot.height - Expected) <= 2
            }
            Main { Activity.window.insetsController!!.hide(android.view.WindowInsets.Type.ime()) }
            Await { !Activity.window.decorView.rootWindowInsets.isVisible(android.view.WindowInsets.Type.ime()) }
        } finally { Finish(Activity) }
    }
    fun testRapidEditingCompositionAndScriptPrecedence() {
        val Activity = Launch()
        try {
            Await { Ready(Activity) }
            lateinit var Editor: android.widget.EditText
            lateinit var Connection: android.view.inputmethod.InputConnection
            Main { Editor = Named(Activity, "Editor") as android.widget.EditText; Editor.requestFocus() }
            Await { Admitted(Activity) && State(Activity, "Editor").getString("Text") == "" }
            Main { Connection = Editor.onCreateInputConnection(android.view.inputmethod.EditorInfo())!! }
            val Release = HoldOwner(Activity)
            try {
                repeat(30) {
                    Main { assertTrue(Connection.commitText("x", 1)) }
                    Thread.sleep(3)
                }
                Main { assertTrue(Connection.deleteSurroundingTextInCodePoints(1, 0)) }
            } finally { Release.countDown() }
            Await { Status(Activity) == "x".repeat(29) && Editor.text.toString() == "x".repeat(29) }
            val Unicode = "e\u0301 \u05e9\u05dc\u05d5\u05dd\uD83D\uDE00\r\n"
            Main {
                Connection.beginBatchEdit(); Connection.setSelection(0, Editor.length())
                Connection.commitText(Unicode, 1); Connection.endBatchEdit()
            }
            Await { Status(Activity) == Unicode }
            val Emoji = Unicode.indexOf("\uD83D\uDE00")
            Main { Connection.setSelection(Emoji + 1, Emoji + 1) }
            Await { Admitted(Activity) && Editor.selectionStart == Emoji && State(Activity, "Editor").getInt("Start") == Unicode.codePointCount(0, Emoji) }
            Main {
                Connection.beginBatchEdit(); Connection.setSelection(0, Editor.length()); Connection.commitText("", 1); Connection.endBatchEdit()
            }
            Await { Status(Activity) == "" }
            Main { assertTrue(Connection.setComposingText("n", 1)) }
            Await { Admitted(Activity) && Text(Activity, "CompositionStatus") == "Compose 1 0 0" }
            Main { assertTrue(State(Activity, "Editor").getString("Text").isEmpty()); assertTrue(Connection.setComposingText("ni", 1)) }
            Await { Admitted(Activity) && Text(Activity, "CompositionStatus") == "Compose 1 1 0" }
            Main { assertTrue(Connection.commitText("\u4f60", 1)) }
            Await { Status(Activity) == "\u4f60" && Text(Activity, "CompositionStatus") == "Compose 1 1 1" }
            val ResetRelease = HoldOwner(Activity)
            try {
                Main {
                    (Named(Activity, "ResetInputButton") as Button).performClick()
                    Connection.commitText("z", 1)
                }
                instrumentation.waitForIdleSync()
            } finally { ResetRelease.countDown() }
            Await { Status(Activity) == "Reset" && Editor.text.toString() == "Reset" }
            Main {
                assertFalse(Connection.commitText("Stale", 1))
                if (android.os.Build.VERSION.SDK_INT >= 33) assertFalse(Connection.commitText("Stale", 1, null))
            }
            instrumentation.sendStatus(1, android.os.Bundle().apply {
                putString("StaleInput", org.json.JSONObject().put("Legacy", "Refused")
                    .put("TextAttribute", if (android.os.Build.VERSION.SDK_INT >= 33) "Refused" else "UnavailableBeforeApi33").toString())
            })
        } finally { Finish(Activity) }
    }
    fun testMultiPointerAndTargetRetirement() {
        val Activity = Launch()
        val Start = android.os.SystemClock.uptimeMillis()
        var Position = IntArray(2)
        val Samples = org.json.JSONArray()
        fun Snapshot(): org.json.JSONObject = org.json.JSONObject()
            .put("Counts", Text(Activity, "InputCounts")).put("Counter", ButtonOf(Activity)?.text?.toString())
            .put("WindowFocus", Activity.hasWindowFocus()).put("Admitted", Admitted(Activity))
            .put("ImeVisible", Activity.window.decorView.rootWindowInsets?.isVisible(android.view.WindowInsets.Type.ime()))
            .put("Width", Named(Activity, "Slot:MobileWindow")?.width).put("Height", Named(Activity, "Slot:MobileWindow")?.height)
            .put("Observation", Node(Activity, "CounterButton").getString("Observation"))
            .put("Pressed", ButtonOf(Activity)?.isPressed).put("Enabled", ButtonOf(Activity)?.isEnabled)
            .put("FocusableInTouchMode", ButtonOf(Activity)?.isFocusableInTouchMode)
        fun Motion(Action: Int, Ids: IntArray) {
            val Properties = Ids.map { Id -> android.view.MotionEvent.PointerProperties().apply { id = Id; toolType = android.view.MotionEvent.TOOL_TYPE_FINGER } }.toTypedArray()
            val Coordinates = Ids.map { Id -> android.view.MotionEvent.PointerCoords().apply { x = Position[0] + if (Id == 7) 40f else 80f; y = Position[1] + 32f; pressure = 1f; size = 1f } }.toTypedArray()
            val Event = android.view.MotionEvent.obtain(Start, android.os.SystemClock.uptimeMillis(), Action, Ids.size, Properties, Coordinates, 0, 0, 1f, 1f, 0, 0, android.view.InputDevice.SOURCE_TOUCHSCREEN, 0)
            Samples.put(org.json.JSONObject().put("Time", Event.eventTime).put("Action", Action).put("Ids", org.json.JSONArray(Ids.toList())))
            instrumentation.sendPointerSync(Event); Event.recycle(); Thread.sleep(30)
        }
        try {
            Await { Ready(Activity) }
            lateinit var Before: org.json.JSONObject
            Main {
                ButtonOf(Activity)!!.getLocationOnScreen(Position); Before = Snapshot()
            }
            Motion(android.view.MotionEvent.ACTION_DOWN, intArrayOf(7))
            Motion(android.view.MotionEvent.ACTION_POINTER_DOWN or (1 shl android.view.MotionEvent.ACTION_POINTER_INDEX_SHIFT), intArrayOf(7, 11))
            Motion(android.view.MotionEvent.ACTION_MOVE, intArrayOf(11, 7))
            Motion(android.view.MotionEvent.ACTION_POINTER_UP or (1 shl android.view.MotionEvent.ACTION_POINTER_INDEX_SHIFT), intArrayOf(11, 7))
            Motion(android.view.MotionEvent.ACTION_UP, intArrayOf(11))
            try {
                Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B2 E2 C0 K0" && ButtonOf(Activity)?.text == "Count 1" }
            } catch (Problem: junit.framework.AssertionFailedError) {
                Main {
                    val Diagnostic = org.json.JSONObject().put("Before", Before).put("After", Snapshot()).put("Samples", Samples)
                    instrumentation.sendStatus(1, android.os.Bundle().apply { putString("PointerDiagnostic", Diagnostic.toString()) })
                }
                throw Problem
            }
            Await { Admitted(Activity) && ButtonOf(Activity)?.isPressed == false }
            Main { Named(Activity, "Toggle")!!.getLocationOnScreen(Position) }
            Motion(android.view.MotionEvent.ACTION_DOWN, intArrayOf(7))
            Motion(android.view.MotionEvent.ACTION_POINTER_DOWN or (1 shl android.view.MotionEvent.ACTION_POINTER_INDEX_SHIFT), intArrayOf(7, 11))
            Motion(android.view.MotionEvent.ACTION_MOVE, intArrayOf(11, 7))
            Motion(android.view.MotionEvent.ACTION_POINTER_UP or (1 shl android.view.MotionEvent.ACTION_POINTER_INDEX_SHIFT), intArrayOf(11, 7))
            Motion(android.view.MotionEvent.ACTION_UP, intArrayOf(11))
            Await { Admitted(Activity) && Named(Activity, "Toggle")?.contentDescription == "Toggle 1" &&
                (Named(Activity, "Toggle") as android.widget.CheckBox).isChecked && Named(Activity, "Toggle")?.isPressed == false }
            Main { ButtonOf(Activity)!!.getLocationOnScreen(Position) }
            Motion(android.view.MotionEvent.ACTION_DOWN, intArrayOf(12))
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B3 E2 C0 K0" }
            Main { (Named(Activity, "MoveTarget") as Button).performClick() }
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B3 E3 C1 K0" }
            Main {
                val Slot = Named(Activity, "Slot:CounterButton")!!
                val Root = Slot.parent as ViewGroup
                val Y = (Node(Activity, "CounterButton").getJSONArray("Bounds").getDouble(1) * Activity.resources.displayMetrics.density).toInt()
                assertTrue(kotlin.math.abs(Slot.top - Root.paddingTop - Y) <= 1)
            }
            Motion(android.view.MotionEvent.ACTION_UP, intArrayOf(12))
            Main { (Named(Activity, "MoveTarget") as Button).performClick() }
            Await { Admitted(Activity) && Node(Activity, "CounterButton").getJSONArray("Bounds").getDouble(1) == 16.0 }
            fun Change() {
                Main { (Named(Activity, "ChangeTarget") as Button).performClick() }
            }
            Motion(android.view.MotionEvent.ACTION_DOWN, intArrayOf(3)); Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B4 E3 C1 K0" }
            Change(); Await { Admitted(Activity) && ButtonOf(Activity)?.isEnabled == false && Text(Activity, "InputCounts") == "B4 E4 C2 K0" }
            Motion(android.view.MotionEvent.ACTION_UP, intArrayOf(3))
            Change(); Await { Admitted(Activity) && ButtonOf(Activity)?.isEnabled == true }
            Motion(android.view.MotionEvent.ACTION_DOWN, intArrayOf(5)); Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B5 E4 C2 K0" }
            Change(); Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B5 E5 C3 K0" }
            Motion(android.view.MotionEvent.ACTION_UP, intArrayOf(5))
            Change(); Await { Admitted(Activity) && ButtonOf(Activity)?.isEnabled == true }
            Motion(android.view.MotionEvent.ACTION_DOWN, intArrayOf(9)); Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B6 E5 C3 K0" }
            lateinit var Removed: Button
            Main { Removed = ButtonOf(Activity)!! }
            Change(); Await { Admitted(Activity) && ButtonOf(Activity) == null && Text(Activity, "InputCounts") == "B6 E5 C3 K0" }
            Motion(android.view.MotionEvent.ACTION_UP, intArrayOf(9))
            Main { assertFalse(Removed.performClick()); assertTrue(Text(Activity, "InputCounts") == "B6 E5 C3 K0") }
        } finally { Finish(Activity) }
    }
    fun testKeyboardModifiersAndPasswordClipboard() {
        val Activity = Launch()
        try {
            Await { Ready(Activity) }
            Main { ButtonOf(Activity)!!.apply { isFocusableInTouchMode = true; requestFocus() } }
            Await { Admitted(Activity) && ButtonOf(Activity)?.isFocused == true }
            val Now = android.os.SystemClock.uptimeMillis()
            for ((Action, Code, Repeat) in listOf(
                Triple(0, android.view.KeyEvent.KEYCODE_SHIFT_LEFT, 0), Triple(0, android.view.KeyEvent.KEYCODE_SHIFT_RIGHT, 0),
                Triple(0, android.view.KeyEvent.KEYCODE_SHIFT_LEFT, 1), Triple(1, android.view.KeyEvent.KEYCODE_SHIFT_LEFT, 0),
                Triple(1, android.view.KeyEvent.KEYCODE_SHIFT_RIGHT, 0))) {
                instrumentation.sendKeySync(android.view.KeyEvent(Now, android.os.SystemClock.uptimeMillis(), Action, Code, Repeat))
            }
            Await { Admitted(Activity) && Text(Activity, "KeyStatus") == "Keys 1 1 1" }
            Main {
                val Password = Named(Activity, "Password") as android.widget.EditText
                val Clipboard = Activity.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                Clipboard.setPrimaryClip(android.content.ClipData.newPlainText("Synthetic", "Clipboard sentinel"))
                Password.requestFocus(); Password.setText("Synthetic password")
            }
            Await { Admitted(Activity) && State(Activity, "Password").getString("Text") == "Synthetic password" }
            Main {
                val Password = Named(Activity, "Password") as android.widget.EditText
                Password.selectAll()
                assertFalse(Password.onTextContextMenuItem(android.R.id.copy)); assertFalse(Password.onTextContextMenuItem(android.R.id.cut))
                val State = State(Activity, "Password")
                assertTrue(State.getInt("Start") == 0 && State.getInt("Length") == 0 && State.getString("CompositionText").isEmpty())
                val Info = Password.createAccessibilityNodeInfo()
                assertTrue("[LUI:MobileTest] PasswordRole", Info.isPassword)
                assertTrue("[LUI:MobileTest] PasswordMask", Password.transformationMethod is android.text.method.PasswordTransformationMethod)
                val Clipboard = Activity.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                assertTrue(Clipboard.primaryClip!!.getItemAt(0).text.toString() == "Clipboard sentinel")
                Clipboard.setPrimaryClip(android.content.ClipData.newPlainText("Synthetic", "Paste"))
                val Editor = Named(Activity, "Editor") as android.widget.EditText
                Editor.requestFocus(); assertTrue(Editor.onTextContextMenuItem(android.R.id.paste))
            }
            Await { Status(Activity) == "Paste" }
            Main {
                val Clipboard = Activity.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                val Multiple = android.content.ClipData.newPlainText("Synthetic", "One").apply { addItem(android.content.ClipData.Item("Two")) }
                Clipboard.setPrimaryClip(Multiple)
                assertFalse((Named(Activity, "Editor") as android.widget.EditText).onTextContextMenuItem(android.R.id.paste))
                Clipboard.clearPrimaryClip()
            }
        } finally { Finish(Activity) }
    }
    private fun Accessible(Predicate: (android.view.accessibility.AccessibilityNodeInfo) -> Boolean): android.view.accessibility.AccessibilityNodeInfo? {
        val Until = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
        while (System.nanoTime() < Until) {
            val Queue = java.util.ArrayDeque<android.view.accessibility.AccessibilityNodeInfo>()
            instrumentation.uiAutomation.rootInActiveWindow?.let { Queue.add(it) }
            var Count = 0
            while (Queue.isNotEmpty() && Count++ < 512) {
                val Item = Queue.removeFirst()
                if (Predicate(Item)) return Item
                for (Index in 0 until Item.childCount) Item.getChild(Index)?.let { Queue.add(it) }
            }
            Thread.sleep(40)
        }
        fail("[LUI:MobileTest] AccessibilityTimeout")
        return null
    }
    fun testCanceledMotionAndInputLimits() {
        val Activity = Launch()
        try {
            Await { Ready(Activity) }
            val Point = IntArray(2)
            Main { ButtonOf(Activity)!!.getLocationOnScreen(Point) }
            val Time = android.os.SystemClock.uptimeMillis()
            val Properties = (0..1).map { Index -> android.view.MotionEvent.PointerProperties().apply { id = Index; toolType = android.view.MotionEvent.TOOL_TYPE_FINGER } }.toTypedArray()
            val Coordinates = (0..1).map { Index -> android.view.MotionEvent.PointerCoords().apply { x = Point[0] + 24f + Index * 24f; y = Point[1] + 24f; pressure = 1f; size = 1f } }.toTypedArray()
            fun Motion(Action: Int, Flags: Int, Count: Int = 1) {
                val Event = android.view.MotionEvent.obtain(Time, android.os.SystemClock.uptimeMillis(), Action, Count, Properties.copyOf(Count), Coordinates.copyOf(Count),
                    0, 0, 1f, 1f, 0, 0, android.view.InputDevice.SOURCE_TOUCHSCREEN, Flags)
                instrumentation.sendPointerSync(Event); Event.recycle()
            }
            Motion(android.view.MotionEvent.ACTION_DOWN, 0)
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B1 E0 C0 K0" }
            Motion(android.view.MotionEvent.ACTION_POINTER_DOWN or (1 shl android.view.MotionEvent.ACTION_POINTER_INDEX_SHIFT), 0, 2)
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B2 E0 C0 K0" }
            Motion(android.view.MotionEvent.ACTION_POINTER_UP or (1 shl android.view.MotionEvent.ACTION_POINTER_INDEX_SHIFT), android.view.MotionEvent.FLAG_CANCELED, 2)
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B2 E1 C1 K0" }
            Motion(android.view.MotionEvent.ACTION_CANCEL, 0)
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B2 E2 C2 K0" && ButtonOf(Activity)?.text == "Count 0" }
            lateinit var Editor: android.widget.EditText
            lateinit var Connection: android.view.inputmethod.InputConnection
            Main { Editor = Named(Activity, "Editor") as android.widget.EditText; Editor.requestFocus(); Editor.setText("Keep") }
            Await { Status(Activity) == "Keep" }
            Main {
                Connection = Editor.onCreateInputConnection(android.view.inputmethod.EditorInfo())!!
                Connection.setSelection(0, Editor.length()); Connection.commitText("x".repeat(65535), 1)
                assertTrue(Editor.text.toString() == "Keep")
                Connection.commitText("\u0000", 1); assertTrue(Editor.text.toString() == "Keep")
                Connection.setSelection(4, 1)
            }
            Await { Admitted(Activity) && State(Activity, "Editor").getInt("Start") == 1 && State(Activity, "Editor").getInt("Length") == 3 }
            Main { Connection.setComposingText("z".repeat(4097), 1) }
            instrumentation.waitForIdleSync()
            Await { Admitted(Activity) && Editor.text.toString() == "Keep" && android.view.inputmethod.BaseInputConnection.getComposingSpanStart(Editor.text) < 0 }
            Main { assertTrue(!State(Activity, "Editor").getBoolean("IsComposing")) }
        } finally { Finish(Activity) }
    }
    fun testScrollFlingAndLiveAccessibility() {
        val Activity = Launch()
        val GestureVelocity = android.view.VelocityTracker.obtain()
        val GestureSamples = org.json.JSONArray()
        try {
            Await { Ready(Activity) }
            assertNotNull(Accessible { it.className == "android.widget.ScrollView" })
            Await { Admitted(Activity) }
            val Start = android.os.SystemClock.uptimeMillis()
            val Point = IntArray(2); var Density = 1f
            Main { Named(Activity, "ScrollTop")!!.getLocationOnScreen(Point); Density = Activity.resources.displayMetrics.density }
            fun Motion(Action: Int, Offset: Float) {
                // This is a synthetic sampled gesture. Automation transport can
                // pause long enough to turn a stationary release into zero
                // velocity; keep sample intervals fixed and release in motion.
                val Event = android.view.MotionEvent.obtain(Start, Start + GestureSamples.length() * 20L, Action,
                    Point[0] + 24 * Density, Point[1] + 16 * Density - Offset * Density, 0)
                assertTrue(Event.eventTime <= android.os.SystemClock.uptimeMillis())
                Event.source = android.view.InputDevice.SOURCE_TOUCHSCREEN
                GestureVelocity.addMovement(Event)
                GestureSamples.put(org.json.JSONObject().put("Time", Event.eventTime).put("Offset", Offset).put("Action", Action))
                instrumentation.sendPointerSync(Event); Event.recycle(); Thread.sleep(20)
            }
            Motion(android.view.MotionEvent.ACTION_DOWN, 0f)
            for (Offset in listOf(8f, 16f, 24f)) Motion(android.view.MotionEvent.ACTION_MOVE, Offset)
            var Before = 0.0
            Main { Before = Node(Activity, "ScrollBottom").getJSONArray("Bounds").getDouble(1) }
            Motion(android.view.MotionEvent.ACTION_UP, 32f)
            GestureVelocity.computeCurrentVelocity(1000)
            assertTrue("[LUI:MobileTest] Synthetic fling velocity ${GestureVelocity.yVelocity} Samples=$GestureSamples",
                kotlin.math.abs(GestureVelocity.yVelocity) >= android.view.ViewConfiguration.get(Activity).scaledMinimumFlingVelocity)
            try {
                Await { Admitted(Activity) && Node(Activity, "ScrollBottom").getJSONArray("Bounds").getDouble(1) < Before - 1 }
            } catch (Problem: junit.framework.AssertionFailedError) {
                Main {
                    val Slot = Named(Activity, "ScrollTop")!!.parent
                    val Driver = Slot.javaClass.getDeclaredField("Driver").apply { isAccessible = true }.get(Slot)
                    fun DriverField(Name: String) = Driver.javaClass.getDeclaredField(Name).apply { isAccessible = true }.get(Driver)
                    val Diagnostic = org.json.JSONObject().put("Samples", GestureSamples).put("VelocityY", GestureVelocity.yVelocity)
                        .put("MinimumVelocity", android.view.ViewConfiguration.get(Activity).scaledMinimumFlingVelocity)
                        .put("FlingFinished", (DriverField("Flinger") as android.widget.OverScroller).isFinished)
                        .put("ScrollObservationMatches", DriverField("ScrollObservation") == DriverField("ScrollGeometry"))
                        .put("WindowFocus", Activity.hasWindowFocus()).put("Admitted", Admitted(Activity))
                        .put("ImeVisible", Activity.window.decorView.rootWindowInsets?.isVisible(android.view.WindowInsets.Type.ime()))
                        .put("Before", Before).put("After", Node(Activity, "ScrollBottom").getJSONArray("Bounds").getDouble(1))
                        .put("InputCounts", Text(Activity, "InputCounts"))
                    instrumentation.sendStatus(1, android.os.Bundle().apply { putString("FlingDiagnostic", Diagnostic.toString()) })
                }
                throw Problem
            }
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B1 E1 C1 K0" }
            Thread.sleep(2100)
            repeat(3) {
                val Scroller = Accessible { it.className == "android.widget.ScrollView" && it.contentDescription?.toString() != "Inner scroll group" }!!
                assertTrue(Scroller.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD))
                Await { Admitted(Activity) }
            }
            Await { Admitted(Activity) && Node(Activity, "ScrollBottom").getJSONArray("Bounds").getDouble(1) == 476.0 }
            val InnerText = Accessible { it.text?.toString() == "Inner top" }!!
            val Inner = InnerText.parent.parent
            assertTrue(Inner.className == "android.widget.ScrollView" && Inner.contentDescription?.toString() == "Inner scroll group")
            assertTrue(Inner.tooltipText?.toString() == "Synthetic nested scroll")
            assertTrue(Inner.parent.className == "android.widget.ScrollView")
            repeat(3) {
                val Current = Accessible { it.contentDescription?.toString() == "Inner scroll group" }!!
                assertTrue(Current.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_FORWARD))
                Await { Admitted(Activity) }
            }
            Await { Admitted(Activity) && Node(Activity, "ScrollBottom").getJSONArray("Bounds").getDouble(1) < 476 }
            val Counter = Accessible { it.text?.toString() == "Count 0" }!!
            assertTrue(Counter.className == "android.widget.Button")
            assertTrue(Counter.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK))
            Await { Admitted(Activity) && ButtonOf(Activity)?.text == "Count 1" }
        } finally { GestureVelocity.recycle(); Finish(Activity) }
    }
    fun testWindowFocusLossCancelsPress() {
        val Activity = Launch()
        var Dialog: android.app.Dialog? = null
        try {
            Await { Ready(Activity) }
            val Point = IntArray(2)
            Main { ButtonOf(Activity)!!.getLocationOnScreen(Point) }
            val Time = android.os.SystemClock.uptimeMillis()
            val Down = android.view.MotionEvent.obtain(Time, Time, android.view.MotionEvent.ACTION_DOWN, Point[0] + 24f, Point[1] + 24f, 0)
            instrumentation.sendPointerSync(Down); Down.recycle()
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B1 E0 C0 K0" }
            Main { Dialog = android.app.Dialog(Activity).apply { setContentView(TextView(Activity).apply { text = "Input gate" }); show() } }
            Await { !Activity.hasWindowFocus() }
            Main { Dialog!!.dismiss(); Dialog = null }
            Await { Admitted(Activity) && Text(Activity, "InputCounts") == "B1 E1 C1 K0" }
            val Up = android.view.MotionEvent.obtain(Time, android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_UP, Point[0] + 24f, Point[1] + 24f, 0)
            instrumentation.sendPointerSync(Up); Up.recycle()
            Main { assertTrue(ButtonOf(Activity)?.text == "Count 0") }
        } finally { Main { Dialog?.dismiss() }; Finish(Activity) }
    }
    private fun Shell(Command: String): String = android.os.ParcelFileDescriptor.AutoCloseInputStream(
        instrumentation.uiAutomation.executeShellCommand(Command)).bufferedReader().use { it.readText().trim() }
    fun testFontScaleAndContrastPreferences() {
        var Activity = Launch()
        val StoredFont = Shell("settings get system font_scale")
        val Font = StoredFont.toFloatOrNull()?.toString() ?: Activity.resources.configuration.fontScale.toString()
        val ContrastAvailable = android.os.Build.VERSION.SDK_INT >= 34
        val Contrast = if (ContrastAvailable) Shell("settings get secure contrast_level") else "null"
        fun Scale(Value: String) {
            if (Shell("settings get system font_scale").toFloatOrNull() == Value.toFloat()) return
            val Monitor = instrumentation.addMonitor(MainActivity::class.java.name, null, false)
            try {
                Shell("settings put system font_scale $Value")
                val Next = instrumentation.waitForMonitorWithTimeout(Monitor, 10000)
                assertNotNull(Next); Activity = Next as MainActivity
                try {
                    Await {
                        Activity = Live(Activity)
                        Ready(Activity)
                    }
                } catch (Failure: Throwable) {
                    Main {
                        android.util.Log.e("LUI", "[LUI:MobileTest] FontSurface ${if (Activity.hasWindowFocus()) 1 else 0} ${if (Admitted(Activity)) 1 else 0} ${if (ButtonOf(Activity) != null) 1 else 0} ${if ((Named(Activity, "Pixel") as? android.widget.ImageView)?.drawable != null) 1 else 0} ${if (Text(Activity, "TaskStatus") == "ABC") 1 else 0}")
                    }
                    throw Failure
                }
            } finally { instrumentation.removeMonitor(Monitor) }
        }
        try {
            assertTrue(StoredFont == "null" || StoredFont.toFloatOrNull() != null)
            assertTrue(Contrast == "null" || Contrast.toFloatOrNull() != null)
            Await { Ready(Activity) }
            Scale("1.3")
            Main {
                val Button = ButtonOf(Activity)!!
                val Expected = android.util.TypedValue.applyDimension(android.util.TypedValue.COMPLEX_UNIT_SP, 14f, Activity.resources.displayMetrics)
                assertTrue(kotlin.math.abs(Button.textSize - Expected) < 1f)
                assertTrue(kotlin.math.abs(Button.height / Activity.resources.displayMetrics.density - 48f) < 1f)
                assertTrue(Button.layout.getEllipsisCount(0) == 0)
            }
            instrumentation.sendStatus(1, android.os.Bundle().apply {
                putString("Preferences", org.json.JSONObject().put("FontScale", 1.3)
                    .put("StoredFontScale", StoredFont).put("InitialEffectiveFontScale", Font.toDouble())
                    .put("Api", android.os.Build.VERSION.SDK_INT)
                    .put("SystemContrast", if (ContrastAvailable) "UiModeManager" else "UnavailableBeforeApi34").toString())
            })
            if (ContrastAvailable) {
                Shell("settings put secure contrast_level 1.0")
                Await { Activity = Live(Activity); Admitted(Activity) && ButtonOf(Activity)?.currentTextColor == android.graphics.Color.WHITE }
                Main {
                    val Background = Named(Activity, "Slot:CounterButton")!!.background as android.graphics.drawable.GradientDrawable
                    assertTrue(Background.color?.defaultColor == android.graphics.Color.BLACK)
                }
            }
        } finally {
            try {
                if (ContrastAvailable) {
                    if (Contrast == "null") Shell("settings delete secure contrast_level") else Shell("settings put secure contrast_level $Contrast")
                }
                Scale(Font)
                Main { assertTrue(kotlin.math.abs(Activity.resources.configuration.fontScale - Font.toFloat()) < 0.001f) }
                if (StoredFont == "null") Shell("settings delete system font_scale")
            } finally { Finish(Activity) }
        }
    }
}
