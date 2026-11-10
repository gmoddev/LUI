package dev.lui.feasibility

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.util.TypedValue
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.*
import org.json.JSONObject
import kotlin.math.roundToInt

// Canonical bounds are window-local. Flat native slots preserve overflow and
// canonical paint contexts without an Android layout/scroll model competing.
internal class Projection(private val Context: Context, private val Root: FrameLayout, private val Session: Session) {
    private val Views = linkedMapOf<Int, Slot>()
    private val Images = ImageProjection(Session)
    private val Semantics = Accessibility(Root, Session)
    private val Density = Context.resources.displayMetrics.density
    private var Applying = false
    private var Admission = false

    private inner class Slot(val NodeId: Int, val Class: String, val Control: View?) : FrameLayout(Context) {
        var StyleRecord: String? = null
        val NativeBackground = Control?.background
        val NativeTextColor = (Control as? TextView)?.textColors
        var Clip = RectF()
        var Input = false
        var Scroller = 0
        var Observation = ""
        val Driver = TouchDriver(this, { Record -> Session.Send(NodeId, Record) }, { X, Y, Token ->
            if (Scroller != 0) Session.Send(Scroller, JSONObject().put("Kind", "Scroll").put("X", X).put("Y", Y).put("Observation", Token))
        }, { Event -> DispatchNative(Event) })
        var Minimum = 0.0
        var Maximum = 100.0
        private val KeysDown = mutableMapOf<String, MutableSet<Int>>()
        init {
            clipChildren = true; clipToPadding = true
            if (Control != null) addView(Control, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        }
        override fun draw(Canvas: Canvas) {
            val Save = Canvas.save(); Canvas.clipRect(Clip)
            super.draw(Canvas); Canvas.restoreToCount(Save)
        }
        override fun dispatchTouchEvent(Event: MotionEvent): Boolean {
            if (Event.actionMasked == MotionEvent.ACTION_DOWN && (!Admission || !Input || !Clip.contains(Event.x, Event.y))) return false
            if (Event.actionMasked == MotionEvent.ACTION_DOWN) Views.values.forEach { it.Driver.InterruptFling() }
            return Driver.Handle(Event, Admission && Input) ?: DispatchNative(Event)
        }
        private fun DispatchNative(Event: MotionEvent): Boolean {
            if (Control !is Button) return super.dispatchTouchEvent(Event)
            // Android 11 TextView filters releases after a pointer-id handoff,
            // even for buttons. The native button gets one continuous contact;
            // Driver above still observes every original pointer and identity.
            val Action = when (Event.actionMasked) {
                MotionEvent.ACTION_DOWN -> MotionEvent.ACTION_DOWN
                MotionEvent.ACTION_UP -> MotionEvent.ACTION_UP
                MotionEvent.ACTION_CANCEL -> MotionEvent.ACTION_CANCEL
                else -> MotionEvent.ACTION_MOVE
            }
            val Properties = MotionEvent.PointerProperties().apply { id = 0; toolType = Event.getToolType(0) }
            val Coordinates = MotionEvent.PointerCoords().also { Event.getPointerCoords(0, it) }
            val Native = MotionEvent.obtain(Event.downTime, Event.eventTime, Action, 1,
                arrayOf(Properties), arrayOf(Coordinates), Event.metaState, Event.buttonState,
                Event.xPrecision, Event.yPrecision, Event.deviceId, Event.edgeFlags, Event.source, Event.flags)
            return try { super.dispatchTouchEvent(Native) } finally { Native.recycle() }
        }
        override fun onTouchEvent(Event: MotionEvent): Boolean = Input && Admission
        override fun dispatchKeyEvent(Event: android.view.KeyEvent): Boolean {
            val Name = Keys.Name(Event.keyCode)
            if (Name != null && (Admission || Event.action == android.view.KeyEvent.ACTION_UP)) {
                val Codes = KeysDown[Name]
                val Phase = if (Event.action == android.view.KeyEvent.ACTION_UP) {
                    if (Codes?.remove(Event.keyCode) == true && Codes.isEmpty()) { KeysDown.remove(Name); 2 } else -1
                } else if (Event.action == android.view.KeyEvent.ACTION_DOWN && Event.repeatCount == 0) {
                    if (Codes == null && KeysDown.size >= 16) { Session.PresentationFailed(); -1 }
                    else { val New = Codes == null; KeysDown.getOrPut(Name) { mutableSetOf() }.add(Event.keyCode); if (New) 0 else -1 }
                } else if (Event.repeatCount > 0 && Codes?.contains(Event.keyCode) == true) 1 else -1
                if (Phase >= 0) Session.Send(NodeId, JSONObject().put("Kind", "Key").put("Phase", Phase).put("Key", Name))
            }
            return super.dispatchKeyEvent(Event)
        }
        fun Cancel() {
            Driver.Cancel(); Control?.isPressed = false
            KeysDown.keys.toList().forEach { Session.Send(NodeId, JSONObject().put("Kind", "Key").put("Phase", 2).put("Key", it)) }
            KeysDown.clear()
        }
    }
    private fun Create(Id: Int, Class: String): Slot {
        val Control: View? = when (Class) {
            "Window", "Frame", "ScrollingFrame" -> null
            "TextLabel" -> TextView(Context)
            "TextButton" -> Button(Context).apply { isAllCaps = false }
            "CheckBox" -> CheckBox(Context)
            "Slider" -> SeekBar(Context)
            "ProgressBar" -> ProgressBar(Context, null, android.R.attr.progressBarStyleHorizontal)
            "TextBox" -> ObservedEditor(Context, { Record -> if (!Applying) Session.Send(Id, Record) }, { Session.PresentationFailed() })
            "ImageLabel" -> ImageView(Context).apply { scaleType = ImageView.ScaleType.FIT_XY }
            else -> error("[LUI:Mobile] Class")
        }
        Control?.minimumWidth = 0; Control?.minimumHeight = 0
        Control?.id = View.generateViewId()
        val Slot = Slot(Id, Class, Control)
        if (Control != null && Control !is ObservedEditor) Control.onFocusChangeListener = View.OnFocusChangeListener { _, Value ->
            if (!Applying) Session.Send(Id, JSONObject().put("Kind", "Focus").put("Value", Value))
        }
        if (Control is Button) Control.setOnClickListener {
            if (!Applying && Admission) Session.Activate(Id)
        }
        if (Control is CheckBox) Control.setOnClickListener {
            if (!Applying && Admission) Session.Send(Id, JSONObject().put("Kind", "Checked").put("Value", Control.isChecked))
        }
        if (Control is SeekBar) Control.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onStartTrackingTouch(Bar: SeekBar) { }
            override fun onStopTrackingTouch(Bar: SeekBar) { }
            override fun onProgressChanged(Bar: SeekBar, Value: Int, FromUser: Boolean) {
                if (FromUser && !Applying && Admission) {
                    Session.Send(Id, JSONObject().put("Kind", "Value").put("Value", Slot.Minimum + (Slot.Maximum - Slot.Minimum) * Value / 10000))
                }
            }
        })
        Root.addView(Slot); return Slot
    }
    fun Admission(Value: Boolean) {
        Admission = Value
    }
    fun Suspend() {
        Admission(false)
        Views.values.forEach { it.Cancel(); (it.Control as? ObservedEditor)?.Suspend(); it.Control?.clearFocus() }
    }
    fun Apply(Frame: JSONObject) {
        Images.Begin()
        Applying = true
        try {
            val Array = Frame.getJSONArray("Nodes")
            check(Array.length() in 1..128)
            val Nodes = (0 until Array.length()).map { Array.getJSONObject(it) }
            check(Nodes.count { it.getString("Class") == "Window" } == 1)
            val Present = Nodes.map { it.getInt("Id") }.toSet()
            for (Id in Views.keys.toList()) if (!Present.contains(Id)) Remove(Id)
            val ById = Nodes.associateBy { it.getInt("Id") }
            for (Node in Nodes) {
                val Id = Node.getInt("Id"); val Class = Node.getString("Class")
                val Slot = Views[Id] ?: Create(Id, Class).also { Views[Id] = it }
                check(Slot.Class == Class)
                val Bounds = Node.getJSONArray("Bounds")
                val Props = Node.getJSONObject("Properties")
                val Presentation = Node.getJSONObject("Presentation")
                val Rect = Presentation.getJSONArray("Rect")
                val Width = Bounds.getDouble(2); val Height = Bounds.getDouble(3)
                check(Width.isFinite() && Height.isFinite() && Width >= 0 && Height >= 0 && Width <= 1000000 && Height <= 1000000)
                val PixelWidth = (Width * Density).roundToInt(); val PixelHeight = (Height * Density).roundToInt()
                val PixelLeft = (Bounds.getDouble(0) * Density).roundToInt(); val PixelTop = (Bounds.getDouble(1) * Density).roundToInt()
                val Previous = Slot.layoutParams as? FrameLayout.LayoutParams
                if (Previous == null || Previous.width != PixelWidth || Previous.height != PixelHeight ||
                    Previous.leftMargin != PixelLeft || Previous.topMargin != PixelTop) {
                    Slot.layoutParams = FrameLayout.LayoutParams(PixelWidth, PixelHeight).apply {
                        gravity = Gravity.TOP or Gravity.LEFT; leftMargin = PixelLeft; topMargin = PixelTop
                    }
                }
                val Clip = RectF((Rect.getDouble(0) * Density).toFloat(), (Rect.getDouble(1) * Density).toFloat(),
                    ((Rect.getDouble(0) + Rect.getDouble(2)) * Density).toFloat(), ((Rect.getDouble(1) + Rect.getDouble(3)) * Density).toFloat())
                if (Slot.Clip != Clip) { Slot.Clip = Clip; Slot.invalidate() }
                Slot.visibility = if (Node.getBoolean("Shown")) View.VISIBLE else View.GONE
                Slot.Input = Presentation.getInt("Input") != 0
                Slot.Observation = Node.getString("Observation")
                Slot.alpha = Node.getDouble("Opacity").toFloat()
                Slot.tag = "Slot:" + Node.getString("Name")
                Slot.Control?.tag = Node.getString("Name")
                var Parent = Node
                Slot.Scroller = 0
                while (true) {
                    if (Parent.getString("Class") == "ScrollingFrame") { Slot.Scroller = Parent.getInt("Id"); break }
                    Parent = ById[Parent.getInt("Parent")] ?: break
                }
                val Control = Slot.Control
                Control?.isEnabled = Slot.Input
                if (Control is ObservedEditor) Control.Apply(Node.getJSONObject("TextState"))
                else if (Control is TextView && Control.text.toString() != Props.optString("Text")) Control.text = Props.optString("Text")
                if (Control is CheckBox) Control.isChecked = Props.optString("Checked") == "true"
                if (Control is ProgressBar) {
                    val Low = Props.optString("Minimum", "0").toDouble()
                    val High = Props.optString("Maximum", "100").toDouble()
                    val Value = Props.optString("Value", "0").toDouble()
                    Control.max = 10000
                    Control.progress = if (High == Low) 0 else ((Value - Low) / (High - Low) * 10000).roundToInt().coerceIn(0, 10000)
                    Slot.Minimum = Low; Slot.Maximum = High
                }
                if (Props.has("__Style")) {
                    val Record = Props.getString("__Style")
                    if (Slot.StyleRecord != Record) { Style(Slot, JSONObject(Record)); Slot.StyleRecord = Record }
                }
                if (Control is ImageView) Images.Apply(Id, Node, Control)
            }
            // Avoid reshuffling every native sibling on an unchanged snapshot.
            // Records already follow canonical context/preorder.
            if (Nodes.withIndex().any { (Index, Node) -> Root.getChildAt(Index) !== Views.getValue(Node.getInt("Id")) }) {
                Nodes.forEach { Views.getValue(it.getInt("Id")).bringToFront() }
            }
            // Acknowledgment must cover hit geometry, not just a pending layout
            // request. The later Android traversal repeats these same rectangles.
            for (Slot in Views.values) {
                val Params = Slot.layoutParams as FrameLayout.LayoutParams
                Slot.measure(View.MeasureSpec.makeMeasureSpec(Params.width, View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(Params.height, View.MeasureSpec.EXACTLY))
                val Left = Root.paddingLeft + Params.leftMargin; val Top = Root.paddingTop + Params.topMargin
                Slot.layout(Left, Top, Left + Params.width, Top + Params.height)
            }
            for (Slot in Views.values) {
                val Scroller = Views[Slot.Scroller]
                Slot.Driver.Update(Slot.Observation, Scroller?.Observation ?: "", Slot.Input,
                    Scroller?.Input == true && Slot.Class !in listOf("TextBox", "Slider"))
            }
            Semantics.Apply(Nodes.map { Node ->
                val Slot = Views.getValue(Node.getInt("Id"))
                Accessibility.Entry(Slot.NodeId, Node.getInt("Parent"), Slot, Slot.Control, RectF(Slot.Clip),
                    Node.getJSONObject("Properties"), Slot.Input, Slot.Class == "ScrollingFrame", Slot.Observation)
            })
        } finally { Applying = false; Images.End() }
    }
    private fun Style(Slot: Slot, Style: JSONObject) {
        fun ColorValue(Name: String, Default: Int): Int {
            if (Style.isNull(Name)) return Default
            val Channels = Style.getJSONArray(Name)
            return Color.rgb((Channels.getDouble(0) * 255).roundToInt(), (Channels.getDouble(1) * 255).roundToInt(), (Channels.getDouble(2) * 255).roundToInt())
        }
        val Contrast = Style.getBoolean("HighContrast")
        Slot.Control?.background = if (Style.isNull("BackgroundColor3")) Slot.NativeBackground else null
        Slot.background = null
        if (Contrast || !Style.isNull("BackgroundColor3") || Style.getDouble("BorderSizePixel") > 0 || Slot.Control == null) {
            Slot.background = GradientDrawable().apply {
                val Background = if (Contrast) Color.BLACK else ColorValue("BackgroundColor3", Color.TRANSPARENT)
                setColor((Background and 0x00ffffff) or (((Color.alpha(Background) * (1 - Style.getDouble("BackgroundTransparency"))).roundToInt()) shl 24))
                cornerRadius = (Style.getDouble("CornerRadius") * Density).toFloat()
                val Outline = if (Contrast && (Slot.Control is Button || Slot.Control is EditText || Slot.Control is SeekBar))
                    maxOf(1.0, Style.getDouble("BorderSizePixel")) else Style.getDouble("BorderSizePixel")
                setStroke((Outline * Density).roundToInt(), if (Contrast) Color.WHITE else ColorValue("BorderColor3", Color.TRANSPARENT))
            }
            // Explicit decoration replaces the toolkit background, not geometry.
            if (Contrast || !Style.isNull("BackgroundColor3")) Slot.Control?.background = null
        }
        val Text = Slot.Control as? TextView ?: return
        if (Contrast || !Style.isNull("TextColor3")) Text.setTextColor(if (Contrast) Color.WHITE else ColorValue("TextColor3", Color.BLACK))
        else Text.setTextColor(Slot.NativeTextColor)
        Text.setTextSize(TypedValue.COMPLEX_UNIT_SP, Style.getDouble("TextSize").toFloat())
        val Family = Style.getString("FontFamily")
        Text.typeface = Typeface.create(if (Family == "System") null else Family, if (Style.getString("FontWeight") == "Bold") Typeface.BOLD else Typeface.NORMAL)
        Text.gravity = (when (Style.getString("TextXAlignment")) { "Center" -> Gravity.CENTER_HORIZONTAL; "Right" -> Gravity.RIGHT; else -> Gravity.LEFT }) or
            (when (Style.getString("TextYAlignment")) { "Center" -> Gravity.CENTER_VERTICAL; "Bottom" -> Gravity.BOTTOM; else -> Gravity.TOP })
        Text.setHorizontallyScrolling(!Style.getBoolean("TextWrapped"))
    }
    private fun Remove(Id: Int) {
        val Slot = Views.remove(Id) ?: return
        Slot.Cancel(); (Slot.Control as? ObservedEditor)?.Retire()
        Slot.Control?.setOnClickListener(null); Slot.Control?.onFocusChangeListener = null
        Slot.accessibilityDelegate = null; Slot.Control?.accessibilityDelegate = null
        (Slot.Control as? CheckBox)?.setOnCheckedChangeListener(null)
        (Slot.Control as? SeekBar)?.setOnSeekBarChangeListener(null)
        Images.Remove(Id); Root.removeView(Slot)
    }
    fun Retire() { Suspend(); Semantics.Retire(); Views.keys.toList().forEach { Remove(it) }; Images.Retire() }
    fun Metrics(): JSONObject = JSONObject().put("Views", Views.size).put("Images", Images.Metrics())
}
