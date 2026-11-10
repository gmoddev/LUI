package dev.lui.feasibility

import android.graphics.Rect
import android.graphics.RectF
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import org.json.JSONObject
import kotlin.math.ceil
import kotlin.math.floor

// A canonical semantic tree over flat visual slots. Native controls still supply
// roles, password/editing behavior and actions; no parallel virtual control model.
internal class Accessibility(private val Root: ViewGroup, private val Session: Session) {
    data class Entry(val Id: Int, val Parent: Int, val Slot: ViewGroup, val Control: View?, val Clip: RectF,
        val Props: JSONObject, val Input: Boolean, val Scroller: Boolean, val Observation: String)
    private var Entries = emptyMap<Int, Entry>()
    init {
        Root.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        Root.accessibilityDelegate = object : View.AccessibilityDelegate() {
            override fun onInitializeAccessibilityNodeInfo(Host: View, Info: AccessibilityNodeInfo) {
                super.onInitializeAccessibilityNodeInfo(Host, Info)
                for (Index in 0 until Root.childCount) Info.removeChild(Root.getChildAt(Index))
                Entries.values.filter { !Entries.containsKey(it.Parent) }.forEach { Info.addChild(it.Slot) }
            }
        }
    }
    private fun Bounds(Entry: Entry): Rect {
        val Location = IntArray(2); Entry.Slot.getLocationOnScreen(Location)
        if (Entry.Clip.isEmpty) return Rect(Location[0], Location[1], Location[0], Location[1])
        return Rect(Location[0] + floor(Entry.Clip.left.coerceAtLeast(0f)).toInt(),
            Location[1] + floor(Entry.Clip.top.coerceAtLeast(0f)).toInt(),
            Location[0] + ceil(Entry.Clip.right.coerceAtMost(Entry.Slot.width.toFloat())).toInt(),
            Location[1] + ceil(Entry.Clip.bottom.coerceAtMost(Entry.Slot.height.toFloat())).toInt())
    }
    fun Apply(Values: List<Entry>) {
        val Previous = Entries
        Entries = Values.associateBy { it.Id }
        for (Entry in Values) {
            Entry.Slot.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
            val Label = Entry.Props.optString("AccessibilityLabel").ifEmpty { null }
            val Description = Entry.Props.optString("AccessibilityDescription").ifEmpty { null }
            if (Entry.Control?.contentDescription?.toString() != Label) Entry.Control?.contentDescription = Label
            if (Entry.Control?.tooltipText?.toString() != Description) Entry.Control?.tooltipText = Description
            if (Previous[Entry.Id]?.Slot === Entry.Slot) continue
            val Id = Entry.Id
            Entry.Slot.accessibilityDelegate = object : View.AccessibilityDelegate() {
                override fun onInitializeAccessibilityNodeInfo(Host: View, Info: AccessibilityNodeInfo) {
                    super.onInitializeAccessibilityNodeInfo(Host, Info)
                    val Current = Entries[Id] ?: return
                    Info.setParent(Entries[Current.Parent]?.Slot ?: Root)
                    Info.isFocusable = false
                    Info.isVisibleToUser = Host.isShown && !Current.Clip.isEmpty
                    Info.setBoundsInScreen(Bounds(Current))
                    if (Current.Control == null) {
                        Info.contentDescription = Current.Props.optString("AccessibilityLabel").ifEmpty { null }
                        Info.tooltipText = Current.Props.optString("AccessibilityDescription").ifEmpty { null }
                    }
                    Entries.values.filter { it.Parent == Id }.forEach { Info.addChild(it.Slot) }
                    if (Current.Scroller) {
                        Info.className = "android.widget.ScrollView"; Info.isScrollable = Current.Input
                        if (Current.Input) {
                            Info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD)
                            Info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD)
                        }
                    }
                }
                override fun performAccessibilityAction(Host: View, Action: Int, Arguments: android.os.Bundle?): Boolean {
                    val Current = Entries[Id] ?: return false
                    if (Current.Input && Current.Scroller &&
                        Action in listOf(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD, AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD)) {
                        val Direction = if (Action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD) 1 else -1
                        val Delta = Current.Slot.height / Current.Slot.resources.displayMetrics.density * 0.8 * Direction
                        Session.Send(Id, JSONObject().put("Kind", "Scroll").put("X", 0).put("Y", Delta).put("Observation", Current.Observation))
                        return true
                    }
                    return super.performAccessibilityAction(Host, Action, Arguments)
                }
            }
            Entry.Control?.accessibilityDelegate = object : View.AccessibilityDelegate() {
                override fun onInitializeAccessibilityNodeInfo(Host: View, Info: AccessibilityNodeInfo) {
                    super.onInitializeAccessibilityNodeInfo(Host, Info)
                    val Current = Entries[Id] ?: return
                    Info.setParent(Current.Slot); Info.setBoundsInScreen(Bounds(Current))
                    Info.isVisibleToUser = Host.isShown && !Current.Clip.isEmpty
                    Info.tooltipText = Current.Props.optString("AccessibilityDescription").ifEmpty { null }
                }
            }
        }
        // The logical parents differ from the flat native hierarchy. Reused
        // delegates read current records, but connected services also need a
        // subtree invalidation to discard cached relationships and bounds.
        if (Root.isAttachedToWindow && Root.context.getSystemService(android.view.accessibility.AccessibilityManager::class.java).isEnabled) {
            Root.sendAccessibilityEventUnchecked(android.view.accessibility.AccessibilityEvent.obtain(
                android.view.accessibility.AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED).apply {
                contentChangeTypes = android.view.accessibility.AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE
            })
        }
    }
    fun Retire() {
        for (Entry in Entries.values) { Entry.Slot.accessibilityDelegate = null; Entry.Control?.accessibilityDelegate = null }
        Entries = emptyMap(); Root.accessibilityDelegate = null
    }
}
