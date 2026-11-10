package dev.lui.feasibility

import android.content.Intent
import android.test.InstrumentationTestCase

// Run Prepare alone, terminate its live process externally, then run Recover
// alone. The normal HostTest suite does not include this deliberate killed run.
@Suppress("DEPRECATION")
class RecoveryTest : InstrumentationTestCase() {
    private fun Main(Work: () -> Unit) {
        var Failure: Throwable? = null
        instrumentation.runOnMainSync { try { Work() } catch (Problem: Throwable) { Failure = Problem } }
        Failure?.let { throw it }
    }
    private fun Walk(Root: android.view.View): List<android.view.View> = listOf(Root) +
        if (Root is android.view.ViewGroup) (0 until Root.childCount).flatMap { Walk(Root.getChildAt(it)) } else emptyList()
    private fun Named(Activity: MainActivity, Name: String) = Walk(Activity.window.decorView).singleOrNull { it.tag == Name }
    private fun Text(Activity: MainActivity, Name: String) = (Named(Activity, Name) as? android.widget.TextView)?.text?.toString()
    private fun Admitted(Activity: MainActivity): Boolean {
        val Owner = MainActivity::class.java.getDeclaredField("OwnerSession").apply { isAccessible = true }.get(Activity)
        return Session::class.java.getDeclaredField("Accepting").apply { isAccessible = true }.getBoolean(Owner)
    }
    private fun Await(Condition: () -> Boolean) {
        val Until = android.os.SystemClock.uptimeMillis() + 20000
        while (android.os.SystemClock.uptimeMillis() < Until) {
            var Ready = false; Main { Ready = Condition() }; if (Ready) return
            Thread.sleep(40)
        }
        fail("[LUI:MobileTest] RecoveryTimeout")
    }
    private fun Launch() = instrumentation.startActivitySync(Intent(instrumentation.targetContext,
        MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
    fun testPrepare() {
        val Root = java.io.File(instrumentation.targetContext.noBackupFilesDir, "LuiStorage")
        val Record = java.io.File(Root, "K-checkpoint.bin")
        if (Record.exists()) assertTrue(Record.delete())
        val Activity = Launch()
        Await { Text(Activity, "CheckpointState") == "Committed 0" && Admitted(Activity) }
        Main { assertTrue(Named(Activity, "CheckpointButton")!!.performClick()) }
        Await { Text(Activity, "CheckpointState") == "Committed 1" }
        Main { (Named(Activity, "Password") as android.widget.EditText).setText("Synthetic password") }
        assertEquals("LUI-CHECKPOINT\n2\n1\n", Record.readText())
        // A separately killed portable writer covers a real mid-write temporary.
        // Here a stale temporary also exercises the Android startup boundary.
        java.io.File(Root, ".lui-write").writeText("partial uncommitted data")
        java.io.File(instrumentation.targetContext.noBackupFilesDir, "RecoveryPid.txt").writeText(android.os.Process.myPid().toString())
        instrumentation.sendStatus(1, android.os.Bundle().apply { putString("Checkpoint", "ColdCheckpointReady") })
        Thread.sleep(60000)
        fail("[LUI:MobileTest] External process termination was not performed")
    }
    fun testRecover() {
        val PreviousPid = java.io.File(instrumentation.targetContext.noBackupFilesDir, "RecoveryPid.txt")
        assertTrue(PreviousPid.readText().toInt() != android.os.Process.myPid())
        val Activity = Launch()
        Await { Text(Activity, "CheckpointState") == "Committed 1" }
        Main { assertTrue(Text(Activity, "Password") == ""); assertEquals("Count 0", Text(Activity, "CounterButton")) }
        val Root = java.io.File(instrumentation.targetContext.noBackupFilesDir, "LuiStorage")
        assertFalse(java.io.File(Root, ".lui-write").exists())
        assertEquals("LUI-CHECKPOINT\n2\n1\n", java.io.File(Root, "K-checkpoint.bin").readText())
        lateinit var Owner: Session
        Main { Owner = Session.GetSession(Activity.application); Activity.finish() }
        Await { Owner.IsClosed() }
        assertTrue(java.io.File(Root, "K-checkpoint.bin").delete()); assertTrue(PreviousPid.delete())
    }
}
