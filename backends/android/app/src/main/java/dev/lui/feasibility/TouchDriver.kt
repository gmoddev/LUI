package dev.lui.feasibility

import android.os.SystemClock
import android.view.MotionEvent
import android.view.VelocityTracker
import android.view.View
import android.view.ViewConfiguration
import android.widget.OverScroller
import org.json.JSONObject

// Coordinates are observed once in the slot. Scrolling reports deltas to LUI;
// OverScroller supplies only bounded velocity decay, never a native content offset.
internal class TouchDriver(private val Target: View, private val Send: (JSONObject) -> Unit,
    private val Scroll: (Double, Double, String) -> Unit, private val CancelNative: (MotionEvent) -> Unit) {
    private data class Press(val Device: Int, val Observation: String)
    private val Presses = linkedMapOf<Int, Press>()
    private val Configuration = ViewConfiguration.get(Target.context)
    private val Density = Target.resources.displayMetrics.density
    private val Flinger = OverScroller(Target.context)
    private var Velocity: VelocityTracker? = null
    private var Primary = -1
    private var LastX = 0f
    private var LastY = 0f
    private var ScrollObservation = ""
    private var FlingX = 0
    private var FlingY = 0
    private var FlingDeadline = 0L
    var Dragging = false; private set
    private var Geometry = ""
    private var ScrollGeometry = ""
    private var CanScroll = false
    val Tracking get() = Primary >= 0
    private val Tick = object : Runnable {
        override fun run() {
            if (SystemClock.uptimeMillis() >= FlingDeadline || !CanScroll || ScrollGeometry != ScrollObservation || !Flinger.computeScrollOffset()) { StopFling(); return }
            val X = Flinger.currX; val Y = Flinger.currY
            if (X != FlingX || Y != FlingY) Scroll((X - FlingX).toDouble() / Density, (Y - FlingY).toDouble() / Density, ScrollObservation)
            FlingX = X; FlingY = Y; Target.postDelayed(this, 16)
        }
    }
    fun Update(Observation: String, ScrollToken: String, Enabled: Boolean, ScrollEnabled: Boolean) {
        if (((Dragging || !Flinger.isFinished) && (!ScrollEnabled || ScrollToken != ScrollObservation)) ||
            (!Dragging && Flinger.isFinished && Geometry.isNotEmpty() && (Geometry != Observation || !Enabled))) Cancel()
        if (ScrollGeometry != ScrollToken || !ScrollEnabled) StopFling()
        Geometry = Observation; ScrollGeometry = ScrollToken; CanScroll = ScrollEnabled
    }
    private fun Pointer(Event: MotionEvent, Index: Int, Phase: Int) {
        val Id = Event.getPointerId(Index)
        if (Phase == 0) {
            if (Presses.size >= 32 || Presses.containsKey(Id)) return
            val Device = when (Event.getToolType(Index)) { MotionEvent.TOOL_TYPE_STYLUS, MotionEvent.TOOL_TYPE_ERASER -> 1; MotionEvent.TOOL_TYPE_MOUSE -> 0; else -> 2 }
            Presses[Id] = Press(Device, Geometry)
        }
        val Press = Presses[Id] ?: return
        Send(JSONObject().put("Kind", "Pointer").put("Phase", Phase).put("Device", Press.Device).put("PointerId", Id)
            .put("X", Event.getX(Index).toDouble() / Density).put("Y", Event.getY(Index).toDouble() / Density).put("Observation", Press.Observation))
        if (Phase >= 2) Presses.remove(Id)
    }
    private fun EndPointers() {
        for ((Id, Press) in Presses) Send(JSONObject().put("Kind", "Pointer").put("Phase", 3).put("Device", Press.Device)
            .put("PointerId", Id).put("X", 0).put("Y", 0).put("Observation", Press.Observation))
        Presses.clear()
    }
    // null delegates to the native widget; a drag consumes its native click path.
    fun Handle(Event: MotionEvent, Admit: Boolean): Boolean? {
        if (Event.actionMasked == MotionEvent.ACTION_DOWN) {
            if (!Admit) return false
            Cancel(); Primary = Event.getPointerId(0); LastX = Event.rawX; LastY = Event.rawY
            ScrollObservation = ScrollGeometry; Velocity = VelocityTracker.obtain()
        } else if (!Tracking) return false
        val Absolute = MotionEvent.obtain(Event).apply { offsetLocation(Event.rawX - Event.x, Event.rawY - Event.y) }
        Velocity?.addMovement(Absolute); Absolute.recycle()
        when (Event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> if (!Dragging) Pointer(Event, Event.actionIndex, 0)
            MotionEvent.ACTION_MOVE -> {
                val Index = Event.findPointerIndex(Primary)
                if (Index < 0) { Cancel(); return true }
                val X = Event.getRawX(Index); val Y = Event.getRawY(Index)
                val DeltaX = LastX - X; val DeltaY = LastY - Y
                if (CanScroll && (Dragging || kotlin.math.abs(DeltaX) + kotlin.math.abs(DeltaY) > Configuration.scaledTouchSlop)) {
                    if (!Dragging) {
                        EndPointers()
                        val Cancel = MotionEvent.obtain(Event).apply { action = MotionEvent.ACTION_CANCEL }
                        CancelNative(Cancel); Cancel.recycle(); Dragging = true
                    }
                    Scroll(DeltaX.toDouble() / Density, DeltaY.toDouble() / Density, ScrollObservation)
                    LastX = X; LastY = Y; return true
                }
                for (IndexValue in 0 until Event.pointerCount) Pointer(Event, IndexValue, 1)
            }
            MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_UP -> {
                val Id = Event.getPointerId(Event.actionIndex)
                val Canceled = Event.flags and MotionEvent.FLAG_CANCELED != 0
                if (!Dragging) Pointer(Event, Event.actionIndex, if (Canceled) 3 else 2)
                if (Event.actionMasked == MotionEvent.ACTION_UP) {
                    val Consumed = Dragging || Canceled
                    if (Canceled) {
                        val Cancel = MotionEvent.obtain(Event).apply { action = MotionEvent.ACTION_CANCEL }
                        CancelNative(Cancel); Cancel.recycle()
                    }
                    if (Dragging && !Canceled) {
                        Velocity?.computeCurrentVelocity(1000, Configuration.scaledMaximumFlingVelocity.toFloat())
                        val X = -(Velocity?.getXVelocity(Primary) ?: 0f).toInt()
                        val Y = -(Velocity?.getYVelocity(Primary) ?: 0f).toInt()
                        if (kotlin.math.abs(X) + kotlin.math.abs(Y) >= Configuration.scaledMinimumFlingVelocity) {
                            FlingX = 0; FlingY = 0; FlingDeadline = SystemClock.uptimeMillis() + 2000
                            Flinger.fling(0, 0, X, Y, -1000000, 1000000, -1000000, 1000000)
                            Target.postDelayed(Tick, 16)
                        }
                    }
                    Primary = -1; Dragging = false; Velocity?.recycle(); Velocity = null
                    if (Consumed) return true
                } else if (Id == Primary) {
                    val Next = if (Event.actionIndex == 0) 1 else 0
                    Primary = Event.getPointerId(Next); LastX = Event.getRawX(Next); LastY = Event.getRawY(Next)
                }
            }
            MotionEvent.ACTION_CANCEL -> { Cancel(); return true }
        }
        return if (Dragging) true else null
    }
    private fun StopFling() { Target.removeCallbacks(Tick); Flinger.forceFinished(true) }
    fun InterruptFling() { StopFling() }
    fun Cancel() {
        StopFling(); EndPointers(); Primary = -1; Dragging = false; Velocity?.recycle(); Velocity = null
        val Time = SystemClock.uptimeMillis()
        val Event = MotionEvent.obtain(Time, Time, MotionEvent.ACTION_CANCEL, 0f, 0f, 0)
        CancelNative(Event); Event.recycle(); Target.isPressed = false
    }
}
