package dev.lui.feasibility

internal object NativeBridge {
    init { System.loadLibrary("LuiMobileJni") }
    external fun Start(Bundle: ByteArray, StorageRoot: ByteArray): Int
    external fun Attach(Token: Int, Epoch: Int, Width: Double, Height: Double): Boolean
    external fun Detach(Token: Int, Epoch: Int): Boolean
    external fun Take(Token: Int): ByteArray?
    external fun Acknowledge(Token: Int, Epoch: Int, Revision: Int, Success: Boolean): Boolean
    external fun Activate(Token: Int, Epoch: Int, Revision: Int, Sequence: Int, Id: Int): Boolean
    external fun Event(Token: Int, Epoch: Int, Revision: Int, Sequence: Int, Id: Int, Input: ByteArray): Int
    external fun Pump(Token: Int): Boolean
    external fun Theme(Token: Int, Mode: Int): Boolean
    external fun Asset(Token: Int, Path: ByteArray): ByteArray?
    external fun Close(Token: Int): Boolean
    external fun Metrics(Token: Int): ByteArray?
}
