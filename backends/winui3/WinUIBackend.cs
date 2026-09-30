using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace Lui.WinUI;

internal sealed class WinUIBackend
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
        public int ParentId;
        public double BoundsX;
        public double BoundsY;
        public bool ApplyingProperty;
    }

    private readonly Dictionary<int, View> Views = new();
    private readonly Native.CreateCallback CreateCallback;
    private readonly Native.PropertyCallback PropertyCallback;
    private readonly Native.ParentCallback ParentCallback;
    private readonly Native.ArrangeCallback ArrangeCallback;
    private readonly Native.DestroyCallback DestroyCallback;
    private readonly Native.LogCallback LogCallback;
    private readonly IntPtr Runtime;
    private string LastReportedError = string.Empty;

    public string LastError => Marshal.PtrToStringUTF8(Native.Lui_GetLastError(Runtime)) ?? "unknown error";

    public WinUIBackend()
    {
        CreateCallback = OnCreate;
        PropertyCallback = OnProperty;
        ParentCallback = OnParent;
        ArrangeCallback = OnArrange;
        DestroyCallback = OnDestroy;
        LogCallback = OnNativeLog;
        Runtime = Native.Lui_Create();
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

    public void Pump()
    {
        int Completed = Native.Lui_Pump(Runtime);
        if (Completed > 0) LuiDiagnostics.Log("Scheduler", $"Completed {Completed} callback(s)");
        string Error = LastError;
        if (!string.IsNullOrEmpty(Error) && Error != LastReportedError)
        {
            LastReportedError = Error;
            LuiDiagnostics.Error("Runtime", Error);
        }
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
        View NewView = new();
        try
        {
            switch (ClassName)
            {
                case "Window":
                    NewView.Window = new Microsoft.UI.Xaml.Window();
                    NewView.Container = new Canvas();
                    NewView.Window.Content = NewView.Container;
                    break;
                case "Frame":
                    NewView.Container = new Canvas();
                    NewView.Element = NewView.Container;
                    break;
                case "TextLabel":
                    NewView.Element = new TextBlock { TextWrapping = TextWrapping.Wrap };
                    break;
                case "TextButton":
                    Button Button = new();
                    Button.Click += (_, _) => { Diagnostic($"Click {Id}"); Native.Lui_Activate(Runtime, Id); };
                    NewView.Element = Button;
                    break;
                case "TextBox":
                    TextBox Input = new();
                    Input.TextChanged += (_, _) => {
                        if (!NewView.ApplyingProperty) { Diagnostic($"TextChanged {Id}"); Native.Lui_TextChanged(Runtime, Id, Input.Text); }
                    };
                    NewView.Element = Input;
                    break;
                case "CheckBox":
                    CheckBox Check = new();
                    Check.Checked += (_, _) => {
                        if (!NewView.ApplyingProperty) { Diagnostic($"Checked {Id}"); Native.Lui_CheckedChanged(Runtime, Id, 1); }
                    };
                    Check.Unchecked += (_, _) => {
                        if (!NewView.ApplyingProperty) { Diagnostic($"Unchecked {Id}"); Native.Lui_CheckedChanged(Runtime, Id, 0); }
                    };
                    Check.Click += (_, _) => { Diagnostic($"Click {Id}"); Native.Lui_Activate(Runtime, Id); };
                    NewView.Element = Check;
                    break;
                case "Slider":
                    Slider Slider = new();
                    Slider.ValueChanged += (_, Args) => {
                        if (!NewView.ApplyingProperty) { Diagnostic($"ValueChanged {Id} {Args.NewValue}"); Native.Lui_ValueChanged(Runtime, Id, Args.NewValue); }
                    };
                    NewView.Element = Slider;
                    break;
                case "ProgressBar":
                    NewView.Element = new ProgressBar();
                    break;
                default:
                    throw new NotSupportedException($"No native control for {ClassName}");
            }
            if (NewView.Element is not null)
            {
                NewView.Element.GotFocus += (_, _) => { Diagnostic($"GotFocus {Id}"); Native.Lui_FocusChanged(Runtime, Id, 1); };
                NewView.Element.LostFocus += (_, _) => { Diagnostic($"LostFocus {Id}"); Native.Lui_FocusChanged(Runtime, Id, 0); };
                NewView.Element.PointerEntered += (_, _) => { Diagnostic($"PointerEntered {Id}"); Native.Lui_HoverChanged(Runtime, Id, 1); };
                NewView.Element.PointerExited += (_, Args) => {
                    Diagnostic($"PointerExited {Id}");
                    Native.Lui_HoverChanged(Runtime, Id, 0);
                    OnPointerInput(Id, NewView, 3, Args);
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
                    new PointerEventHandler((_, Args) => OnPointerInput(Id, NewView, 3, Args)), true);
            }
            Views.Add(Id, NewView);
            Diagnostic($"Create end {Id} {ClassName}");
            return 1;
        }
        catch (Exception Error)
        {
            try { NewView.Window?.Close(); }
            catch (Exception CleanupError) { LuiDiagnostics.Error("WinUI", "Create cleanup: " + CleanupError); }
            return ReportBackendFailure("Create", Error);
        }
    }

    private void OnPointerInput(int Id, View Target, int Phase, PointerRoutedEventArgs Args)
    {
        try
        {
            if (Target.Element is null) return;
            int Device = Args.Pointer.PointerDeviceType switch
            {
                Microsoft.UI.Input.PointerDeviceType.Mouse => 0,
                Microsoft.UI.Input.PointerDeviceType.Pen => 1,
                Microsoft.UI.Input.PointerDeviceType.Touch => 2,
                Microsoft.UI.Input.PointerDeviceType.Touchpad => 3,
                _ => -1,
            };
            if (Device < 0) return;
            if (Phase == 3)
            {
                Diagnostic($"PointerCanceled {Id} {Args.Pointer.PointerId}");
                Native.Lui_PointerInput(Runtime, Id, Phase, Device, Args.Pointer.PointerId, 0, 0);
                return;
            }
            var Point = Args.GetCurrentPoint(Target.Element);
            Diagnostic($"PointerInput {Id} {Phase} {Device} {Point.PointerId} {Point.Position.X} {Point.Position.Y}");
            Native.Lui_PointerInput(Runtime, Id, Phase, Device, Point.PointerId, Point.Position.X, Point.Position.Y);
        }
        catch (Exception Error) { LuiDiagnostics.Error("Input", "Pointer: " + Error); }
    }

    private int OnProperty(IntPtr Context, int Id, string Name, string Value)
    {
        Diagnostic($"Property begin {Id} {Name} {Value}");
        try
        {
            if (!Views.TryGetValue(Id, out View? Target))
                return ReportBackendFailure("Property", new InvalidOperationException($"Missing view {Id}"));
            Target.ApplyingProperty = true;
            try
            {
                switch (Name)
                {
                    case "Title" when Target.Window is not null:
                        Target.Window.Title = Value;
                        break;
                    case "Text" when Target.Element is TextBlock Label:
                        Label.Text = Value;
                        break;
                    case "Text" when Target.Element is Button Button:
                        Button.Content = Value;
                        break;
                    case "Text" when Target.Element is TextBox Input:
                        Input.Text = Value;
                        break;
                    case "Text" when Target.Element is CheckBox Check:
                        Check.Content = Value;
                        break;
                    case "Checked" when Target.Element is CheckBox Check:
                        Check.IsChecked = Value == "true";
                        break;
                    case "Enabled" when Target.Element is Control Control:
                        Control.IsEnabled = Value == "true";
                        break;
                    case "Minimum" or "Maximum" or "Value" when Target.Element is Slider Slider:
                        SetRange(Slider, Name, Value);
                        break;
                    case "Minimum" or "Maximum" or "Value" when Target.Element is ProgressBar Progress:
                        SetRange(Progress, Name, Value);
                        break;
                    case "Visible" when Target.Window is not null:
                        if (Value == "true") Target.Window.Activate();
                        else Target.Window.AppWindow.Hide();
                        break;
                    case "Visible" when Target.Element is not null:
                        Target.Element.Visibility = Value == "true" ? Visibility.Visible : Visibility.Collapsed;
                        break;
                }
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

    private int OnParent(IntPtr Context, int Id, int ParentId)
    {
        Diagnostic($"Parent {Id} {ParentId}");
        try
        {
            View Child = Views[Id];
            if (Child.Element is null) return 1;
            if (Child.ParentId != 0 && Views.TryGetValue(Child.ParentId, out View? OldParent))
                OldParent.Container?.Children.Remove(Child.Element);
            Child.ParentId = ParentId;
            if (ParentId != 0)
            {
                Canvas Container = Views[ParentId].Container
                    ?? throw new InvalidOperationException($"Parent {ParentId} has no native container");
                Container.Children.Add(Child.Element);
            }
            return 1;
        }
        catch (Exception Error) { return ReportBackendFailure("Parent", Error); }
    }

    private int OnArrange(IntPtr Context, int Id, double X, double Y, double Width, double Height)
    {
        Diagnostic($"Arrange {Id} {X} {Y} {Width} {Height}");
        try
        {
            if (!Views.TryGetValue(Id, out View? Target))
                return ReportBackendFailure("Arrange", new InvalidOperationException($"Missing view {Id}"));
            Target.BoundsX = X;
            Target.BoundsY = Y;
            if (Target.Window is not null)
            {
                Target.Window.AppWindow.Resize(new Windows.Graphics.SizeInt32(
                    Math.Max(1, (int)Math.Round(Width)), Math.Max(1, (int)Math.Round(Height))));
            }
            else if (Target.Element is FrameworkElement Element)
            {
                Element.Width = Width;
                Element.Height = Height;
                View? ParentView = Target.ParentId != 0 && Views.TryGetValue(Target.ParentId, out View? FoundParent)
                    ? FoundParent : null;
                double ParentX = ParentView?.BoundsX ?? 0;
                double ParentY = ParentView?.BoundsY ?? 0;
                Canvas.SetLeft(Element, X - ParentX);
                Canvas.SetTop(Element, Y - ParentY);
            }
            return 1;
        }
        catch (Exception Error) { return ReportBackendFailure("Arrange", Error); }
    }

    private int OnDestroy(IntPtr Context, int Id)
    {
        Diagnostic($"Destroy {Id}");
        try
        {
            if (!Views.Remove(Id, out View? Target)) return 1;
            if (Target.ParentId != 0 && Target.Element is not null && Views.TryGetValue(Target.ParentId, out View? Parent))
                Parent.Container?.Children.Remove(Target.Element);
            Target.Window?.Close();
            return 1;
        }
        catch (Exception Error) { return ReportBackendFailure("Destroy", Error); }
    }

    private static class Native
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int CreateCallback(IntPtr Context, int Id, [MarshalAs(UnmanagedType.LPUTF8Str)] string ClassName);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int PropertyCallback(IntPtr Context, int Id, [MarshalAs(UnmanagedType.LPUTF8Str)] string Name, [MarshalAs(UnmanagedType.LPUTF8Str)] string Value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ParentCallback(IntPtr Context, int Id, int ParentId);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ArrangeCallback(IntPtr Context, int Id, double X, double Y, double Width, double Height);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int DestroyCallback(IntPtr Context, int Id);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void LogCallback(IntPtr Context, [MarshalAs(UnmanagedType.LPUTF8Str)] string Level, [MarshalAs(UnmanagedType.LPUTF8Str)] string Message);

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

        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Lui_Create();
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Lui_SetBackend(IntPtr Runtime, BackendCallbacks Callbacks);
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
        internal static extern int Lui_FocusChanged(IntPtr Runtime, int Id, int Focused);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_HoverChanged(IntPtr Runtime, int Id, int Hovered);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_PointerInput(IntPtr Runtime, int Id, int Phase, int Device,
            uint PointerId, double X, double Y);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_Pump(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Lui_GetLastError(IntPtr Runtime);
    }
}
