using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.ApplicationModel.DataTransfer;
using Windows.UI.ViewManagement;
using Lui.Project;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend : IDisposable
{
    internal static void Diagnostic(string Message)
    {
        LuiDiagnostics.Log("WinUI", Message);
    }

    private sealed class View
    {
        public Microsoft.UI.Xaml.Window? Window;
        public UIElement? Element;
        public Canvas? Container;
        public UIElement? Content;
        public Border? Decoration;
        public int ParentId;
        public IntPtr CodeHost;
        public int CodeWindowId;
        public double CodeWidth, CodeHeight;
        public double BoundsX;
        public double BoundsY;
        public bool ApplyingProperty;
        public bool IsAlive = true;
        public bool Visible = true;
        public bool Enabled = true;
        public bool InputVisible = true;
        public bool Multiline, Secret, Composing;
        public int SelectionStart, SelectionLength, MaxLength = 65534;
        public string Text = string.Empty, Placeholder = string.Empty, StyleJson = string.Empty;
        public ulong ImageToken;
        public string ImageKey = string.Empty;
        public string ClassName = string.Empty;
        public bool PendingResize;
        public double ViewportWidth, ViewportHeight;
        public bool NativeClosed;
        public bool CloseApproved, CloseQueued;
        public Action? RetireCloseHook;
        public readonly Dictionary<uint, HashSet<int>> ActivePointerButtons = new();
        public readonly Dictionary<uint, ulong> CaptureGenerations = new();
        public ulong NextCaptureGeneration;
        public ulong FrameEpoch = 1;
        public FrameUpload? PreparedFrame, DisplayedFrame;
        public SemaphoreSlim? FramePrepareGate;
    }

    private readonly Dictionary<int, View> Views = new();
    internal bool PauseFrameQualificationPump;
    private readonly Dictionary<string, string> AssetPaths = new(StringComparer.Ordinal);
    private readonly Native.CreateCallback CreateCallback;
    private readonly Native.PropertyCallback PropertyCallback;
    private readonly Native.ParentCallback ParentCallback;
    private readonly Native.ArrangeCallback ArrangeCallback;
    private readonly Native.PresentCallback PresentCallback;
    private readonly Native.DestroyCallback DestroyCallback;
    private readonly Native.LogCallback LogCallback;
    private readonly Native.WriteClipboardCallback WriteClipboardCallback;
    private readonly Native.StartPlatformRequestCallback ReadClipboardCallback;
    private readonly Native.StartPlatformRequestCallback OpenFileCallback;
    private readonly Native.RequestFocusCallback RequestFocusCallback;
    private readonly CodeEditorHost.EditorEvent CodeEditorEventCallback;
    private readonly IntPtr Runtime;
    private string LastReportedError = string.Empty;
    private int FocusedViewId;
    private ulong FocusGeneration;
    private bool IsDisposed;
    private bool IsDisposing;
    private readonly AccessibilitySettings Accessibility = new();
    private readonly UISettings UiSettings = new();
    private string LastTheme = string.Empty;
    private long PresentationAt;

    public event Action? AllWindowsClosed;

    public string LastError => Marshal.PtrToStringUTF8(Native.Lui_GetLastError(Runtime)) ?? "unknown error";

    public WinUIBackend(PreparedProject? Project=null)
    {
        CreateCallback = OnCreate;
        PropertyCallback = OnProperty;
        ParentCallback = OnParent;
        ArrangeCallback = OnArrange;
        PresentCallback = OnPresent;
        DestroyCallback = OnDestroy;
        LogCallback = OnNativeLog;
        WriteClipboardCallback = OnWriteClipboardText;
        ReadClipboardCallback = OnReadClipboardText;
        OpenFileCallback = OnOpenFile;
        RequestFocusCallback = OnRequestFocus;
        CodeEditorEventCallback = OnCodeEditorEvent;
        Runtime = Project?.CreateRuntime() ?? Native.Lui_Create();
        if (Runtime == IntPtr.Zero) throw new InvalidOperationException("Could not initialize Luau");
        Native.Lui_SetLogCallback(Runtime, IntPtr.Zero, LogCallback);
        Native.Lui_SetBackendName(Runtime, "winui3");
        Native.Lui_SetBackend(Runtime, new Native.BackendCallbacks
        {
            Create = CreateCallback,
            Property = PropertyCallback,
            Parent = ParentCallback,
            Arrange = ArrangeCallback,
            Destroy = DestroyCallback,
        });
        Native.FocusCallbacks Focus = new()
        {
            StructSize = (uint)Marshal.SizeOf<Native.FocusCallbacks>(), AbiVersion = 1,
            RequestFocus = RequestFocusCallback,
        };
        if (Native.Lui_SetFocusCallbacks(Runtime, ref Focus) != 1)
            throw new InvalidOperationException("Focus callbacks failed: " + LastError);
        StartWindowLifecycle();
        Native.PlatformCallbacks Platform = new()
        {
            StructSize = (uint)Marshal.SizeOf<Native.PlatformCallbacks>(),
            AbiVersion = 1,
            WriteClipboardText = WriteClipboardCallback,
            ReadClipboardText = ReadClipboardCallback,
            OpenFile = OpenFileCallback,
        };
        Native.PresentationCallbacks Presentation = new() {
            StructSize = (uint)Marshal.SizeOf<Native.PresentationCallbacks>(), AbiVersion = 1, Present = PresentCallback,
        };
        if (Native.Lui_SetPresentationCallbacks(Runtime, ref Presentation) != 1)
            throw new InvalidOperationException("Presentation callbacks failed: " + LastError);
        StartImages();
        StartFrames();
        CompositionTarget.Rendering += OnRendering;
        if (Native.Lui_SetPlatformCallbacks(Runtime, ref Platform) != 1)
            throw new InvalidOperationException("Platform callbacks failed: " + LastError);
        CheckTheme();
        StartOwnerWake();
    }

    public void Dispose()
    {
        if (IsDisposed || IsDisposing) return;
        IsDisposing = true;
        StopOwnerWake();
        CompositionTarget.Rendering -= OnRendering;
        try { StopFrames(); }
        finally {
            try { StopImages(); }
            finally {
                Native.Lui_Destroy(Runtime);
                IsDisposed = true;
                IsDisposing = false;
            }
        }
    }

    public void ConfigureManifest(AppManifest Manifest)
    {
        Native.CapabilityDeclaration Declaration = new()
        {
            StructSize = (uint)Marshal.SizeOf<Native.CapabilityDeclaration>(),
            AbiVersion = 1,
            GrantedCapabilities = (Manifest.AllowNativeExtensions ? 1UL : 0UL) |
                (Manifest.AllowHostServices ? 2UL : 0UL) |
                (Manifest.AllowClipboard ? 4UL : 0UL) |
                (Manifest.AllowDialogs ? 8UL : 0UL) |
                (Manifest.AllowNetworkClient ? 16UL : 0UL) |
                (Manifest.AllowNetworkServer ? 32UL : 0UL) |
                (Manifest.AllowNetworkRaw ? 64UL : 0UL) |
                (Manifest.AllowStorage ? 128UL : 0UL) |
                (Manifest.AllowNotifications ? 256UL : 0UL) |
                (Manifest.AllowTray ? 512UL : 0UL),
        };
        if (Native.Lui_DeclareCapabilities(Runtime, ref Declaration) != 1)
            throw new InvalidOperationException("Capability declaration failed: " + LastError);
        StartDesktopServices();
        if (Manifest.NetworkPolicy is { } Policy)
        {
            Native.NetworkPolicy HostPolicy = new()
            {
                StructSize = (uint)Marshal.SizeOf<Native.NetworkPolicy>(),
                AbiVersion = 1,
                Flags = (Policy.ClientLoopbackOnly ? 1u : 0u) |
                    (Policy.ServerLoopbackOnly ? 2u : 0u),
                ClientPortMin = Policy.ClientPortMin,
                ClientPortMax = Policy.ClientPortMax,
                ServerPortMin = Policy.ServerPortMin,
                ServerPortMax = Policy.ServerPortMax,
            };
            if (Native.Lui_SetNetworkPolicy(Runtime, ref HostPolicy) != 1)
                throw new InvalidOperationException("Network policy failed: " + LastError);
        }
        if (Manifest.Sandbox is not null)
        {
            Native.SandboxLimits Limits = new()
            {
                StructSize = (uint)Marshal.SizeOf<Native.SandboxLimits>(),
                AbiVersion = 1,
                MaxMemoryBytes = Manifest.Sandbox.MaxMemoryBytes,
                MaxInterrupts = Manifest.Sandbox.MaxInterrupts,
            };
            if (Native.Lui_ConfigureSandbox(Runtime, ref Limits) != 1)
                throw new InvalidOperationException("Sandbox configuration failed: " + LastError);
        }
        foreach (string ExtensionPath in Manifest.ExtensionPaths)
        {
            if (Native.Lui_LoadExtension(Runtime, ExtensionPath) != 1)
                throw new InvalidOperationException("Extension load failed: " + LastError);
        }
        foreach (KeyValuePair<string, string> Asset in Manifest.AssetPaths)
        {
            if (Native.Lui_RegisterAsset(Runtime, Asset.Key) != 1)
                throw new InvalidOperationException("Asset registration failed: " + LastError);
            AssetPaths.Add(Asset.Key, Asset.Value);
            CaptureAsset(Asset.Key, Asset.Value);
        }
    }
    public bool RunProject(PreparedProject Project,bool AllowNative=false)
    {
        Project.Configure(Runtime,true,AllowNative,StartDesktopServices);
        foreach(var Asset in Project.Manifest.GetProperty("Assets").EnumerateArray()) {
            string Name=Asset.GetString()!;
            AssetPaths[Name]=Path.Combine(Path.GetDirectoryName(Project.ManifestPath)!,Name);
            CaptureAsset(Name, AssetPaths[Name]);
        }
        return Project.Run(Runtime);
    }
    private void StartDesktopServices()
    {
        if (Native.Lui_InstallWindowsDesktopV1(Runtime) != 0)
            throw new InvalidOperationException("[LUI:Desktop] InstallationFailed");
    }

    public bool RunFile(string Path)
    {
        try
        {
            return Native.Lui_RunScript(Runtime, File.ReadAllText(Path), Path) == 1;
        }
        catch (Exception Error)
        {
            LuiDiagnostics.Error("WinUI", Error.ToString());
            return false;
        }
    }

    public async Task<bool> RunNativeQualificationAsync()
    {
        try
        {
            LuiDiagnostics.Log("Qualification", "Checking native views");
            View Find(string Label) => Views.Values.Single(View => View.Element is not null &&
                AutomationProperties.GetName(View.Element) == Label);
            static void Require(bool Condition, string Message)
            {
                if (!Condition) throw new InvalidOperationException(Message);
            }

            View NativeWindow = Views.Values.Single(View => View.Window?.Title == "LUI Native Mapping");
            View NativeRoot = Views.Values.Single(View => View.ParentId == Views.First(Pair =>
                ReferenceEquals(Pair.Value, NativeWindow)).Key && View.Container is not null);
            View NativeInput = Find("Qualification input");
            View NativeCheck = Find("Qualification check");
            View NativeSlider = Find("Qualification slider");
            View NativeProgress = Find("Qualification progress");
            View NativeHidden = Find("Qualification hidden frame");

            Require(ReferenceEquals(NativeWindow.Window?.Content, NativeWindow.Container), "window content mismatch");
            Require(NativeInput.Element is TextBox Input && Input.Text == "initial" &&
                AutomationProperties.GetHelpText(Input) == "Native help text", "text or accessibility mapping mismatch");
            Require(NativeCheck.Element is CheckBox Check && Check.IsChecked == true && !Check.IsEnabled,
                "checked or disabled state mismatch");
            Require(NativeSlider.Element is Slider Slider && Slider.Minimum == 10 && Slider.Maximum == 20 &&
                Slider.Value == 15, "slider range mismatch");
            Require(NativeProgress.Element is ProgressBar Progress && Progress.Minimum == 0 &&
                Progress.Maximum == 100 && Progress.Value == 40, "progress range mismatch");
            Require(NativeHidden.Element?.Visibility == Visibility.Collapsed, "hidden state mismatch");
            Require(NativeRoot.Container is not null && NativeInput.Element is not null &&
                NativeRoot.Container.Children.Contains(NativeInput.Element), "native parent mapping mismatch");
            Require(NativeInput.Element is FrameworkElement Arranged && Arranged.Width == 240 && Arranged.Height == 36,
                "resolved native bounds mismatch");

            LuiDiagnostics.Log("Qualification", "Waiting for loaded controls");
            TextBox NativeTextBox = (TextBox)NativeInput.Element!;
            if (!NativeTextBox.IsLoaded)
            {
                TaskCompletionSource<bool> Loaded = new(TaskCreationOptions.RunContinuationsAsynchronously);
                NativeTextBox.Loaded += (_, _) => Loaded.TrySetResult(true);
                await Loaded.Task.WaitAsync(TimeSpan.FromSeconds(5));
            }
            LuiDiagnostics.Log("Qualification", "Checking accessibility peer");
            AutomationPeer? Peer = FrameworkElementAutomationPeer.CreatePeerForElement(NativeTextBox);
            Require(Peer is not null && Peer.GetName() == "Qualification input" &&
                Peer.GetHelpText() == "Native help text", "native accessibility peer mismatch");
            Require(NativeTextBox.XamlRoot is not null &&
                NativeTextBox.XamlRoot.RasterizationScale > 0 &&
                Math.Abs(NativeTextBox.ActualWidth - 240) < 1 &&
                Math.Abs(NativeTextBox.ActualHeight - 36) < 1,
                "logical bounds or DPI mapping mismatch");
            LuiDiagnostics.Log("Qualification", $"DPI scale {NativeTextBox.XamlRoot!.RasterizationScale}; theme {LastTheme}");
            LuiDiagnostics.Log("Qualification", "Checking native event round trip");
            TaskCompletionSource<bool> Edited = new(TaskCreationOptions.RunContinuationsAsynchronously);
            NativeTextBox.TextChanged += (_, _) =>
            {
                if (NativeTextBox.Text == "native edit") Edited.TrySetResult(true);
            };
            NativeTextBox.Focus(FocusState.Programmatic);
            NativeTextBox.Text = "native edit";
            ((Slider)NativeSlider.Element!).Value = 17;
            await Edited.Task.WaitAsync(TimeSpan.FromSeconds(5));
            Pump();
            Require(Native.Lui_RunScript(Runtime,
                $"assert(app:GetService('ThemeService').CurrentTheme == '{LastTheme}')",
                "NativeThemeCheck") == 1, "system theme did not reach Luau");
            Require(Native.Lui_RunScript(Runtime,
                "assert(Input.Text == 'native edit', 'text round trip'); assert(Slider.Value == 17, 'slider round trip'); assert(Check.Checked and not Check.Enabled, 'checkbox mapping')",
                "NativeMappingCheck") == 1, "native event did not reach Luau: " + LastError);
            LuiDiagnostics.Log("Qualification", "Native control mapping and event round trip passed");
            return true;
        }
        catch (Exception Error)
        {
            LuiDiagnostics.Error("Qualification", Error.ToString());
            return false;
        }
    }

    public void Pump()
    {
        if(IsDisposed||IsDisposing||Pumping)return;
        if(OwnerWake?.DeferIfBusy()==true)return;
        if(OwnerWake?.Retry()==true)return;
        if(OwnerWake is not null&&Native.Lui_IsOwnerPumpReadyV1(Runtime)!=1){
            // A terminal VM cannot pump, but adapter cleanup still owns charged
            // resources and must retry on ordinary owner turns until released.
            Pumping=true;try{DrainFrameRetirements();}finally{Pumping=false;}return;
        }
        Pumping=true;
        try{
        foreach (var Entry in Views.ToArray()) {
            var View = Entry.Value;
            if (View.IsAlive && View.Window is not null && View.PendingResize) {
                View.PendingResize = false;
                Native.Lui_WindowResized(Runtime, Entry.Key, View.ViewportWidth, View.ViewportHeight);
            }
        }
        CheckTheme();
        OwnerPumpObservation?.Invoke();
        int Completed = Native.Lui_Pump(Runtime);
        DrainFrameRetirements();
        if (Completed > 0) LuiDiagnostics.Log("Scheduler", $"Completed {Completed} callback(s)");
        string Error = LastError;
        if (!string.IsNullOrEmpty(Error) && Error != LastReportedError)
        {
            LastReportedError = Error;
            LuiDiagnostics.Error("Runtime", Error);
        }
        }finally{Pumping=false;}
    }

    private void CheckTheme()
    {
        try
        {
            var Background = UiSettings.GetColorValue(UIColorType.Background);
            string Theme = Accessibility.HighContrast ? "HighContrast" :
                Background.R + Background.G + Background.B < 384 ? "Dark" : "Light";
            if (Theme == LastTheme) return;
            if (Native.Lui_SystemThemeChanged(Runtime, Theme) != 1)
                throw new InvalidOperationException(LastError);
            LastTheme = Theme;
            LuiDiagnostics.Log("Theme", "System theme: " + Theme);
        }
        catch (Exception Error) { LuiDiagnostics.Error("Theme", "Detection failed: " + Error); }
    }

    private int OnWriteClipboardText(IntPtr Context, string Text)
    {
        try
        {
            DataPackage Package = new();
            Package.SetText(Text);
            Clipboard.SetContent(Package);
            return 1;
        }
        catch (Exception Error)
        {
            LuiDiagnostics.Error("Clipboard", "Write failed: " + Error);
            return 0;
        }
    }

    private int OnReadClipboardText(IntPtr Context, ulong RequestId)
    {
        _ = ReadClipboardTextAsync(RequestId);
        return 1;
    }

    private async Task ReadClipboardTextAsync(ulong RequestId)
    {
        try
        {
            DataPackageView Content = Clipboard.GetContent();
            string? Text = Content.Contains(StandardDataFormats.Text) ? await Content.GetTextAsync() : null;
            CompletePlatformRequest(RequestId, Text, null);
        }
        catch (Exception Error)
        {
            LuiDiagnostics.Error("Clipboard", "Read failed: " + Error);
            CompletePlatformRequest(RequestId, null, "Clipboard read failed");
        }
    }

    private int OnOpenFile(IntPtr Context, ulong RequestId)
    {
        Microsoft.UI.Xaml.Window? Window = GetDialogWindow();
        if (Window is null) return 0;
        _ = OpenFileAsync(Window, RequestId);
        return 1;
    }

    private Microsoft.UI.Xaml.Window? GetDialogWindow() => Views.Values.FirstOrDefault(View =>
        View.IsAlive && !View.NativeClosed && View.Window is not null)?.Window;

    private async Task OpenFileAsync(Microsoft.UI.Xaml.Window Window, ulong RequestId)
    {
        try
        {
            Microsoft.Windows.Storage.Pickers.FileOpenPicker Picker = new(Window.AppWindow.Id);
            var Result = await Picker.PickSingleFileAsync();
            CompletePlatformRequest(RequestId, Result?.Path, null);
        }
        catch (Exception Error)
        {
            LuiDiagnostics.Error("Dialog", "File picker failed: " + Error);
            CompletePlatformRequest(RequestId, null, "File picker failed");
        }
    }

    private void CompletePlatformRequest(ulong RequestId, string? Result, string? Error)
    {
        if (IsDisposed || IsDisposing) return;
        if (Native.Lui_CompletePlatformRequest(Runtime, RequestId, Result, Error) != 1)
            LuiDiagnostics.Error("Platform", "Completion was rejected: " + RequestId);
    }

    private static void OnNativeLog(IntPtr Context, string Level, string Message)
    {
        if (Level == "Error") LuiDiagnostics.Error("Luau", Message);
        else
        {
            Trace.WriteLine("[LUI:Luau] " + Message);
            LuiDiagnostics.Log("Luau", Message);
        }
    }

    private int ReportBackendFailure(string Operation, Exception Error)
    {
        string Message = Operation + ": " + Error;
        LuiDiagnostics.Error("WinUI", Message);
        Native.Lui_ReportBackendError(Runtime, Message);
        return 0;
    }

    private int OnCreate(IntPtr Context, int Id, string ClassName)
    {
        Diagnostic($"Create begin {Id} {ClassName}");
        View NewView = new() { ClassName = ClassName };
        try
        {
            switch (ClassName)
            {
                case "Window":
                    NewView.Visible = false;
                    NewView.Window = new Microsoft.UI.Xaml.Window();
                    NewView.Container = new Canvas();
                    NewView.Window.Content = NewView.Container;
                    ConfigureWindowLifecycle(Id, NewView);
                    // Bubble only unhandled keys, after the focused native control's
                    // editing/default behavior. No Handled flag or key state is changed.
                    NewView.Container.KeyDown += (_, Args) => OnWindowShortcut(Id, NewView, Args);
                    NewView.Window.AppWindow.Changed += (_, _) => {
                        if(IsDisposed || IsDisposing || !NewView.IsAlive) return;
                        if(!NewView.Window.DispatcherQueue.TryEnqueue(() => {
                            if(IsDisposed || IsDisposing || !NewView.IsAlive) return;
                            try {
                                foreach(View Editor in Views.Values)
                                    if(Editor.ClassName=="CodeEditor" && Editor.CodeWindowId==Id)
                                        PositionCodeEditor(Editor,Editor.CodeWidth,Editor.CodeHeight);
                            } catch(Exception Error) {LuiDiagnostics.Error("CodeEditor","Window move: "+Error.GetType().Name);}
                        })) LuiDiagnostics.Error("CodeEditor","Window move dispatch unavailable");
                    };
                    NewView.Window.Activated += (_, Args) => {
                        if (Args.WindowActivationState != WindowActivationState.Deactivated || IsDisposed || IsDisposing || !NewView.IsAlive) return;
                        CancelWindowInput(Id);
                    };
                    NewView.Window.Closed += (_, _) =>
                    {
                        if (IsDisposed || IsDisposing || !NewView.IsAlive) return;
                        OnNativeWindowClosed(Id, NewView);
                    };
                    break;
                case "Frame":
                case "ScrollingFrame":
                    NewView.Container = new Canvas();
                    NewView.Element = NewView.Container;
                    break;
                case "TextLabel":
                    NewView.Element = new TextBlock { TextWrapping = TextWrapping.Wrap };
                    break;
                case "ImageLabel":
                    Image Picture = new() { Stretch = Stretch.Uniform };
                    Picture.ImageFailed += (_, Args) =>
                        LuiDiagnostics.Error("Asset", "ImagePresentationFailed");
                    NewView.Element = Picture;
                    break;
                case "FrameSurface":
                    NewView.Element = new Image { Stretch = Stretch.Uniform,
                        HorizontalAlignment = HorizontalAlignment.Stretch, VerticalAlignment = VerticalAlignment.Stretch };
                    break;
                case "TextButton":
                    Button Button = new();
                    Button.Click += (_, _) => {
                        if (!NewView.IsAlive) return;
                        Diagnostic($"Click {Id}"); ActivateMeasured(Id);
                    };
                    NewView.Element = Button;
                    break;
                case "TextBox":
                    TextBox Input = new();
                    NewView.Element = Input;
                    ConnectText(Id, NewView, Input);
                    break;
                case "CodeEditor":
                    NewView.Multiline = true;
                    NewView.MaxLength = 262144;
                    NewView.CodeHost = CodeEditorHost.LuiWinUITextCreatePopupHostV1(IntPtr.Zero,
                        CodeEditorEventCallback, new IntPtr(Id));
                    if (NewView.CodeHost == IntPtr.Zero) throw new InvalidOperationException("[LUI:CodeEditor] Host creation failed");
                    break;
                case "CheckBox":
                    CheckBox Check = new();
                    Check.Checked += (_, _) => {
                        if (NewView.IsAlive && !NewView.ApplyingProperty) {
                            Diagnostic($"Checked {Id}"); Native.Lui_CheckedChanged(Runtime, Id, 1);
                        }
                    };
                    Check.Unchecked += (_, _) => {
                        if (NewView.IsAlive && !NewView.ApplyingProperty) {
                            Diagnostic($"Unchecked {Id}"); Native.Lui_CheckedChanged(Runtime, Id, 0);
                        }
                    };
                    Check.Click += (_, _) => {
                        if (!NewView.IsAlive) return;
                        Diagnostic($"Click {Id}"); ActivateMeasured(Id);
                    };
                    NewView.Element = Check;
                    break;
                case "Slider":
                    Slider Slider = new();
                    Slider.ValueChanged += (_, Args) => {
                        if (NewView.IsAlive && !NewView.ApplyingProperty) {
                            Diagnostic($"ValueChanged {Id} {Args.NewValue}"); Native.Lui_ValueChanged(Runtime, Id, Args.NewValue);
                        }
                    };
                    NewView.Element = Slider;
                    break;
                case "ProgressBar":
                    NewView.Element = new ProgressBar();
                    break;
                default:
                    throw new NotSupportedException($"No native control for {ClassName}");
            }
            if (NewView.Element is not null && NewView.Element is not Control)
            {
                NewView.Content = NewView.Element;
                NewView.Decoration = new Border { IsHitTestVisible = false };
                Grid Shell = new() { Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent) };
                Shell.Children.Add(NewView.Decoration); Shell.Children.Add(NewView.Element);
                NewView.Element = Shell;
            }
            if (NewView.Element is not null)
            {
                if (ClassName == "ScrollingFrame") NewView.Element.PointerWheelChanged += (_, Args) => {
                    if (!NewView.IsAlive || Args.Handled) return;
                    var Point = Args.GetCurrentPoint(NewView.Element);
                    double Delta = -Point.Properties.MouseWheelDelta / 120.0 * 40.0;
                    double X = Point.Properties.IsHorizontalMouseWheel ? -Delta : 0;
                    double Y = Point.Properties.IsHorizontalMouseWheel ? 0 : Delta;
                    if (Native.Lui_ScrollInput(Runtime, Id, X, Y) == 1) Args.Handled = true;
                };
                NewView.Element.GotFocus += (_, _) => {
                    if (!NewView.IsAlive) return;
                    Diagnostic($"GotFocus {Id}"); SyncFocus(NewView.Element);
                };
                NewView.Element.LostFocus += (_, _) => {
                    if (!NewView.IsAlive) return;
                    Diagnostic($"LostFocus {Id}"); SyncFocus(NewView.Element);
                };
                NewView.Element.PointerEntered += (_, _) => {
                    if (!NewView.IsAlive) return;
                    Diagnostic($"PointerEntered {Id}"); Native.Lui_HoverChanged(Runtime, Id, 1);
                };
                NewView.Element.PointerExited += (_, Args) => {
                    if (!NewView.IsAlive) return;
                    Diagnostic($"PointerExited {Id}");
                    Native.Lui_HoverChanged(Runtime, Id, 0);
                    if (!NewView.ActivePointerButtons.ContainsKey(Args.Pointer.PointerId)) OnPointerInput(Id, NewView, 3, Args);
                };
                NewView.Element.AddHandler(UIElement.PointerPressedEvent,
                    new PointerEventHandler((_, Args) => OnPointerInput(Id, NewView, 0, Args)), true);
                NewView.Element.AddHandler(UIElement.PointerMovedEvent,
                    new PointerEventHandler((_, Args) => OnPointerInput(Id, NewView, 1, Args)), true);
                NewView.Element.AddHandler(UIElement.PointerReleasedEvent,
                    new PointerEventHandler((_, Args) => OnPointerInput(Id, NewView, 2, Args)), true);
                NewView.Element.AddHandler(UIElement.PointerCanceledEvent,
                    new PointerEventHandler((_, Args) => OnPointerInput(Id, NewView, 3, Args)), true);
                NewView.Element.AddHandler(UIElement.PointerCaptureLostEvent,
                    new PointerEventHandler((_, Args) => OnPointerCaptureLost(Id, NewView, Args)), true);
                NewView.Element.AddHandler(UIElement.KeyDownEvent,
                    new KeyEventHandler((_, Args) => OnKeyInput(Id, NewView, false, Args)), true);
                NewView.Element.AddHandler(UIElement.KeyUpEvent,
                    new KeyEventHandler((_, Args) => OnKeyInput(Id, NewView, true, Args)), true);
            }
            Views.Add(Id, NewView);
            if (NewView.Window is not null && NewView.Container is not null)
            {
                NewView.Container.SizeChanged += (_, Args) =>
                {
                    if (!NewView.IsAlive || Args.NewSize.Width <= 0 || Args.NewSize.Height <= 0) return;
                    Diagnostic($"WindowResized {Id} {Args.NewSize.Width} {Args.NewSize.Height}");
                    NewView.ViewportWidth = Args.NewSize.Width; NewView.ViewportHeight = Args.NewSize.Height;
                    NewView.PendingResize = true;
                };
            }
            Diagnostic($"Create end {Id} {ClassName}");
            return 1;
        }
        catch (Exception Error)
        {
            NewView.IsAlive = false;
            if (NewView.CodeHost != IntPtr.Zero) CodeEditorHost.LuiWinUITextDestroyHostV1(NewView.CodeHost);
            try { NewView.Window?.Close(); }
            catch (Exception CleanupError) { LuiDiagnostics.Error("WinUI", "Create cleanup: " + CleanupError); }
            return ReportBackendFailure("Create", Error);
        }
    }

    private void OnPointerInput(int Id, View Target, int Phase, PointerRoutedEventArgs Args)
    {
        try
        {
            if (!Target.IsAlive || Target.Element is null || !Views.TryGetValue(Id, out View? Current) ||
                !ReferenceEquals(Target, Current)) return;
            int Device = Args.Pointer.PointerDeviceType switch
            {
                Microsoft.UI.Input.PointerDeviceType.Mouse => 0,
                Microsoft.UI.Input.PointerDeviceType.Pen => 1,
                Microsoft.UI.Input.PointerDeviceType.Touch => 2,
                Microsoft.UI.Input.PointerDeviceType.Touchpad => 3,
                _ => -1,
            };
            if (Device < 0) return;
            uint PointerId = Args.Pointer.PointerId;
            if (Phase == 3)
            {
                CancelCapturedPointer(Id, Target, PointerId, Device, Args);
                return;
            }
            var Point = Args.GetCurrentPoint(Target.Element);
            int Button = Device == 0 && (Phase == 0 || Phase == 2) ? Point.Properties.PointerUpdateKind switch
            {
                Microsoft.UI.Input.PointerUpdateKind.LeftButtonPressed or Microsoft.UI.Input.PointerUpdateKind.LeftButtonReleased => 1,
                Microsoft.UI.Input.PointerUpdateKind.RightButtonPressed or Microsoft.UI.Input.PointerUpdateKind.RightButtonReleased => 2,
                Microsoft.UI.Input.PointerUpdateKind.MiddleButtonPressed or Microsoft.UI.Input.PointerUpdateKind.MiddleButtonReleased => 3,
                Microsoft.UI.Input.PointerUpdateKind.XButton1Pressed or Microsoft.UI.Input.PointerUpdateKind.XButton1Released => 4,
                Microsoft.UI.Input.PointerUpdateKind.XButton2Pressed or Microsoft.UI.Input.PointerUpdateKind.XButton2Released => 5,
                _ => 0,
            } : 0;
            if (Device == 0 && (Phase == 0 || Phase == 2) && Button == 0) return;
            ulong FocusBefore = FocusGeneration;
            long Started = Stopwatch.GetTimestamp();
            int Accepted = Native.Lui_PointerInputV2(Runtime, Id, Phase, Device, Point.PointerId,
                Point.Position.X, Point.Position.Y, Button);
            Native.Lui_RecordPerformance(Runtime, 1, Stopwatch.GetElapsedTime(Started).TotalMilliseconds);
            if (Phase == 2) RetireCapturedButton(Target, PointerId, Button, Args);
            if (Accepted != 1) return;
            if (!Target.IsAlive || !Views.TryGetValue(Id, out Current) || !ReferenceEquals(Target, Current) ||
                !CanReceiveFocus(Id) || Target.Element is null ||
                (Phase == 0 && FocusGeneration != FocusBefore && FocusedViewId != Id)) {
                if (Phase == 0) Native.Lui_PointerInputV2(Runtime, Id, 3, Device, PointerId, 0, 0, 0);
                return;
            }
            if (Phase == 0)
            {
                if (!Target.ActivePointerButtons.TryGetValue(PointerId, out HashSet<int>? Buttons))
                {
                    Buttons = new HashSet<int>(); Target.ActivePointerButtons.Add(PointerId, Buttons);
                    Target.CaptureGenerations[PointerId] = ++Target.NextCaptureGeneration;
                    if (!Target.Element.CapturePointer(Args.Pointer))
                    {
                        Target.ActivePointerButtons.Remove(PointerId);
                        Target.CaptureGenerations.Remove(PointerId);
                        Native.Lui_PointerInputV2(Runtime, Id, 3, Device, PointerId, 0, 0, 0);
                        return;
                    }
                }
                Buttons.Add(Button);
            }
        }
        catch (Exception Error) { LuiDiagnostics.Error("Input", "Pointer: " + Error); }
    }

    private static void RetireCapturedButton(View Target, uint PointerId, int Button, PointerRoutedEventArgs Args)
    {
        if (!Target.ActivePointerButtons.TryGetValue(PointerId, out HashSet<int>? Buttons)) return;
        Buttons.Remove(Button);
        if (Buttons.Count != 0) return;
        Target.ActivePointerButtons.Remove(PointerId);
        Target.CaptureGenerations.Remove(PointerId);
        Target.Element?.ReleasePointerCapture(Args.Pointer);
    }

    private void CancelCapturedPointer(int Id, View Target, uint PointerId, int Device,
        PointerRoutedEventArgs? Args = null)
    {
        if (!Target.ActivePointerButtons.Remove(PointerId)) return;
        Target.CaptureGenerations.Remove(PointerId);
        Native.Lui_PointerInputV2(Runtime, Id, 3, Device, PointerId, 0, 0, 0);
        if (Args is not null) Target.Element?.ReleasePointerCapture(Args.Pointer);
    }

    private void CancelCapturedPointers(View Target)
    {
        if (Target.ActivePointerButtons.Count == 0) return;
        int Id = Views.FirstOrDefault(Entry => ReferenceEquals(Entry.Value, Target)).Key;
        if (Id == 0) return;
        foreach (uint PointerId in Target.ActivePointerButtons.Keys.ToArray())
            CancelCapturedPointer(Id, Target, PointerId, 0);
        Target.Element?.ReleasePointerCaptures();
    }

    private void OnPointerCaptureLost(int Id, View Target, PointerRoutedEventArgs Args)
    {
        try
        {
            if (!Target.IsAlive || Target.Element is null) return;
            uint PointerId = Args.Pointer.PointerId;
            int Device = Args.Pointer.PointerDeviceType switch
            {
                Microsoft.UI.Input.PointerDeviceType.Mouse => 0,
                Microsoft.UI.Input.PointerDeviceType.Pen => 1,
                Microsoft.UI.Input.PointerDeviceType.Touch => 2,
                Microsoft.UI.Input.PointerDeviceType.Touchpad => 3,
                _ => -1,
            };
            if (Device < 0) return;
            if (!Target.CaptureGenerations.TryGetValue(PointerId, out ulong Generation)) return;
            Target.Element.DispatcherQueue.TryEnqueue(() =>
            {
                try
                {
                    if (!Target.IsAlive) return;
                    if (!Views.TryGetValue(Id, out View? Current) || !ReferenceEquals(Target, Current)) return;
                    if (!Target.CaptureGenerations.TryGetValue(PointerId, out ulong ActiveGeneration) ||
                        ActiveGeneration != Generation) return;
                    Diagnostic($"PointerCaptureLost {Id} {PointerId}");
                    CancelCapturedPointer(Id, Target, PointerId, Device);
                }
                catch (Exception Error) { LuiDiagnostics.Error("Input", "Pointer capture: " + Error); }
            });
        }
        catch (Exception Error) { LuiDiagnostics.Error("Input", "Pointer capture: " + Error); }
    }

    private void SyncFocus(UIElement Source)
    {
        try
        {
            if (Source.XamlRoot is null) return;
            DependencyObject? Focused = FocusManager.GetFocusedElement(Source.XamlRoot) as DependencyObject;
            int NewId = GetViewId(Focused);
            if (NewId != 0 && !CanReceiveFocus(NewId)) NewId = 0;
            if (NewId == FocusedViewId) return;
            int PreviousId = FocusedViewId;
            FocusedViewId = NewId;
            ++FocusGeneration;
            if (PreviousId != 0) {
                Native.Lui_FocusChanged(Runtime, PreviousId, 0);
                if (Views.TryGetValue(PreviousId, out View? Previous)) CancelCapturedPointers(Previous);
            }
            if (NewId != 0) Native.Lui_FocusChanged(Runtime, NewId, 1);
        }
        catch (Exception Error) { LuiDiagnostics.Error("Input", "Focus: " + Error); }
    }

    private int GetViewId(DependencyObject? Source)
    {
        while (Source is not null)
        {
            foreach (KeyValuePair<int, View> Entry in Views)
                if (ReferenceEquals(Entry.Value.Element, Source)) return Entry.Key;
            Source = VisualTreeHelper.GetParent(Source);
        }
        return 0;
    }

    private void OnKeyInput(int Id, View Target, bool Released, KeyRoutedEventArgs Args)
    {
        try
        {
            if (!Target.IsAlive || Target.Element is null || !Views.TryGetValue(Id, out View? Current) || !ReferenceEquals(Target, Current) ||
                GetViewId(Args.OriginalSource as DependencyObject) != Id) return;
            string? Key = GetPortableKey(Args.Key);
            if (Key is null) return;
            int Phase = Released ? 2 : Args.KeyStatus.WasKeyDown ? 1 : 0;
            Diagnostic($"KeyInput {Id} {Phase} {Key}");
            long Started = Stopwatch.GetTimestamp();
            Native.Lui_KeyInput(Runtime, Id, Phase, Key);
            Native.Lui_RecordPerformance(Runtime, 1, Stopwatch.GetElapsedTime(Started).TotalMilliseconds);
        }
        catch (Exception Error) { LuiDiagnostics.Error("Input", "Keyboard: " + Error); }
    }

    private void OnWindowShortcut(int Id, View Target, KeyRoutedEventArgs Args)
    {
        try
        {
            if (IsDisposed || IsDisposing || Args.Handled || !Target.IsAlive || Target.NativeClosed ||
                Target.Container?.XamlRoot is null || !Views.TryGetValue(Id, out View? Current) ||
                !ReferenceEquals(Target, Current)) return;
            string? Key = GetPortableKey(Args.Key);
            if (Key is null || Key is "Control" or "Shift" or "Alt" or "CapsLock") return;
            int OriginId = GetViewId(FocusManager.GetFocusedElement(Target.Container.XamlRoot) as DependencyObject);
            if (OriginId == 0 || !IsInWindow(OriginId, Id) || !CanReceiveFocus(OriginId)) return;
            static bool Down(Windows.System.VirtualKey Key) =>
                (Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(Key) & Windows.UI.Core.CoreVirtualKeyStates.Down) != 0;
            uint Modifiers = (Down(Windows.System.VirtualKey.Control) ? 1u : 0u) |
                (Down(Windows.System.VirtualKey.Shift) ? 2u : 0u) | (Down(Windows.System.VirtualKey.Menu) ? 4u : 0u);
            Native.Lui_WindowShortcut(Runtime, Id, OriginId, Key, Modifiers, Args.KeyStatus.WasKeyDown ? 1 : 0);
        }
        catch (Exception Error) { LuiDiagnostics.Error("Input", "Window shortcut: " + Error); }
    }

    private static string? GetPortableKey(Windows.System.VirtualKey Key)
    {
        int Code = (int)Key;
        if (Code >= 65 && Code <= 90) return ((char)Code).ToString();
        if (Code >= 48 && Code <= 57) return ((char)Code).ToString();
        if (Code >= 112 && Code <= 123) return $"F{Code - 111}";
        return Key switch
        {
            Windows.System.VirtualKey.Enter => "Enter",
            Windows.System.VirtualKey.Escape => "Escape",
            Windows.System.VirtualKey.Tab => "Tab",
            Windows.System.VirtualKey.Space => "Space",
            Windows.System.VirtualKey.Back => "Backspace",
            Windows.System.VirtualKey.Delete => "Delete",
            Windows.System.VirtualKey.Insert => "Insert",
            Windows.System.VirtualKey.Home => "Home",
            Windows.System.VirtualKey.End => "End",
            Windows.System.VirtualKey.PageUp => "PageUp",
            Windows.System.VirtualKey.PageDown => "PageDown",
            Windows.System.VirtualKey.Left => "ArrowLeft",
            Windows.System.VirtualKey.Right => "ArrowRight",
            Windows.System.VirtualKey.Up => "ArrowUp",
            Windows.System.VirtualKey.Down => "ArrowDown",
            Windows.System.VirtualKey.Shift => "Shift",
            Windows.System.VirtualKey.Control => "Control",
            Windows.System.VirtualKey.Menu => "Alt",
            Windows.System.VirtualKey.CapitalLock => "CapsLock",
            _ => null,
        };
    }

    private bool CanReceiveFocus(int Id)
    {
        if (!Views.TryGetValue(Id, out View? Target) || !Target.InputVisible) return false;
        HashSet<int> Visited = new();
        while (Id != 0)
        {
            if (!Visited.Add(Id) || !Views.TryGetValue(Id, out View? Current) || !Current.Visible || !Current.Enabled)
                return false;
            Id = Current.ParentId;
        }
        return true;
    }

    private bool IsInWindow(int Id, int WindowId)
    {
        for (int Depth = 0; Depth <= Views.Count && Id != 0; ++Depth)
        {
            if (Id == WindowId) return true;
            if (!Views.TryGetValue(Id, out View? Target)) return false;
            Id = Target.ParentId;
        }
        return false;
    }

    private void CancelWindowInput(int WindowId)
    {
        if (FocusedViewId != 0 && IsInWindow(FocusedViewId, WindowId))
        {
            int PreviousId = FocusedViewId; FocusedViewId = 0; ++FocusGeneration;
            Native.Lui_FocusChanged(Runtime, PreviousId, 0);
        }
        foreach (KeyValuePair<int, View> Entry in Views.ToArray())
            if (IsInWindow(Entry.Key, WindowId)) CancelCapturedPointers(Entry.Value);
    }

    private int OnRequestFocus(IntPtr Context, int Id)
    {
        try
        {
            if (!CanReceiveFocus(Id) || !Views.TryGetValue(Id, out View? Target)) return 0;
            if(Target.CodeHost!=IntPtr.Zero) return CodeEditorHost.LuiWinUITextFocusV1(Target.CodeHost);
            if (
                Target.Element is not Control Control) return 0;
            return Control.Focus(FocusState.Programmatic) ? 1 : 0;
        }
        catch (Exception Error) { LuiDiagnostics.Error("Input", "Focus request: " + Error); return 0; }
    }

    private void ClearInvalidFocus()
    {
        if (FocusedViewId == 0 || CanReceiveFocus(FocusedViewId)) return;
        int PreviousId = FocusedViewId;
        FocusedViewId = 0;
        ++FocusGeneration;
        Native.Lui_FocusChanged(Runtime, PreviousId, 0);
        if (Views.TryGetValue(PreviousId, out View? Previous)) CancelCapturedPointers(Previous);
    }

    private int OnProperty(IntPtr Context, int Id, string Name, string Value)
    {
        Diagnostic($"Property begin {Id} {Name}");
        try
        {
            if (!Views.TryGetValue(Id, out View? Target))
                return ReportBackendFailure("Property", new InvalidOperationException($"Missing view {Id}"));
            Target.ApplyingProperty = true;
            try
            {
                UIElement? Content = Target.Content ?? Target.Element;
                if ((Name == "__Style" && Target.StyleJson != Value) ||
                    (Name == "Text" && Content is TextBlock Text && Text.Text != Value) ||
                    (Name == "Text" && Content is Button ButtonText && ButtonText.Content as string != Value) ||
                    (Name == "Visible" && Target.Visible != (Value == "true")) ||
                    (Name == "Enabled" && Target.Enabled != (Value == "true"))) MarkPresentation();
                switch (Name)
                {
                    case "__TextConfig":
                        if (Target.ClassName == "CodeEditor") ConfigureCodeEditor(Target, Value);
                        else Target = ConfigureText(Id, Target, Value);
                        break;
                    case "__Selection":
                        string[] Selection = Value.Split(',');
                        Target.SelectionStart = int.Parse(Selection[0], CultureInfo.InvariantCulture);
                        Target.SelectionLength = int.Parse(Selection[1], CultureInfo.InvariantCulture);
                        if (Target.ClassName == "CodeEditor") RequireCodeEditor(CodeEditorHost.SetSelection(Target.CodeHost, Target.SelectionStart, Target.SelectionLength), "Selection");
                        else ApplySelection(Target);
                        break;
                    case "__CodeStyles" when Target.ClassName == "CodeEditor":
                        ulong CurrentRevision=0;
                        if(Value.Length!=0) {
                            int HeaderEnd=Value.IndexOf(';');
                            if(HeaderEnd<0) HeaderEnd=Value.Length;
                            RequireCodeEditor(ulong.TryParse(Value.AsSpan(0,HeaderEnd),NumberStyles.None,
                                CultureInfo.InvariantCulture,out ulong ExpectedRevision) &&
                                Native.Lui_GetCodeEditorTextRevision(Runtime,Id,out CurrentRevision)==1,"Style revision");
                            if(ExpectedRevision!=CurrentRevision) break;
                        }
                        RequireCodeEditor(CodeEditorHost.SetStyles(Target.CodeHost, Value), "Styles");
                        break;
                    case "__Style":
                        ApplyStyle(Target, Value);
                        break;
                    case "Title" when Target.Window is not null:
                        Target.Window.Title = Value;
                        break;
                    case "Text" when (Target.Content ?? Target.Element) is TextBlock Label:
                        Label.Text = Value;
                        break;
                    case "Source" when (Target.Content ?? Target.Element) is Image Picture:
                        if (string.IsNullOrEmpty(Value)) ReleaseImage(Target);
                        break;
                    case "ScaleMode" when Target.ClassName == "FrameSurface" && Target.Content is Image SurfaceImage:
                        SurfaceImage.Stretch = Value switch {"Fill" => Stretch.UniformToFill, "Stretch" => Stretch.Fill, _ => Stretch.Uniform};
                        MarkPresentation();
                        break;
                    case "Text" when Target.Element is Button Button:
                        Button.Content = Value;
                        break;
                    case "Text" when Target.Element is TextBox Input:
                        Target.Text = Value;
                        Input.Text = Value;
                        ApplySelection(Target);
                        break;
                    case "Text" when Target.ClassName == "CodeEditor":
                        RequireCodeEditor(CodeEditorHost.LuiWinUITextSetTextV1(Target.CodeHost, Value)==1, "Text");
                        break;
                    case "Text" when Target.Element is PasswordBox Password:
                        Target.Text = Value;
                        Password.Password = Value;
                        break;
                    case "Text" when Target.Element is CheckBox Check:
                        Check.Content = Value;
                        break;
                    case "AccessibilityLabel" when Target.Element is not null:
                        AutomationProperties.SetName(Target.Content is TextBlock or Image ? Target.Content : Target.Element, Value);
                        break;
                    case "AccessibilityDescription" when Target.Element is not null:
                        AutomationProperties.SetHelpText(Target.Content is TextBlock or Image ? Target.Content : Target.Element, Value);
                        break;
                    case "Checked" when Target.Element is CheckBox Check:
                        Check.IsChecked = Value == "true";
                        break;
                    case "Enabled" when Target.Element is Control Control:
                        Target.Enabled = Value == "true";
                        Control.IsEnabled = Target.Enabled && Target.InputVisible;
                        break;
                    case "Enabled" when Target.ClassName == "CodeEditor":
                        Target.Enabled=Value=="true";
                        RequireCodeEditor(CodeEditorHost.LuiWinUITextSetEnabledV1(Target.CodeHost, Target.Enabled && Target.InputVisible?1:0)==1,"Enabled");
                        break;
                    case "Minimum" or "Maximum" or "Value" when Target.Element is Slider Slider:
                        SetRange(Slider, Name, Value);
                        break;
                    case "Minimum" or "Maximum" or "Value" when Target.Element is ProgressBar Progress:
                        SetRange(Progress, Name, Value);
                        break;
                    case "Visible" when Target.Window is not null:
                        Target.Visible = Value == "true";
                        if (Value == "true") Target.Window.Activate();
                        else Target.Window.AppWindow.Hide();
                        break;
                    case "Visible" when Target.Element is not null:
                        Target.Visible = Value == "true";
                        Target.Element.Visibility = Value == "true" ? Visibility.Visible : Visibility.Collapsed;
                        break;
                    case "Visible" when Target.ClassName == "CodeEditor":
                        Target.Visible=Value=="true";
                        RequireCodeEditor(CodeEditorHost.LuiWinUITextSetVisibleV1(Target.CodeHost,0)==1,"Hide until current Present");
                        break;
                }
                if (Name is "Enabled" or "Visible") ClearInvalidFocus();
            }
            finally { Target.ApplyingProperty = false; }
            Diagnostic($"Property end {Id} {Name}");
            return 1;
        }
        catch (Exception Error) { return ReportBackendFailure("Property", Error); }
    }

    private static void SetRange(Slider Target, string Name, string Value)
    {
        double Parsed = double.Parse(Value, CultureInfo.InvariantCulture);
        if (Name == "Minimum") Target.Minimum = Parsed;
        else if (Name == "Maximum") Target.Maximum = Parsed;
        else Target.Value = Parsed;
    }

    private static void SetRange(ProgressBar Target, string Name, string Value)
    {
        double Parsed = double.Parse(Value, CultureInfo.InvariantCulture);
        if (Name == "Minimum") Target.Minimum = Parsed;
        else if (Name == "Maximum") Target.Maximum = Parsed;
        else Target.Value = Parsed;
    }

    private void ApplyStyle(View Target, string Value)
    {
        Target.StyleJson = Value;
        using var Document = JsonDocument.Parse(Value); var Style = Document.RootElement;
        bool Contrast = Style.GetProperty("HighContrast").GetBoolean();
        Brush? Color(string Name, double Alpha, Windows.UI.ViewManagement.UIColorType Role) {
            var Channels = Style.GetProperty(Name);
            if (Contrast) return new SolidColorBrush(UiSettings.GetColorValue(Role));
            if (Channels.ValueKind == JsonValueKind.Null) return null;
            return new SolidColorBrush(Windows.UI.Color.FromArgb((byte)Math.Round(Alpha*255),
                (byte)Math.Round(Channels[0].GetDouble()*255), (byte)Math.Round(Channels[1].GetDouble()*255), (byte)Math.Round(Channels[2].GetDouble()*255)));
        }
        var Background = Color("BackgroundColor3", 1-Style.GetProperty("BackgroundTransparency").GetDouble(), Windows.UI.ViewManagement.UIColorType.Background);
        var Foreground = Color("TextColor3", 1, Windows.UI.ViewManagement.UIColorType.Foreground);
        var Border = Color("BorderColor3", 1, Windows.UI.ViewManagement.UIColorType.Foreground);
        double BorderSize = Style.GetProperty("BorderSizePixel").GetDouble(), Radius = Style.GetProperty("CornerRadius").GetDouble();
        var Family = new FontFamily(Style.GetProperty("FontFamily").GetString() is "System" ? "Segoe UI" : Style.GetProperty("FontFamily").GetString());
        var Weight = new Windows.UI.Text.FontWeight { Weight = Style.GetProperty("FontWeight").GetString() is "Bold" ? (ushort)700 : (ushort)400 };
        double Size = Style.GetProperty("TextSize").GetDouble();
        if (Target.Element is not null) Target.Element.Opacity = Style.GetProperty("Opacity").GetDouble();
        if (Target.Decoration is not null) {
            Target.Decoration.Background = Background; Target.Decoration.BorderBrush = Border;
            Target.Decoration.BorderThickness = new Thickness(BorderSize); Target.Decoration.CornerRadius = new CornerRadius(Radius);
        }
        UIElement? Element = Target.Content ?? Target.Element;
        if (Element is TextBox Input) Input.TextWrapping = Target.Multiline && Style.GetProperty("TextWrapped").GetBoolean() ? TextWrapping.Wrap : TextWrapping.NoWrap;
        if (Element is Control Control) {
            if (Background is null) Control.ClearValue(Microsoft.UI.Xaml.Controls.Control.BackgroundProperty); else Control.Background = Background;
            if (Foreground is null) Control.ClearValue(Microsoft.UI.Xaml.Controls.Control.ForegroundProperty); else Control.Foreground = Foreground;
            if (Border is null) Control.ClearValue(Microsoft.UI.Xaml.Controls.Control.BorderBrushProperty); else Control.BorderBrush = Border;
            Control.BorderThickness = new Thickness(Contrast ? Math.Max(1, BorderSize) : BorderSize); Control.CornerRadius = new CornerRadius(Radius);
            Control.FontFamily = Family; Control.FontSize = Size; Control.FontWeight = Weight;
            Control.HorizontalContentAlignment = Style.GetProperty("TextXAlignment").GetString() switch {"Center"=>HorizontalAlignment.Center,"Right"=>HorizontalAlignment.Right,_=>HorizontalAlignment.Left};
            Control.VerticalContentAlignment = Style.GetProperty("TextYAlignment").GetString() switch {"Center"=>VerticalAlignment.Center,"Bottom"=>VerticalAlignment.Bottom,_=>VerticalAlignment.Top};
        } else if (Element is TextBlock Text) {
            if (Foreground is null) Text.ClearValue(TextBlock.ForegroundProperty); else Text.Foreground = Foreground;
            Text.FontFamily = Family; Text.FontSize = Size; Text.FontWeight = Weight;
            Text.TextWrapping = Style.GetProperty("TextWrapped").GetBoolean() ? TextWrapping.Wrap : TextWrapping.NoWrap;
            Text.TextAlignment = Style.GetProperty("TextXAlignment").GetString() switch {"Center"=>TextAlignment.Center,"Right"=>TextAlignment.Right,_=>TextAlignment.Left};
            Text.VerticalAlignment = Style.GetProperty("TextYAlignment").GetString() switch {"Center"=>VerticalAlignment.Center,"Bottom"=>VerticalAlignment.Bottom,_=>VerticalAlignment.Top};
        }
    }

    private static int ScalarIndex(string Text, int Utf16)
    {
        int Offset = 0, Scalars = 0;
        foreach (var Rune in Text.EnumerateRunes()) {
            if (Offset + Rune.Utf16SequenceLength > Utf16) break;
            Offset += Rune.Utf16SequenceLength; Scalars++;
        }
        return Scalars;
    }
    private static int Utf16Index(string Text, int Scalars)
    {
        int Offset = 0;
        foreach (var Rune in Text.EnumerateRunes()) {if (Scalars-- <= 0) break; Offset += Rune.Utf16SequenceLength;}
        return Offset;
    }
    private void ApplySelection(View Target)
    {
        if (Target.Element is TextBox Input) {
            int Start = Utf16Index(Input.Text, Target.SelectionStart);
            Input.Select(Start, Utf16Index(Input.Text, Target.SelectionStart + Target.SelectionLength) - Start);
        }
    }
    private void ReportText(int Id, View Target, string Text, int Start, int Length)
    {
        if (!Target.IsAlive || Target.ApplyingProperty || Target.Composing) return;
        int Count = ScalarIndex(Text, Text.Length);
        bool Valid;
        try {Valid = Count <= Target.MaxLength && new UTF8Encoding(false, true).GetByteCount(Text) <= 65534;}
        catch (EncoderFallbackException) {Valid = false;}
        if (!Valid) {
            Target.ApplyingProperty = true;
            if (Target.Element is TextBox Input) {Input.Text = Target.Text; ApplySelection(Target);}
            else if (Target.Element is PasswordBox Password) Password.Password = Target.Text;
            Target.ApplyingProperty = false; return;
        }
        Target.Text = Text;
        Target.SelectionStart = Target.Secret ? 0 : ScalarIndex(Text, Start);
        Target.SelectionLength = Target.Secret ? 0 : ScalarIndex(Text, Start + Length) - Target.SelectionStart;
        Native.Lui_TextInput(Runtime, Id, Text, Target.SelectionStart, Target.SelectionLength);
    }
    private void ConnectText(int Id, View Target, TextBox Input)
    {
        bool Current() => Target.IsAlive && ReferenceEquals(Target.Element, Input);
        Input.MaxLength = Target.MaxLength * 2;
        Input.TextChanged += (_, _) => {if (Current()) ReportText(Id, Target, Input.Text, Input.SelectionStart, Input.SelectionLength);};
        Input.SelectionChanged += (_, _) => {if (Current()) ReportText(Id, Target, Input.Text, Input.SelectionStart, Input.SelectionLength);};
        Input.TextCompositionStarted += (_, _) => {
            if (!Current() || Target.ApplyingProperty) return;
            Target.Composing = true; Native.Lui_TextComposition(Runtime, Id, 0, string.Empty);
        };
        Input.TextCompositionChanged += (_, Args) => {
            if (!Current() || Target.ApplyingProperty || !Target.Composing) return;
            int Start = Math.Clamp(Args.StartIndex, 0, Input.Text.Length);
            int Length = Math.Clamp(Args.Length, 0, Input.Text.Length - Start);
            Native.Lui_TextComposition(Runtime, Id, 1, Input.Text.Substring(Start, Length));
        };
        Input.TextCompositionEnded += (_, _) => {
            if (!Current() || Target.ApplyingProperty) return;
            Target.Composing = false;
            ReportText(Id, Target, Input.Text, Input.SelectionStart, Input.SelectionLength);
            Native.Lui_TextComposition(Runtime, Id, 2, string.Empty);
        };
    }
    private View ConfigureText(int Id, View Target, string Value)
    {
        string[] Fields = Value.Split(',', 4);
        bool Multiline = Fields[0] == "1", Secret = Fields[1] == "1";
        int Limit = int.Parse(Fields[2], CultureInfo.InvariantCulture); string Placeholder = Fields[3];
        if (Target.Secret != Secret || Target.Composing) {
            UIElement? Previous = Target.Element;
            int ParentId = Target.ParentId;
            CancelCapturedPointers(Target);
            Target.IsAlive = false; Views.Remove(Id);
            if (ParentId != 0 && Views.TryGetValue(ParentId, out var Parent)) Parent.Container?.Children.Remove(Previous!);
            if (OnCreate(IntPtr.Zero, Id, "TextBox") != 1) throw new InvalidOperationException("Text replacement failed");
            View Next = Views[Id]; Next.ApplyingProperty = true;
            if (Secret) {
                Next.Element = new PasswordBox {PasswordRevealMode = PasswordRevealMode.Hidden}; var Password = (PasswordBox)Next.Element;
                Password.PasswordChanged += (_, _) => ReportText(Id, Next, Password.Password, 0, 0);
                Password.GotFocus += (_, _) => {if (Next.IsAlive) SyncFocus(Password);};
                Password.LostFocus += (_, _) => {if (Next.IsAlive) SyncFocus(Password);};
                Password.PointerEntered += (_, _) => {if (Next.IsAlive) Native.Lui_HoverChanged(Runtime, Id, 1);};
                Password.PointerExited += (_, Args) => {if (Next.IsAlive) {Native.Lui_HoverChanged(Runtime, Id, 0); if (!Next.ActivePointerButtons.ContainsKey(Args.Pointer.PointerId)) OnPointerInput(Id, Next, 3, Args);}};
                Password.AddHandler(UIElement.PointerPressedEvent, new PointerEventHandler((_, Args) => OnPointerInput(Id, Next, 0, Args)), true);
                Password.AddHandler(UIElement.PointerMovedEvent, new PointerEventHandler((_, Args) => OnPointerInput(Id, Next, 1, Args)), true);
                Password.AddHandler(UIElement.PointerReleasedEvent, new PointerEventHandler((_, Args) => OnPointerInput(Id, Next, 2, Args)), true);
                Password.AddHandler(UIElement.PointerCanceledEvent, new PointerEventHandler((_, Args) => OnPointerInput(Id, Next, 3, Args)), true);
                Password.AddHandler(UIElement.PointerCaptureLostEvent, new PointerEventHandler((_, Args) => OnPointerCaptureLost(Id, Next, Args)), true);
                Password.AddHandler(UIElement.KeyDownEvent, new KeyEventHandler((_, Args) => OnKeyInput(Id, Next, false, Args)), true);
                Password.AddHandler(UIElement.KeyUpEvent, new KeyEventHandler((_, Args) => OnKeyInput(Id, Next, true, Args)), true);
            }
            Next.Secret = Secret; Next.ParentId = ParentId; Next.Visible = Target.Visible; Next.Enabled = Target.Enabled;
            Next.InputVisible = Target.InputVisible; Next.Text = Target.Text;
            Next.BoundsX = Target.BoundsX; Next.BoundsY = Target.BoundsY;
            Next.StyleJson = Target.StyleJson;
            if (Next.Element is FrameworkElement Element && Previous is FrameworkElement Old) {
                Element.Width = Old.Width; Element.Height = Old.Height;
                Element.Visibility = Old.Visibility; Element.Opacity = Old.Opacity;
                Element.Clip = Old.Clip is null ? null : new RectangleGeometry {Rect = Old.Clip.Rect};
                Canvas.SetLeft(Element, Canvas.GetLeft(Old)); Canvas.SetTop(Element, Canvas.GetTop(Old)); Canvas.SetZIndex(Element, Canvas.GetZIndex(Old));
                AutomationProperties.SetName(Element, AutomationProperties.GetName(Old));
                AutomationProperties.SetHelpText(Element, AutomationProperties.GetHelpText(Old));
            }
            if (ParentId != 0 && Views.TryGetValue(ParentId, out Parent)) Parent.Container?.Children.Add(Next.Element!);
            Target = Next; Native.Lui_FocusChanged(Runtime, Id, 0);
        }
        Target.Multiline = Multiline; Target.Secret = Secret; Target.MaxLength = Limit; Target.Placeholder = Placeholder;
        Target.Composing = false;
        Target.ApplyingProperty = true;
        if (Target.Element is TextBox Input) {
            Input.AcceptsReturn = Multiline; Input.TextWrapping = Multiline ? TextWrapping.Wrap : TextWrapping.NoWrap;
            Input.MaxLength = Limit * 2; Input.PlaceholderText = Placeholder; Input.Text = Target.Text; ApplySelection(Target);
        } else if (Target.Element is PasswordBox Password) {
            Password.MaxLength = Limit * 2; Password.PlaceholderText = Placeholder; Password.Password = Target.Text;
        }
        if (Target.Element is Control Control) Control.IsEnabled = Target.Enabled && Target.InputVisible;
        if (!string.IsNullOrEmpty(Target.StyleJson)) ApplyStyle(Target, Target.StyleJson);
        return Target;
    }

    private int OnParent(IntPtr Context, int Id, int ParentId)
    {
        Diagnostic($"Parent {Id} {ParentId}");
        try
        {
            View Child = Views[Id];
            if (Child.Element is null) {
                Child.ParentId=ParentId;
                AttachCodeEditors();
                ClearInvalidFocus();
                return 1;
            }
            if (Child.ParentId != 0 && Views.TryGetValue(Child.ParentId, out View? OldParent))
                OldParent.Container?.Children.Remove(Child.Element);
            Child.ParentId = ParentId;
            if (ParentId != 0)
            {
                Canvas Container = Views[ParentId].Container
                    ?? throw new InvalidOperationException($"Parent {ParentId} has no native container");
                MarkPresentation();
                Container.Children.Add(Child.Element);
            }
            AttachCodeEditors();
            ClearInvalidFocus();
            return 1;
        }
        catch (Exception Error) { return ReportBackendFailure("Parent", Error); }
    }

    private int OnArrange(IntPtr Context, int Id, double X, double Y, double Width, double Height, int ResizeWindow)
    {
        Diagnostic($"Arrange {Id} {X} {Y} {Width} {Height}");
        try
        {
            if (!Views.TryGetValue(Id, out View? Target))
                return ReportBackendFailure("Arrange", new InvalidOperationException($"Missing view {Id}"));
            Target.BoundsX = X;
            Target.BoundsY = Y;
            if (Target.ClassName == "CodeEditor") PositionCodeEditor(Target, Width, Height);
            if (Target.Window is not null)
            {
                if (ResizeWindow != 0)
                {
                    Target.Window.AppWindow.Resize(new Windows.Graphics.SizeInt32(
                        Math.Max(1, (int)Math.Round(Width)), Math.Max(1, (int)Math.Round(Height))));
                }
            }
            else if (Target.Element is FrameworkElement Element)
            {
                if (Element.Width != Width || Element.Height != Height) MarkPresentation();
                Element.Width = Width;
                Element.Height = Height;
                View? ParentView = Target.ParentId != 0 && Views.TryGetValue(Target.ParentId, out View? FoundParent)
                    ? FoundParent : null;
                double ParentX = ParentView?.BoundsX ?? 0;
                double ParentY = ParentView?.BoundsY ?? 0;
                if (Canvas.GetLeft(Element) != X - ParentX || Canvas.GetTop(Element) != Y - ParentY) MarkPresentation();
                Canvas.SetLeft(Element, X - ParentX);
                Canvas.SetTop(Element, Y - ParentY);
            }
            return 1;
        }
        catch (Exception Error) { return ReportBackendFailure("Arrange", Error); }
    }

    private int OnPresent(IntPtr Context, int Id, int HasClip, double X, double Y, double Width, double Height, int Rank, int InputVisible)
    {
        try {
            if (!Views.TryGetValue(Id, out View? Target)) return 0;
            Target.InputVisible = InputVisible != 0;
            if(Target.ClassName=="CodeEditor") PresentCodeEditor(Id,Target,HasClip,X,Y,Width,Height);
            if (!Target.InputVisible) {ClearInvalidFocus(); CancelCapturedPointers(Target);}
            UIElement? Element = Target.Element ?? Target.Container;
            if (Element is not null) {
                var Clip = new Windows.Foundation.Rect(X, Y, Width, Height);
                if (HasClip == 0) {if (Element.Clip is not null) {MarkPresentation(); Element.Clip = null;}}
                else if (Element.Clip is null || Element.Clip.Rect != Clip) {MarkPresentation(); Element.Clip = new RectangleGeometry {Rect = Clip};}
                if (Canvas.GetZIndex(Element) != Rank) {MarkPresentation(); Canvas.SetZIndex(Element, Rank);}
                if (Element is Control Control && Control.IsEnabled != (Target.Enabled && Target.InputVisible)) {MarkPresentation(); Control.IsEnabled = Target.Enabled && Target.InputVisible;}
            }
            ClearInvalidFocus(); return 1;
        } catch (Exception Error) {return ReportBackendFailure("Present", Error);}
    }

    private void MarkPresentation() {if (PresentationAt == 0) PresentationAt = Stopwatch.GetTimestamp();}
    private void OnRendering(object? Sender, object Args)
    {
        RecordFrameRendering();
        if (!IsDisposed && !IsDisposing && PresentationAt != 0) {
            double Elapsed = Stopwatch.GetElapsedTime(PresentationAt).TotalMilliseconds;
            Native.Lui_RecordPerformance(Runtime, 2, Elapsed);
            PresentationAt = 0;
        }
    }
    private void ActivateMeasured(int Id)
    {
        long Started = Stopwatch.GetTimestamp(); Native.Lui_Activate(Runtime, Id);
        Native.Lui_RecordPerformance(Runtime, 1, Stopwatch.GetElapsedTime(Started).TotalMilliseconds);
    }

    private int OnDestroy(IntPtr Context, int Id)
    {
        Diagnostic($"Destroy {Id}");
        try
        {
            if (Views.TryGetValue(Id, out View? Existing)) CancelCapturedPointers(Existing);
            if (!Views.Remove(Id, out View? Target)) return 1;
            Target.IsAlive = false;
            if(Target.Window is not null) DetachCodeEditors(Id);
            if(Target.CodeHost!=IntPtr.Zero) {
                CodeEditorHost.LuiWinUITextDestroyHostV1(Target.CodeHost);
                Target.CodeHost=IntPtr.Zero;
            }
            RetireWindowCloseHook(Target);
            // Runtime parenting is batched, destruction is immediate. A live
            // child may already belong to another runtime parent while its
            // native parent change is still queued. Detach it before retiring
            // this container so the later native Add has no previous parent.
            foreach (var Child in Views.Values) if (Child.ParentId == Id && Child.Element is not null) {
                Target.Container?.Children.Remove(Child.Element);
                Child.ParentId = 0;
            }
            ReleaseImage(Target);
            if (FocusedViewId == Id) {FocusedViewId = 0; ++FocusGeneration;}
            if (Target.ParentId != 0 && Target.Element is not null && Views.TryGetValue(Target.ParentId, out View? Parent))
                Parent.Container?.Children.Remove(Target.Element);
            if (!Target.NativeClosed) Target.Window?.Close();
            if (Target.Window is not null) QueueLastWindowExit(Target);
            return 1;
        }
        catch (Exception Error) { return ReportBackendFailure("Destroy", Error); }
    }

    private static partial class Native
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct CapabilityDeclaration
        {
            internal uint StructSize;
            internal uint AbiVersion;
            internal ulong GrantedCapabilities;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct SandboxLimits
        {
            internal uint StructSize;
            internal uint AbiVersion;
            internal ulong MaxMemoryBytes;
            internal ulong MaxInterrupts;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct NetworkPolicy
        {
            internal uint StructSize;
            internal uint AbiVersion;
            internal uint Flags;
            internal uint ClientPortMin;
            internal uint ClientPortMax;
            internal uint ServerPortMin;
            internal uint ServerPortMax;
        }
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int CreateCallback(IntPtr Context, int Id, [MarshalAs(UnmanagedType.LPUTF8Str)] string ClassName);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int RequestFocusCallback(IntPtr Context, int Id);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int PropertyCallback(IntPtr Context, int Id, [MarshalAs(UnmanagedType.LPUTF8Str)] string Name, [MarshalAs(UnmanagedType.LPUTF8Str)] string Value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ParentCallback(IntPtr Context, int Id, int ParentId);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ArrangeCallback(IntPtr Context, int Id, double X, double Y, double Width, double Height, int ResizeWindow);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int PresentCallback(IntPtr Context, int Id, int HasClip, double X, double Y, double Width, double Height, int Rank, int InputVisible);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int LoadImageCallback(IntPtr Context, int Id, ulong Token, [MarshalAs(UnmanagedType.LPUTF8Str)] string Source);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void CancelImageCallback(IntPtr Context, ulong Token);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int DestroyCallback(IntPtr Context, int Id);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void LogCallback(IntPtr Context, [MarshalAs(UnmanagedType.LPUTF8Str)] string Level, [MarshalAs(UnmanagedType.LPUTF8Str)] string Message);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int WriteClipboardCallback(IntPtr Context,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string Text);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int StartPlatformRequestCallback(IntPtr Context, ulong RequestId);

        [StructLayout(LayoutKind.Sequential)]
        internal struct PlatformCallbacks
        {
            internal uint StructSize;
            internal uint AbiVersion;
            internal IntPtr Context;
            internal WriteClipboardCallback WriteClipboardText;
            internal StartPlatformRequestCallback ReadClipboardText;
            internal StartPlatformRequestCallback OpenFile;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct BackendCallbacks
        {
            internal IntPtr Context;
            internal CreateCallback Create;
            internal PropertyCallback Property;
            internal ParentCallback Parent;
            internal ArrangeCallback Arrange;
            internal DestroyCallback Destroy;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct FocusCallbacks
        {
            internal uint StructSize;
            internal uint AbiVersion;
            internal IntPtr Context;
            internal RequestFocusCallback RequestFocus;
            internal ulong Reserved;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct PresentationCallbacks {
            internal uint StructSize, AbiVersion;
            internal IntPtr Context;
            internal PresentCallback Present;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct ImageCallbacks {
            internal uint StructSize, AbiVersion;
            internal IntPtr Context;
            internal LoadImageCallback Load;
            internal CancelImageCallback Cancel;
        }

        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Lui_Create();
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Lui_Destroy(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_DeclareCapabilities(IntPtr Runtime, ref CapabilityDeclaration Declaration);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint Lui_InstallWindowsDesktopV1(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_ConfigureSandbox(IntPtr Runtime, ref SandboxLimits Limits);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_SetNetworkPolicy(IntPtr Runtime, ref NetworkPolicy Policy);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_RegisterAsset(IntPtr Runtime,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string Name);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_LoadExtension(IntPtr Runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string Path);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Lui_SetBackend(IntPtr Runtime, BackendCallbacks Callbacks);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_SetFocusCallbacks(IntPtr Runtime, ref FocusCallbacks Callbacks);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_SetPresentationCallbacks(IntPtr Runtime, ref PresentationCallbacks Callbacks);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_SetImageCallbacks(IntPtr Runtime, ref ImageCallbacks Callbacks);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_ImageCompleted(IntPtr Runtime, int Id, ulong Token, int Success);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_RecordPerformance(IntPtr Runtime, int Kind, double Milliseconds);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Lui_GetPerformanceJson(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Lui_GetPreviewTreeJson(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_ScrollInput(IntPtr Runtime, int OriginId, double DeltaX, double DeltaY);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_TextInput(IntPtr Runtime, int Id, [MarshalAs(UnmanagedType.LPUTF8Str)] string Text, int SelectionStart, int SelectionLength);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_TextComposition(IntPtr Runtime, int Id, int Phase, [MarshalAs(UnmanagedType.LPUTF8Str)] string Preedit);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_SetPlatformCallbacks(IntPtr Runtime, ref PlatformCallbacks Callbacks);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_CompletePlatformRequest(IntPtr Runtime, ulong RequestId,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? Result,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? Error);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Lui_ReportBackendError(IntPtr Runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string Message);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Lui_SetBackendName(IntPtr Runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string Name);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Lui_SetLogCallback(IntPtr Runtime, IntPtr Context, LogCallback Callback);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_RunScript(IntPtr Runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string Source, [MarshalAs(UnmanagedType.LPUTF8Str)] string ChunkName);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_Activate(IntPtr Runtime, int Id);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_TextChanged(IntPtr Runtime, int Id, [MarshalAs(UnmanagedType.LPUTF8Str)] string Text);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_CheckedChanged(IntPtr Runtime, int Id, int Checked);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_ValueChanged(IntPtr Runtime, int Id, double Value);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_WindowResized(IntPtr Runtime, int Id, double Width, double Height);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_FocusChanged(IntPtr Runtime, int Id, int Focused);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_KeyInput(IntPtr Runtime, int Id, int Phase,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string Key);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_WindowShortcut(IntPtr Runtime, int WindowId, int OriginId,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string Key, uint Modifiers, int Repeat);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_HoverChanged(IntPtr Runtime, int Id, int Hovered);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_PointerInput(IntPtr Runtime, int Id, int Phase, int Device,
            uint PointerId, double X, double Y);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_PointerInputV2(IntPtr Runtime, int Id, int Phase, int Device,
            uint PointerId, double X, double Y, int Button);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_Pump(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_SystemThemeChanged(IntPtr Runtime,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string Theme);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Lui_GetLastError(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_GetCodeEditorTextRevision(IntPtr Runtime,int Id,out ulong Revision);
    }
}
