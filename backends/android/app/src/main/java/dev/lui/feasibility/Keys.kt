package dev.lui.feasibility

import android.view.KeyEvent

internal object Keys {
    fun Name(Code: Int): String? = when (Code) {
        in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> ('A'.code + Code - KeyEvent.KEYCODE_A).toChar().toString()
        in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> ('0'.code + Code - KeyEvent.KEYCODE_0).toChar().toString()
        in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 -> "F${Code - KeyEvent.KEYCODE_F1 + 1}"
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> "Enter"
        KeyEvent.KEYCODE_ESCAPE -> "Escape"
        KeyEvent.KEYCODE_TAB -> "Tab"
        KeyEvent.KEYCODE_SPACE -> "Space"
        KeyEvent.KEYCODE_DEL -> "Backspace"
        KeyEvent.KEYCODE_FORWARD_DEL -> "Delete"
        KeyEvent.KEYCODE_INSERT -> "Insert"
        KeyEvent.KEYCODE_MOVE_HOME -> "Home"
        KeyEvent.KEYCODE_MOVE_END -> "End"
        KeyEvent.KEYCODE_PAGE_UP -> "PageUp"
        KeyEvent.KEYCODE_PAGE_DOWN -> "PageDown"
        KeyEvent.KEYCODE_DPAD_LEFT -> "ArrowLeft"
        KeyEvent.KEYCODE_DPAD_RIGHT -> "ArrowRight"
        KeyEvent.KEYCODE_DPAD_UP -> "ArrowUp"
        KeyEvent.KEYCODE_DPAD_DOWN -> "ArrowDown"
        KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> "Shift"
        KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> "Control"
        KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> "Alt"
        KeyEvent.KEYCODE_CAPS_LOCK -> "CapsLock"
        else -> null
    }
}
