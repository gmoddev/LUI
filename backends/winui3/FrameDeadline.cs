using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    private static readonly bool EnableFrameDeadline = true;
    private static readonly bool EnableFrameSettlement = true;

    private bool TrySettleFrameUploads()
    {
        if (WindowResizePending || Pumping) return false;
        Pumping = true;
        try
        {
            return Native.Lui_SettleFrameUploadsV1(Runtime) == 1;
        }
        finally { Pumping = false; }
    }

    private sealed class FrameDeadline : WaitHandle
    {
        private const uint HighResolution = 2;
        private const uint ModifyAndSynchronize = 0x100002;
        private readonly Action Notify;
        private readonly RegisteredWaitHandle Registration;
        private readonly object ArmLock = new();
        private long ArmedFor;
        private long FiredAt;

        internal FrameDeadline(Action Notify)
        {
            this.Notify = Notify;
            SafeWaitHandle = CreateWaitableTimerEx(IntPtr.Zero, null, HighResolution, ModifyAndSynchronize);
            try
            {
                if (SafeWaitHandle.IsInvalid) throw new System.ComponentModel.Win32Exception();
                Registration = ThreadPool.UnsafeRegisterWaitForSingleObject(this,
                    static (Context, _) => ((FrameDeadline)Context!).OnTimer(), this, Timeout.Infinite, false);
            }
            catch
            {
                SafeWaitHandle.Dispose();
                throw;
            }
        }

        internal bool Update(int Microseconds)
        {
            lock (ArmLock)
            {
                if (Microseconds < 0)
                {
                    if (ArmedFor != 0) _ = CancelWaitableTimer(SafeWaitHandle);
                    ArmedFor = 0;
                    return true;
                }
                long Now = Stopwatch.GetTimestamp();
                // Bound expedited turns even when timed work is recurringly due.
                // Other owner wake sources remain independent.
                long SinceFired = FiredAt == 0 ? 4000 : (long)Stopwatch.GetElapsedTime(FiredAt, Now).TotalMicroseconds;
                long Delay = Math.Max(Math.Max(Microseconds, 500), 4000 - SinceFired);
                long Due = Now + Delay * Stopwatch.Frequency / 1_000_000;
                if (ArmedFor > Now && Math.Abs(ArmedFor - Due) < Stopwatch.Frequency / 5000) return true;
                ArmedFor = Due;
                long Relative = -Delay * 10;
                return SetWaitableTimerEx(SafeWaitHandle, in Relative, 0, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, 0);
            }
        }

        private void OnTimer()
        {
            lock (ArmLock)
            {
                long Now = Stopwatch.GetTimestamp();
                if (ArmedFor == 0 || ArmedFor > Now) return;
                ArmedFor = 0;
                FiredAt = Now;
            }
            Notify();
        }

        protected override void Dispose(bool Explicit)
        {
            if (Explicit)
            {
                Update(-1);
                Registration?.Unregister(null);
            }
            base.Dispose(Explicit);
        }

        [DllImport("kernel32.dll", EntryPoint = "CreateWaitableTimerExW", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern SafeWaitHandle CreateWaitableTimerEx(IntPtr Attributes, string? Name, uint Flags, uint Access);
        [DllImport("kernel32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetWaitableTimerEx(SafeWaitHandle Timer, in long Due, int Period,
            IntPtr Completion, IntPtr Context, IntPtr WakeContext, uint Slack);
        [DllImport("kernel32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CancelWaitableTimer(SafeWaitHandle Timer);
    }

    private static partial class Native
    {
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_GetFrameTaskDelayV1(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_SettleFrameUploadsV1(IntPtr Runtime);
    }
}
