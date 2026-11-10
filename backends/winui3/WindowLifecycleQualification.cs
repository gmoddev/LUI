using System.Text.Json;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessageW(IntPtr Window, uint Message, UIntPtr WParam, IntPtr LParam);

    private static void RequestNativeClose(Microsoft.UI.Xaml.Window Window)
    {
        // Exercise the native system close route, independently of any XAML
        // programmatic Close-to-Destroy implementation detail.
        if (!PostMessageW(WinRT.Interop.WindowNative.GetWindowHandle(Window), 0x0112,
            new UIntPtr(0xF060u), IntPtr.Zero)) throw new InvalidOperationException("NativeCloseAdmission");
    }
    public async Task<bool> RunWindowLifecycleQualificationAsync(string ReportPath)
    {
        var Passed = new List<string>();
        var OwnerThread = Environment.CurrentManagedThreadId;
        int AllClosed = 0;
        Action ObserveClosed = () => ++AllClosed;
        AllWindowsClosed += ObserveClosed;
        View LiveWindow(string Title) => Views.Values.Single(View =>
            View.IsAlive && !View.NativeClosed && View.Window?.Title == Title);
        void Mark(string Group)
        {
            Passed.Add(Group);
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
            File.WriteAllText(ReportPath, JsonSerializer.Serialize(new
                { Passed = false, Status = "Running", Groups = Passed, OwnerThread, AllClosed, Views = Views.Count },
                new JsonSerializerOptions { WriteIndented = true }));
        }
        async Task Settle()
        {
            for (int Index = 0; Index < 8; ++Index) { await Task.Delay(20); Pump(); }
            FrameRequire(Environment.CurrentManagedThreadId == OwnerThread, "WindowOwnerThread");
        }
        try
        {
            FrameRun("""
                assert(app:GetService('PlatformService'):Supports('WindowLifecycle'))
                Main=Instance.new('Window',{Title='LUI lifecycle main',Visible=true,Size=UDim2.fromOffset(420,260)})
                Floating=Instance.new('Window',{Title='LUI lifecycle floating',Visible=true,Size=UDim2.fromOffset(360,220)})
                Editor=Instance.new('TextBox',{Parent=Floating,Text='retained text',Size=UDim2.fromOffset(260,80),Multiline=true})
                Attempts,Closed=0,0
                Block=true
                CloseHandler=Floating.CloseRequested:Connect(function()
                    Attempts+=1
                    assert(not Floating:RequestClose())
                    if Block then assert(Floating:PreventClose()) else Editor.Parent=Main end
                end)
                Floating.Closed:Connect(function() assert(Floating.IsClosed and not Floating.Visible); Closed+=1 end)
                """);
            await Settle();
            var FloatingView = LiveWindow("LUI lifecycle floating");
            var MainView = LiveWindow("LUI lifecycle main");
            var EditorView = Views.Values.Single(View => View.ClassName == "TextBox");
            var EditorControl = (TextBox)(EditorView.Content ?? EditorView.Element ??
                throw new InvalidOperationException("LifecycleEditorMissing"));
            EditorControl.Focus(FocusState.Programmatic);
            EditorControl.Select(2, 3);
            await Settle();
            RequestNativeClose(FloatingView.Window!);
            await Settle();
            FrameRun("assert(Attempts==1 and Closed==0 and not Floating.IsClosed and Editor.Text=='retained text')");
            FrameRequire(!FloatingView.NativeClosed && ReferenceEquals(EditorControl, EditorView.Content ?? EditorView.Element) &&
                EditorControl.SelectionStart == 2 && EditorControl.SelectionLength == 3 && AllClosed == 0,
                "PreventedNativeCloseRetainsEditor");
            Mark("Native AppWindow close request prevented; text/control/selection retained on owner");

            FrameRun("Block=false");
            RequestNativeClose(FloatingView.Window!);
            await Settle();
            FrameRun("assert(Attempts==2 and Closed==1 and Floating.IsClosed and Editor.Parent==Main and Editor.Text=='retained text')");
            FrameRequire(FloatingView.NativeClosed && FloatingView.RetireCloseHook is null && ReferenceEquals(EditorControl, EditorView.Content ?? EditorView.Element) &&
                EditorView.ParentId == Views.Single(Pair => ReferenceEquals(Pair.Value, MainView)).Key && AllClosed == 0,
                "ApprovedNativeCloseRedocksRetainedEditor");
            Mark("Native closure acknowledges once after canonical redocking; same editor survives");
            FrameRun("assert(not Floating:RequestClose()); assert(not pcall(function() Floating.Visible=true end)); Floating:Destroy(); assert(Editor.Text=='retained text')");
            await Settle();
            Mark("Terminal window rejects reopen/repeat; explicit old-window destruction preserves moved child");

            FrameRun("""
                ErrorWindow=Instance.new('Window',{Title='LUI lifecycle error',Visible=true})
                ErrorHandler=ErrorWindow.CloseRequested:Connect(function() error('synthetic close failure') end)
                """);
            await Settle();
            var ErrorView = LiveWindow("LUI lifecycle error");
            RequestNativeClose(ErrorView.Window!); await Settle();
            FrameRun("assert(not ErrorWindow.IsClosed); ErrorHandler:Disconnect()");
            FrameRequire(!ErrorView.NativeClosed, "CallbackFailureDidNotPreventClose");
            RequestNativeClose(ErrorView.Window!); await Settle();
            FrameRun("assert(ErrorWindow.IsClosed); ErrorWindow:Destroy()");
            Mark("Callback error prevents native close; disconnected handler permits explicit retry");

            FrameRun("""
                LateWindow=Instance.new('Window',{Title='LUI lifecycle late',Visible=true})
                LateEditor=Instance.new('TextBox',{Parent=LateWindow,Text='post-close retained',Size=UDim2.fromOffset(220,60)})
                """);
            await Settle();
            var LateWindowView = LiveWindow("LUI lifecycle late");
            var LateEditorEntry = Views.Single(Pair => Pair.Value.ClassName == "TextBox" &&
                Pair.Value.ParentId == Views.Single(Window => ReferenceEquals(Window.Value, LateWindowView)).Key);
            var LateEditorControl = (TextBox)LateEditorEntry.Value.Element!;
            RequestNativeClose(LateWindowView.Window!); await Settle();
            FrameRun("assert(LateWindow.IsClosed and LateEditor.Parent==LateWindow and LateEditor.Text=='post-close retained');LateEditor.Parent=Main;LateWindow:Destroy()");
            await Settle();
            FrameRequire(ReferenceEquals(LateEditorControl, LateEditorEntry.Value.Element) &&
                ReferenceEquals(LateEditorControl.XamlRoot, MainView.Container!.XamlRoot), "PostCloseRedockChangedEditor");
            MainView.Window!.Activate();
            FrameRun("assert(LateEditor:Focus())"); await Settle();
            FrameRun("assert(LateEditor.IsFocused and LateEditor.Text=='post-close retained')");
            FrameRequire(FocusedViewId == LateEditorEntry.Key, "PostCloseFocusNotRestored");
            Mark("Post-close redocking retains the same native editor and restores canonical focus");
            int StaleFocus = -1;
            FrameRequire(MainView.Window.DispatcherQueue.TryEnqueue(() =>
                StaleFocus = OnRequestFocus(IntPtr.Zero, LateEditorEntry.Key)), "PendingFocusNotQueued");
            FrameRun("LateEditor:Destroy()"); await Settle();
            FrameRequire(StaleFocus == 0 && !Views.ContainsKey(LateEditorEntry.Key), "PendingFocusReachedDestroyedEditor");
            Mark("Queued native focus completion rejects retired editor identity");

            FrameRun("Main.Closed:Connect(function() Replacement=Instance.new('Window',{Title='LUI lifecycle replacement',Visible=true}) end)");
            RequestNativeClose(MainView.Window!); await Settle();
            FrameRun("assert(Main.IsClosed and Replacement and not Replacement.IsClosed)");
            FrameRequire(AllClosed==0,"StaleLastWindowExitIgnoredReplacement");
            FrameRequire(ReferenceEquals(GetDialogWindow(), LiveWindow("LUI lifecycle replacement").Window),
                "RetainedClosedWindowSelectedForNativeDialog");
            Mark("Closed callback creates a new native window; stale last-window exit is fenced");
            RequestNativeClose(LiveWindow("LUI lifecycle replacement").Window!);
            await Settle();
            FrameRun("assert(Replacement.IsClosed); Replacement:Destroy()");
            FrameRun("assert(Main.IsClosed); Editor:Destroy(); Main:Destroy()");
            FrameRequire(AllClosed == 1 && Views.Count == 0, "LastWindowClosureOrRetainedViews");
            Mark("One last-window notification after canonical acknowledgement; explicit cleanup releases views");
            FrameRun("""
                DestroyedOwner=Instance.new('Window',{Title='LUI lifecycle destroy',Visible=true})
                DestroyedOwner.CloseRequested:Connect(function() DestroyedInRequest=true;DestroyedOwner:Destroy() end)
                """);
            await Settle();
            RequestNativeClose(LiveWindow("LUI lifecycle destroy").Window!);
            await Settle();
            FrameRun("assert(DestroyedInRequest and #app:GetService('WindowService'):GetWindows()==0)");
            FrameRequire(AllClosed == 2 && Views.Count == 0, "DestroyFromCloseRequestStrandedHost");
            Mark("Explicit Destroy inside close request retires the new last-window generation without stranding the host");
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
            await File.WriteAllTextAsync(ReportPath, JsonSerializer.Serialize(new { Passed = true, Groups = Passed, OwnerThread, AllClosed, Views = Views.Count }, new JsonSerializerOptions { WriteIndented = true }));
            return true;
        }
        catch (Exception Error)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
            await File.WriteAllTextAsync(ReportPath, JsonSerializer.Serialize(new { Passed = false, Groups = Passed, Error = Error.ToString(), AllClosed, Views = Views.Count }, new JsonSerializerOptions { WriteIndented = true }));
            LuiDiagnostics.Error("Window", "[LUI:Window] Native qualification failed: " + Error.GetType().Name);
            return false;
        }
        finally { AllWindowsClosed -= ObserveClosed; }
    }
}
