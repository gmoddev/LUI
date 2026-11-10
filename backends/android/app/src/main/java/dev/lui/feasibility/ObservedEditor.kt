package dev.lui.feasibility

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.text.InputType
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputConnectionWrapper
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import org.json.JSONObject

// Observed editing: a reply to an older edit cannot replace newer local input.
internal class ObservedEditor(Context: Context, private val Send: (JSONObject) -> Unit, private val Fail: () -> Unit) : EditText(Context) {
    private val Main = Handler(Looper.getMainLooper())
    private var Applying = false
    private var Retired = false
    private var Observed = "0"
    private var Geometry = ""
    private var Secret = false
    private var Composing = false
    private var Last = ""
    private var Ready = false
    private var Maximum = 65534
    private var LocalEdit = 0
    private var PendingEdit = 0
    private var Dirty = false
    private var Batch = 0
    private var ConnectionEpoch = 0
    private var Canonical = ""
    private var CanonicalLeft = 0
    private var CanonicalRight = 0
    private val Flush = Runnable { Report() }
    init {
        addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(Text: CharSequence?, Start: Int, Count: Int, After: Int) { }
            override fun onTextChanged(Text: CharSequence?, Start: Int, Before: Int, Count: Int) { }
            override fun afterTextChanged(Value: Editable?) { Queue() }
        })
        onFocusChangeListener = android.view.View.OnFocusChangeListener { _, Value ->
            if (!Applying && !Retired) {
                if (!Value) {
                    val Preedit = BaseInputConnection.getComposingSpanStart(text) >= 0
                    if (!Preedit && Dirty) Report()
                    Main.removeCallbacks(Flush); InvalidateConnection(); Dirty = false
                    Applying = true
                    try {
                        BaseInputConnection.removeComposingSpans(text)
                        if (Preedit) Restore()
                        Composing = false
                    } finally { Applying = false }
                }
                Send(JSONObject().put("Kind", "Focus").put("Value", Value))
            }
        }
        Ready = true
        filters = arrayOf(android.text.InputFilter { Source, Start, End, Dest, Left, Right ->
            if (Applying) null
            else if (End - Start > 131068) Dest.subSequence(Left, Right)
            else {
                val Candidate = Dest.subSequence(0, Left).toString() + Source.subSequence(Start, End) + Dest.subSequence(Right, Dest.length)
                if (Candidate.codePointCount(0, Candidate.length) > Maximum || Candidate.toByteArray(Charsets.UTF_8).size > 65534 ||
                    Candidate.codePoints().anyMatch { it == 0 || it in 0xD800..0xDFFF }) Dest.subSequence(Left, Right) else null
            }
        })
    }
    override fun onSelectionChanged(Start: Int, End: Int) { super.onSelectionChanged(Start, End); if (Ready) Queue() }
    private fun Queue() {
        if (Applying || Retired) return
        Dirty = true
        Main.removeCallbacks(Flush)
        if (Batch == 0) Main.post(Flush)
    }
    private fun Emit(Record: JSONObject) {
        if (LocalEdit == Int.MAX_VALUE) { Fail(); return }
        PendingEdit = ++LocalEdit
        Send(Record.put("EditId", PendingEdit).put("Observation", Geometry))
    }
    private fun Report() {
        if (Applying || Retired || Observed == "0" || Batch != 0) return
        Dirty = false
        val Value = text ?: return
        val Start = BaseInputConnection.getComposingSpanStart(Value)
        val End = BaseInputConnection.getComposingSpanEnd(Value)
        if (Start >= 0 && End >= Start) {
            val Preedit = Value.subSequence(Start, End).toString()
            if (Preedit.toByteArray(Charsets.UTF_8).size > 4096) {
                if (Composing && !Secret) Emit(JSONObject().put("Kind", "Composition").put("InputRevision", Observed).put("Phase", 2).put("Text", ""))
                Applying = true
                try {
                    Restore(); InvalidateConnection()
                    if (isFocused) (context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).restartInput(this)
                } finally { Applying = false }
                return
            }
            if (!Secret) Emit(JSONObject().put("Kind", "Composition").put("InputRevision", Observed)
                .put("Phase", if (Composing) 1 else 0).put("Text", Preedit))
            Composing = true; return
        }
        if (Composing) {
            if (!Secret) Emit(JSONObject().put("Kind", "Composition").put("InputRevision", Observed).put("Phase", 2).put("Text", ""))
            Composing = false
        }
        val StringValue = Value.toString()
        if (StringValue.toByteArray(Charsets.UTF_8).size > 65534) return
        var Left = minOf(selectionStart, selectionEnd).coerceIn(0, StringValue.length)
        var Right = maxOf(selectionStart, selectionEnd).coerceIn(Left, StringValue.length)
        val Caret = Left == Right
        fun Split(Index: Int) = Index > 0 && Index < StringValue.length && Character.isHighSurrogate(StringValue[Index - 1]) && Character.isLowSurrogate(StringValue[Index])
        if (Split(Left)) --Left
        if (Caret) Right = Left else if (Split(Right)) ++Right
        val Record = JSONObject().put("Kind", "Text").put("InputRevision", Observed).put("Text", StringValue)
            .put("Start", if (Secret) 0 else StringValue.codePointCount(0, Left))
            .put("Length", if (Secret) 0 else StringValue.codePointCount(Left, Right))
        if (Record.toString() != Last) { Last = Record.toString(); Emit(Record) }
    }
    override fun onCreateInputConnection(Info: EditorInfo): InputConnection? {
        val Connection = super.onCreateInputConnection(Info) ?: return null
        val Epoch = ConnectionEpoch
        return object : InputConnectionWrapper(Connection, false) {
            private fun Live() = !Retired && Epoch == ConnectionEpoch
            private fun Change(Work: () -> Boolean): Boolean = Live() && Work().also { Queue() }
            override fun beginBatchEdit(): Boolean {
                if (!Live() || Batch >= 64) return false
                val Accepted = super.beginBatchEdit(); if (Accepted) Batch++
                return Accepted
            }
            override fun endBatchEdit(): Boolean {
                if (!Live() || Batch == 0) return false
                val Result = super.endBatchEdit(); Batch--; Queue(); return Result
            }
            override fun finishComposingText(): Boolean = Change { super.finishComposingText() }
            override fun commitText(Text: CharSequence?, Position: Int): Boolean = Change { super.commitText(Text, Position) }
            override fun setComposingText(Text: CharSequence?, Position: Int): Boolean = Change { super.setComposingText(Text, Position) }
            override fun setComposingRegion(Start: Int, End: Int): Boolean = Change { super.setComposingRegion(Start, End) }
            override fun setSelection(Start: Int, End: Int): Boolean = Change { super.setSelection(Start, End) }
            override fun deleteSurroundingText(Before: Int, After: Int): Boolean = Change { super.deleteSurroundingText(Before, After) }
            override fun deleteSurroundingTextInCodePoints(Before: Int, After: Int): Boolean = Change { super.deleteSurroundingTextInCodePoints(Before, After) }
            override fun sendKeyEvent(Event: android.view.KeyEvent): Boolean = Change { super.sendKeyEvent(Event) }
            override fun performContextMenuAction(Id: Int): Boolean = Change {
                if (Secret && Id in listOf(android.R.id.copy, android.R.id.cut)) false else super.performContextMenuAction(Id)
            }
            @android.annotation.TargetApi(33)
            override fun commitText(Text: CharSequence, Position: Int, Attribute: android.view.inputmethod.TextAttribute?): Boolean = Change { super.commitText(Text, Position, Attribute) }
            @android.annotation.TargetApi(33)
            override fun setComposingText(Text: CharSequence, Position: Int, Attribute: android.view.inputmethod.TextAttribute?): Boolean = Change { super.setComposingText(Text, Position, Attribute) }
            @android.annotation.TargetApi(33)
            override fun setComposingRegion(Start: Int, End: Int, Attribute: android.view.inputmethod.TextAttribute?): Boolean = Change { super.setComposingRegion(Start, End, Attribute) }
            @android.annotation.TargetApi(34)
            override fun replaceText(Start: Int, End: Int, Text: CharSequence, Position: Int, Attribute: android.view.inputmethod.TextAttribute?): Boolean = Change { super.replaceText(Start, End, Text, Position, Attribute) }
        }
    }
    override fun onTextContextMenuItem(Id: Int): Boolean {
        if (Retired) return false
        if (Secret && Id in listOf(android.R.id.copy, android.R.id.cut)) return false
        if (Id == android.R.id.paste || Id == android.R.id.pasteAsPlainText) {
            // Never coerce a clipboard URI/HTML provider on the UI thread.
            val Clip = try { (context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager).primaryClip }
                catch (_: SecurityException) { null } ?: return false
            if (Clip.itemCount != 1) return false
            val Item = Clip.getItemAt(0)
            if (Item.uri != null || Item.intent != null) return false
            val Source = Item.text ?: return false
            if (Source.length > 131068) return false
            val Plain = Source.toString()
            if (Plain.toByteArray(Charsets.UTF_8).size > 65534 || Plain.codePoints().anyMatch { it == 0 || it in 0xD800..0xDFFF }) return false
            val Current = text ?: return false
            val Left = minOf(selectionStart, selectionEnd).coerceIn(0, Current.length)
            val Right = maxOf(selectionStart, selectionEnd).coerceIn(Left, Current.length)
            val Candidate = Current.subSequence(0, Left).toString() + Plain + Current.subSequence(Right, Current.length)
            if (Candidate.codePointCount(0, Candidate.length) > Maximum || Candidate.toByteArray(Charsets.UTF_8).size > 65534) return false
            Current.replace(Left, Right, Plain)
            return true
        }
        return super.onTextContextMenuItem(Id)
    }
    fun Apply(State: JSONObject) {
        Applying = true
        try {
            val Revision = State.getString("InputRevision")
            Maximum = State.getInt("MaxLength")
            Secret = State.getBoolean("Secret")
            val Multiline = State.getBoolean("Multiline")
            check(!Secret || !Multiline)
            val Type = InputType.TYPE_CLASS_TEXT or (if (Secret) InputType.TYPE_TEXT_VARIATION_PASSWORD else InputType.TYPE_TEXT_VARIATION_NORMAL) or
                (if (Multiline) InputType.TYPE_TEXT_FLAG_MULTI_LINE else 0)
            if (inputType != Type) { setSingleLine(!Multiline); inputType = Type }
            val Transformation = if (Secret) android.text.method.PasswordTransformationMethod.getInstance()
                else if (!Multiline) android.text.method.SingleLineTransformationMethod.getInstance() else null
            if (transformationMethod !== Transformation) transformationMethod = Transformation
            hint = State.getString("Placeholder")
            val Value = State.getString("Text"); Canonical = Value
            CanonicalLeft = if (Secret) Value.length else Value.offsetByCodePoints(0, State.getInt("Start"))
            CanonicalRight = if (Secret) Value.length else Value.offsetByCodePoints(CanonicalLeft, State.getInt("Length"))
            val NextGeometry = State.getString("Observation")
            val Changed = Revision != Observed || Geometry != NextGeometry
            val Preserve = !Changed && (Dirty || PendingEdit > State.getInt("EditId"))
            if (Changed) {
                Main.removeCallbacks(Flush)
                BaseInputConnection.removeComposingSpans(text)
                Composing = false; Dirty = false; PendingEdit = State.getInt("EditId")
                InvalidateConnection()
            }
            if (!Preserve && !Composing && text.toString() != Value) { setText(Value); if (Secret) setSelection(length()) }
            if (!Preserve && !Composing && !Secret) {
                setSelection(CanonicalLeft, CanonicalRight)
            }
            if (Changed) Last = ""
            Observed = Revision; Geometry = NextGeometry
            if (Changed && isFocused) (context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).restartInput(this)
        } finally { Applying = false }
    }
    private fun InvalidateConnection() {
        ConnectionEpoch++
        val Previous = Applying; Applying = true
        try { while (Batch > 0) { endBatchEdit(); Batch-- } } finally { Applying = Previous }
    }
    private fun Restore() {
        BaseInputConnection.removeComposingSpans(text); Composing = false; setText(Canonical)
        setSelection(CanonicalLeft, CanonicalRight)
    }
    fun Suspend() {
        Main.removeCallbacks(Flush); Dirty = false; InvalidateConnection(); LocalEdit = 0; PendingEdit = 0
        Composing = false; Last = ""; Observed = "0"; Geometry = ""
        Applying = true
        try { BaseInputConnection.removeComposingSpans(text); clearFocus() } finally { Applying = false }
    }
    fun Retire() { Retired = true; Suspend(); onFocusChangeListener = null }
}
