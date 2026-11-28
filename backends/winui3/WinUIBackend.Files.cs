using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Windows.Storage.Pickers;

namespace Lui.WinUI;
internal sealed partial class WinUIBackend
{
    private readonly Native.StartFilePickerCallback StartFilePickerCallback;
    private readonly Native.CancelFilePickerCallback CancelFilePickerCallback;
    private readonly Dictionary<ulong, PendingFilePicker> FilePickers = new();
    private bool LegacyFilePickerPending;
    private sealed class PendingFilePicker
    {
        private readonly IntPtr Parent;
        private readonly Microsoft.UI.Xaml.Window OwnerWindow;
        private readonly Microsoft.UI.Dispatching.DispatcherQueueTimer CloseTimer;
        private Action? CancelOperation;
        private bool Canceled;
        private bool Completed;
        private bool OwnerClosed;
        private int CloseAttempts;
        internal PendingFilePicker(Microsoft.UI.Xaml.Window Window)
        {
            OwnerWindow = Window;
            Parent = WinRT.Interop.WindowNative.GetWindowHandle(Window);
            CloseTimer = Window.DispatcherQueue.CreateTimer();
            CloseTimer.Interval = TimeSpan.FromMilliseconds(50);
            CloseTimer.IsRepeating = true;
            CloseTimer.Tick += (_, _) => {
                if (Completed || OwnerClosed) { CloseTimer.Stop(); return; }
                Dismiss();
                if (++CloseAttempts >= 2400) CloseTimer.Stop();
            };
            Window.Closed += ParentClosed;
        }
        internal void Bind(Action Operation)
        {
            if (Completed) return;
            CancelOperation = Operation;
            if (Canceled) Cancel();
        }
        internal void Cancel()
        {
            if (Completed) return;
            Canceled = true;
            try { CancelOperation?.Invoke(); } catch { }
            if (Completed || OwnerClosed) return;
            // IAsyncInfo.Cancel requests cancellation but the Windows App SDK
            // picker can remain inside IFileDialog.Show. Close only our own
            // common dialog, including its creation race with cancellation.
            Dismiss(); CloseTimer.Start();
        }
        private void ParentClosed(object Sender, Microsoft.UI.Xaml.WindowEventArgs Arguments)
        {
            OwnerClosed = true; Cancel(); CloseTimer.Stop();
        }
        internal void Complete()
        {
            Completed = true; CloseTimer.Stop(); CancelOperation = null;
            OwnerWindow.Closed -= ParentClosed;
        }
        private void Dismiss()
        {
            if (Completed || OwnerClosed) return;
            PickerEnumWindows((Window, _) => {
                PickerGetWindowThreadProcessId(Window, out uint Process);
                if (Process != (uint)Environment.ProcessId || PickerGetWindow(Window, 4) != Parent) return true;
                StringBuilder ClassName = new(64);
                if (PickerGetClassName(Window, ClassName, ClassName.Capacity) > 0 && ClassName.ToString() == "#32770")
                    PickerPostMessage(Window, 0x0010, IntPtr.Zero, IntPtr.Zero);
                return true;
            }, IntPtr.Zero);
        }
    }
    private delegate bool PickerWindowCallback(IntPtr Window, IntPtr Context);
    [DllImport("user32.dll", EntryPoint = "EnumWindows")]
    private static extern bool PickerEnumWindows(PickerWindowCallback Callback, IntPtr Context);
    [DllImport("user32.dll", EntryPoint = "GetWindowThreadProcessId")]
    private static extern uint PickerGetWindowThreadProcessId(IntPtr Window, out uint Process);
    [DllImport("user32.dll", EntryPoint = "GetWindow")]
    private static extern IntPtr PickerGetWindow(IntPtr Window, uint Command);
    [DllImport("user32.dll", EntryPoint = "GetClassNameW", CharSet = CharSet.Unicode)]
    private static extern int PickerGetClassName(IntPtr Window, StringBuilder ClassName, int Capacity);
    [DllImport("user32.dll", EntryPoint = "PostMessageW")]
    private static extern bool PickerPostMessage(IntPtr Window, uint Message, IntPtr WParam, IntPtr LParam);
    private void StartFiles()
    {
        Native.FilePickerCallbacks Callbacks = new() {
            StructSize = (uint)Marshal.SizeOf<Native.FilePickerCallbacks>(), AbiVersion = 1,
            StartPicker = StartFilePickerCallback, CancelPicker = CancelFilePickerCallback,
        };
        uint Status = Native.Lui_InstallFilesV1(Runtime, ref Callbacks);
        if (Status != 0 && Status != 2) throw new InvalidOperationException("[LUI:Files] InstallationFailed");
    }
    private uint OnStartFilePicker(IntPtr Context, ulong Request, uint Kind, string Name)
    {
        if (IsDisposed || IsDisposing || FilePickers.Count != 0 || LegacyFilePickerPending) return 5;
        var Window = Views.Values.FirstOrDefault(View => View.IsAlive && View.Window is not null && View.Visible)?.Window;
        if (Window is null || Kind < 1 || Kind > 3) return 3;
        // Reserve before starting the async operation; synchronous refusal also
        // removes this reservation in finally, without interrupting the owner.
        PendingFilePicker Pending = new(Window);
        FilePickers.Add(Request, Pending);
        _ = PickFileAsync(Window, Pending, Request, Kind, Name);
        return 0;
    }
    private async Task PickFileAsync(Microsoft.UI.Xaml.Window Window, PendingFilePicker Pending, ulong Request, uint Kind, string Name)
    {
        string? Path = null; uint Status = 0;
        try {
            if (Kind == 1) {
                FileOpenPicker Picker = new(Window.AppWindow.Id);
                var Operation = Picker.PickSingleFileAsync(); Pending.Bind(() => Operation.Cancel());
                Path = (await Operation)?.Path;
            } else if (Kind == 2) {
                FileSavePicker Picker = new(Window.AppWindow.Id) { SuggestedFileName = Name };
                var Operation = Picker.PickSaveFileAsync(); Pending.Bind(() => Operation.Cancel());
                Path = (await Operation)?.Path;
            } else {
                FolderPicker Picker = new(Window.AppWindow.Id);
                var Operation = Picker.PickSingleFolderAsync(); Pending.Bind(() => Operation.Cancel());
                Path = (await Operation)?.Path;
            }
        } catch (OperationCanceledException) { Status = 6; }
        catch { Status = 9; }
        finally { Pending.Complete(); FilePickers.Remove(Request); }
        if (!IsDisposed && !IsDisposing)
            Native.Lui_CompleteFilePickerV1(Runtime, Request, Path, Status);
    }
    private void OnCancelFilePicker(IntPtr Context, ulong Request)
    {
        if (FilePickers.TryGetValue(Request, out var Pending)) Pending.Cancel();
    }
    private void StopFilePickers()
    {
        foreach (var Pending in FilePickers.Values.ToArray()) Pending.Cancel();
        FilePickers.Clear();
    }
    private static partial class Native
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate uint StartFilePickerCallback(IntPtr Context, ulong Request, uint Kind,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string Name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void CancelFilePickerCallback(IntPtr Context, ulong Request);
        [StructLayout(LayoutKind.Sequential)]
        internal struct FilePickerCallbacks {
            internal uint StructSize, AbiVersion;
            internal IntPtr Context;
            internal StartFilePickerCallback StartPicker;
            internal CancelFilePickerCallback CancelPicker;
        }
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint Lui_InstallFilesV1(IntPtr Runtime, ref FilePickerCallbacks Callbacks);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint Lui_CompleteFilePickerV1(IntPtr Runtime, ulong Request,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? Path, uint Status);
    }
}
