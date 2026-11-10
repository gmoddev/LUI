package dev.lui.feasibility

import android.app.Activity
import android.os.Bundle
import android.os.Looper
import android.view.View
import android.view.WindowInsets
import android.widget.FrameLayout
import android.widget.TextView
import org.json.JSONObject
import kotlin.math.roundToInt

open class MainActivity : Activity() {
    protected open val BundleName: String get() = "Fixture.json"
    private lateinit var Root: FrameLayout
    private lateinit var OwnerSession: Session
    private var Active = false
    private var Attached = false
    private var LastWidth = 0
    private var LastHeight = 0
    private lateinit var Renderer: Projection
    private var GeometryDirty = false
    private var Failed = false
    private var OwnsSession = false
    private var RefreshHz = 60.0
    private var Insets: WindowInsets? = null
    private var ContrastListener: android.app.UiModeManager.ContrastChangeListener? = null

    override fun onCreate(Saved: Bundle?) {
        super.onCreate(Saved)
        window.setDecorFitsSystemWindows(false)
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE)
        RefreshHz = display?.refreshRate?.toDouble()?.coerceAtLeast(1.0) ?: 60.0
        // Canonical state lives in the warm owner or the explicit application
        // checkpoint. Do not serialize native editor/password/preedit state.
        Root = object : FrameLayout(this) {
            override fun dispatchSaveInstanceState(Container: android.util.SparseArray<android.os.Parcelable>) { }
            override fun dispatchRestoreInstanceState(Container: android.util.SparseArray<android.os.Parcelable>) { }
        }.apply { isFocusableInTouchMode = true; requestFocus() }
        setContentView(Root)
        OwnerSession = Session.GetSession(application, BundleName)
        Renderer = Projection(this, Root, OwnerSession)
        OwnsSession = OwnerSession.MatchesBundle(BundleName)
        if (!OwnsSession) { Fail(); return }
        window.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
        Root.setOnApplyWindowInsetsListener { _, Insets ->
            this.Insets = Insets
            Root.post { UpdateInsets(); AttachIfReady() }
            Insets
        }
        Root.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> UpdateInsets(); AttachIfReady() }
        if (android.os.Build.VERSION.SDK_INT >= 34) {
            ContrastListener = android.app.UiModeManager.ContrastChangeListener {
                if (Active) { Attached = false; Renderer.Suspend(); AttachIfReady() }
            }.also { getSystemService(android.app.UiModeManager::class.java).addContrastChangeListener(mainExecutor, it) }
        }
    }
    override fun onResume() { super.onResume(); Active = true; Root.post { AttachIfReady() } }
    override fun onPause() {
        Active = false; Attached = false; Renderer.Suspend()
        Root.foreground = android.graphics.drawable.ColorDrawable(android.graphics.Color.BLACK)
        Root.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
        OwnerSession.Detach(this); super.onPause()
    }
    override fun onDestroy() {
        if (android.os.Build.VERSION.SDK_INT >= 34) ContrastListener?.let {
            getSystemService(android.app.UiModeManager::class.java).removeContrastChangeListener(it)
        }
        OwnerSession.Detach(this)
        Renderer.Retire()
        if (isFinishing && OwnsSession) OwnerSession.Retire()
        super.onDestroy()
    }
    override fun onWindowFocusChanged(Focused: Boolean) {
        super.onWindowFocusChanged(Focused)
        if (!::Renderer.isInitialized) return
        if (!Focused) { Attached = false; Renderer.Suspend(); OwnerSession.Detach(this) }
        else if (Active) Root.post { AttachIfReady() }
    }
    private fun AttachIfReady() {
        val Width = Root.width - Root.paddingLeft - Root.paddingRight
        val Height = Root.height - Root.paddingTop - Root.paddingBottom
        if (Failed || !Active || !hasWindowFocus() || Width <= 0 || Height <= 0 || (Attached && !GeometryDirty && Width == LastWidth && Height == LastHeight)) return
        LastWidth = Width; LastHeight = Height; Attached = true; GeometryDirty = false
        Renderer.Suspend()
        val Density = resources.displayMetrics.density.toDouble()
        val Dark = resources.configuration.uiMode and android.content.res.Configuration.UI_MODE_NIGHT_MASK == android.content.res.Configuration.UI_MODE_NIGHT_YES
        val HighContrast = android.os.Build.VERSION.SDK_INT >= 34 && getSystemService(android.app.UiModeManager::class.java).contrast >= 0.5f
        OwnerSession.Attach(this, Width / Density, Height / Density, if (HighContrast) 2 else if (Dark) 1 else 0)
    }
    private fun UpdateInsets() {
        val Value = Insets ?: return
        if (Root.width <= 0 || Root.height <= 0) return
        val Safe = Value.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
        val Keyboard = Value.getInsets(WindowInsets.Type.ime())
        val Bounds = windowManager.currentWindowMetrics.bounds
        val Location = IntArray(2); Root.getLocationOnScreen(Location)
        // Intersect actual root bounds with the usable window. If adjustResize
        // already reduced the root, keyboard occlusion is not subtracted twice.
        val Left = (Bounds.left + Safe.left - Location[0]).coerceIn(0, Root.width)
        val Top = (Bounds.top + Safe.top - Location[1]).coerceIn(0, Root.height)
        val Right = (Location[0] + Root.width - (Bounds.right - Safe.right)).coerceIn(0, Root.width)
        val Bottom = (Location[1] + Root.height - (Bounds.bottom - maxOf(Safe.bottom, Keyboard.bottom))).coerceIn(0, Root.height)
        if (Root.paddingLeft != Left || Root.paddingTop != Top || Root.paddingRight != Right || Root.paddingBottom != Bottom) {
            Renderer.Suspend(); GeometryDirty = true; Root.setPadding(Left, Top, Right, Bottom)
        }
    }
    internal fun Apply(Frame: JSONObject) {
        check(Looper.myLooper() == Looper.getMainLooper())
        Renderer.Apply(Frame)
    }
    internal fun Admission(Ready: Boolean) {
        val Interactive = Active && hasWindowFocus() && Ready
        Renderer.Admission(Interactive)
        if (Interactive) { Root.foreground = null; Root.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES }
    }
    internal fun Fail() {
        Failed = true
        Renderer.Retire(); Root.removeAllViews()
        Root.addView(TextView(this).apply { text = "LUI session stopped" })
    }
    internal fun PresentationMetrics(): JSONObject = Renderer.Metrics().put("RefreshHz", RefreshHz)
}

// Fixed signed assets, selected by internal activities, never by Intent payload.
class ConversationActivity : MainActivity() { override val BundleName get() = "Conversation.json" }
class ModularActivity : MainActivity() { override val BundleName get() = "Modular.json" }
