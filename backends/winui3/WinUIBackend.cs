using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Lui.WinUI;

internal sealed class WinUIBackend
{
    private sealed class View
    {
        public Microsoft.UI.Xaml.Window? Window;
        public UIElement? Element;
        public Canvas? Container;
        public int ParentId;
    }

    private readonly Dictionary<int, View> Views = new();
    private readonly Native.CreateCallback CreateCallback;
    private readonly Native.PropertyCallback PropertyCallback;
    private readonly Native.ParentCallback ParentCallback;
    private readonly Native.ArrangeCallback ArrangeCallback;
    private readonly Native.DestroyCallback DestroyCallback;
    private readonly IntPtr Runtime;

    public string LastError => Marshal.PtrToStringUTF8(Native.Lui_GetLastError(Runtime)) ?? "unknown error";

    public WinUIBackend()
    {
        CreateCallback = OnCreate;
        PropertyCallback = OnProperty;
        ParentCallback = OnParent;
        ArrangeCallback = OnArrange;
        DestroyCallback = OnDestroy;
        Runtime = Native.Lui_Create();
        if (Runtime == IntPtr.Zero) throw new InvalidOperationException("Could not initialize Luau");
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
            Trace.TraceError("[LUI:WinUI] {0}", Error);
            return false;
        }
    }

    public void Pump() => Native.Lui_Pump(Runtime);

    private void OnCreate(IntPtr Context, int Id, string ClassName)
    {
        try
        {
            View NewView = new();
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
                    Button.Click += (_, _) => Native.Lui_Activate(Runtime, Id);
                    NewView.Element = Button;
                    break;
            }
            Views.Add(Id, NewView);
        }
        catch (Exception Error) { Trace.TraceError("[LUI:WinUI] Create: {0}", Error); }
    }

    private void OnProperty(IntPtr Context, int Id, string Name, string Value)
    {
        try
        {
            if (!Views.TryGetValue(Id, out View? Target)) return;
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
                case "Visible" when Target.Window is not null:
                    if (Value == "true") Target.Window.Activate();
                    else Target.Window.AppWindow.Hide();
                    break;
                case "Visible" when Target.Element is not null:
                    Target.Element.Visibility = Value == "true" ? Visibility.Visible : Visibility.Collapsed;
                    break;
            }
        }
        catch (Exception Error) { Trace.TraceError("[LUI:WinUI] Property: {0}", Error); }
    }

    private void OnParent(IntPtr Context, int Id, int ParentId)
    {
        try
        {
            View Child = Views[Id];
            if (Child.Element is null) return;
            if (Child.ParentId != 0 && Views.TryGetValue(Child.ParentId, out View? OldParent))
                OldParent.Container?.Children.Remove(Child.Element);
            Child.ParentId = ParentId;
            if (ParentId != 0) Views[ParentId].Container?.Children.Add(Child.Element);
        }
        catch (Exception Error) { Trace.TraceError("[LUI:WinUI] Parent: {0}", Error); }
    }

    private void OnArrange(IntPtr Context, int Id, double X, double Y, double Width, double Height)
    {
        try
        {
            if (!Views.TryGetValue(Id, out View? Target)) return;
            if (Target.Window is not null)
            {
                Target.Window.AppWindow.Resize(new Windows.Graphics.SizeInt32(
                    Math.Max(1, (int)Math.Round(Width)), Math.Max(1, (int)Math.Round(Height))));
            }
            else if (Target.Element is FrameworkElement Element)
            {
                Element.Width = Width;
                Element.Height = Height;
                Canvas.SetLeft(Element, X);
                Canvas.SetTop(Element, Y);
            }
        }
        catch (Exception Error) { Trace.TraceError("[LUI:WinUI] Arrange: {0}", Error); }
    }

    private void OnDestroy(IntPtr Context, int Id)
    {
        try
        {
            if (!Views.Remove(Id, out View? Target)) return;
            if (Target.ParentId != 0 && Target.Element is not null && Views.TryGetValue(Target.ParentId, out View? Parent))
                Parent.Container?.Children.Remove(Target.Element);
            Target.Window?.Close();
        }
        catch (Exception Error) { Trace.TraceError("[LUI:WinUI] Destroy: {0}", Error); }
    }

    private static class Native
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void CreateCallback(IntPtr Context, int Id, [MarshalAs(UnmanagedType.LPUTF8Str)] string ClassName);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void PropertyCallback(IntPtr Context, int Id, [MarshalAs(UnmanagedType.LPUTF8Str)] string Name, [MarshalAs(UnmanagedType.LPUTF8Str)] string Value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ParentCallback(IntPtr Context, int Id, int ParentId);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ArrangeCallback(IntPtr Context, int Id, double X, double Y, double Width, double Height);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void DestroyCallback(IntPtr Context, int Id);

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
        internal static extern int Lui_RunScript(IntPtr Runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string Source, [MarshalAs(UnmanagedType.LPUTF8Str)] string ChunkName);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_Activate(IntPtr Runtime, int Id);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Lui_Pump(IntPtr Runtime);
        [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Lui_GetLastError(IntPtr Runtime);
    }
}
