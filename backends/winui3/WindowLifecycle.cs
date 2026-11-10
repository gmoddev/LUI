using System.Runtime.InteropServices;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    private Native.CloseWindowCallback? CloseWindowCallback;
    private bool AllWindowsClosedPublished;
    private ulong WindowLifecycleGeneration;

    private void StartWindowLifecycle()
    {
        CloseWindowCallback = OnCloseWindow;
        Native.WindowCallbacks Callbacks = new()
        {
            StructSize = (uint)Marshal.SizeOf<Native.WindowCallbacks>(), AbiVersion = 1,
            Close = CloseWindowCallback,
        };
        if (Native.Lui_SetWindowCallbacks(Runtime, ref Callbacks) != 1)
            throw new InvalidOperationException("[LUI:Window] Lifecycle registration failed");
    }

    private void ConfigureWindowLifecycle(int Id, View Target)
    {
        ++WindowLifecycleGeneration;
        AllWindowsClosedPublished = false;
        var NativeWindow = Target.Window!.AppWindow;
        Windows.Foundation.TypedEventHandler<Microsoft.UI.Windowing.AppWindow,
            Microsoft.UI.Windowing.AppWindowClosingEventArgs> Handler = (_, Args) =>
        {
            if (IsDisposed || IsDisposing || !Target.IsAlive || Target.CloseApproved) return;
            // OS intent must not destroy a dirty owner while Luau decides.
            Args.Cancel = true;
            if (Target.CloseQueued || Target.NativeClosed) return;
            Target.CloseQueued = true;
            if (!Target.Window!.DispatcherQueue.TryEnqueue(() =>
            {
                Target.CloseQueued = false;
                if (IsDisposed || IsDisposing || !Target.IsAlive || Target.NativeClosed ||
                    !Views.TryGetValue(Id, out View? Current) || !ReferenceEquals(Current, Target)) return;
                Native.Lui_WindowCloseRequested(Runtime, Id);
            }))
            {
                Target.CloseQueued = false;
                LuiDiagnostics.Error("Window", "[LUI:Window] Close owner dispatch unavailable");
            }
        };
        NativeWindow.Closing += Handler;
        Target.RetireCloseHook = () => NativeWindow.Closing -= Handler;
    }

    private void RetireWindowCloseHook(View Target)
    {
        var Retire = Target.RetireCloseHook;
        Target.RetireCloseHook = null;
        try { Retire?.Invoke(); }
        catch (Exception Error)
        {
            LuiDiagnostics.Error("Window", "[LUI:Window] Close hook retirement: " + Error.GetType().Name);
        }
    }

    private int OnCloseWindow(IntPtr Context, int Id)
    {
        if (IsDisposed || IsDisposing || !Views.TryGetValue(Id, out View? Target) ||
            !Target.IsAlive || Target.NativeClosed || Target.Window is null) return 0;
        try
        {
            Target.CloseApproved = true;
            DetachCodeEditors(Id);
            Target.Window.Close();
            return 1;
        }
        catch (Exception Error)
        {
            LuiDiagnostics.Error("Window", "[LUI:Window] Native close rejected: " + Error.GetType().Name);
            return 0;
        }
        finally { Target.CloseApproved = false; }
    }

    private void OnNativeWindowClosed(int Id, View Target)
    {
        if (Target.NativeClosed) return;
        RetireWindowCloseHook(Target);
        Target.NativeClosed = true;
        Target.Visible = false;
        Native.Lui_WindowClosed(Runtime, Id);
        CancelWindowInput(Id);
        QueueLastWindowExit(Target);
    }

    private void QueueLastWindowExit(View Target)
    {
        if (IsDisposed || IsDisposing || Target.Window is null) return;
        // Native acknowledgement may be queued behind the active backend call.
        // Let that owner turn settle before the application's last-window exit.
        if (AllWindowsClosedPublished || Views.Values.Any(View => View.Window is not null && View.IsAlive && !View.NativeClosed)) return;
        AllWindowsClosedPublished = true;
        var Generation = WindowLifecycleGeneration;
        Target.Window.DispatcherQueue.TryEnqueue(() =>
        {
            if (!IsDisposed && !IsDisposing && Generation == WindowLifecycleGeneration && Views.Values.Where(View => View.Window is not null)
                .All(View => View.NativeClosed || !View.IsAlive)) AllWindowsClosed?.Invoke();
        });
    }

    private static partial class Native
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int CloseWindowCallback(IntPtr Context, int Id);
        [StructLayout(LayoutKind.Sequential)]
        internal struct WindowCallbacks
        {
            internal uint StructSize, AbiVersion;
            internal IntPtr Context;
            internal CloseWindowCallback Close;
            internal ulong Reserved;
        }
        [DllImport("LuiRuntime", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_SetWindowCallbacks(IntPtr Runtime, ref WindowCallbacks Callbacks);
        [DllImport("LuiRuntime", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_WindowCloseRequested(IntPtr Runtime, int Id);
        [DllImport("LuiRuntime", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_WindowClosed(IntPtr Runtime, int Id);
    }
}
