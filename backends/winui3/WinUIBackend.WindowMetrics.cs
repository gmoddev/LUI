using System.Runtime.InteropServices;
using Windows.Graphics;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    // Trusted desktop hosts opt in after granting HostServices. Only windows owned by this runtime resolve.
    private DesktopMetrics? WindowMetrics;

    internal void StartWindowMetrics()
    {
        WindowMetrics ??= new DesktopMetrics(Runtime);
    }

    private sealed class DesktopMetrics
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct Value
        {
            public uint Size, Type;
            public double Number;
            public nint Text;
            public ulong TextLength;
            public int Boolean;
            public uint Reserved;
        }

        [StructLayout(LayoutKind.Sequential)] private struct Point { public int X, Y; }
        [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)] private struct MonitorInfo { public int Size; public Rect Monitor, Work; public int Flags; }
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int Method(nint Context, nint Args, uint Count, out Value Result);
        [DllImport("LuiRuntime", CallingConvention=CallingConvention.Cdecl)]
        private static extern int Lui_RegisterHostMethod(nint Runtime, string Service, string Name, string Type, Method Callback, nint Context);
        [DllImport("user32")] private static extern bool ClientToScreen(nint Window, ref Point Position);
        [DllImport("user32")] private static extern uint GetDpiForWindow(nint Window);
        [DllImport("user32")] private static extern nint WindowFromPoint(Point Position);
        [DllImport("user32")] private static extern nint GetAncestor(nint Window, uint Mode);
        [DllImport("user32")] private static extern bool IsIconic(nint Window);
        [DllImport("user32")] private static extern nint MonitorFromRect(ref Rect Area, uint Flags);
        [DllImport("user32", EntryPoint="GetMonitorInfoW")] private static extern bool GetMonitorInfo(nint Monitor, ref MonitorInfo Info);
        private readonly Dictionary<int, (string Name, View Target)> Windows = new();
        private readonly Dictionary<int, (nint Handle, nint Owner)> OwnedHandles = new();
        [DllImport("user32")] private static extern nint GetWindow(nint Handle, uint Which);
        [DllImport("user32", EntryPoint="SetWindowLongPtrW")] private static extern nint SetWindowLongPtr(nint Handle, int Index, nint Value);
        private readonly Method ReadCallback, PlaceCallback, AtCallback, OwnCallback;

        public DesktopMetrics(nint Runtime)
        {
            ReadCallback = Read;
            PlaceCallback = Place;
            AtCallback = At;
            OwnCallback = Own;
            void Register(string Name, string Signature, Method Call)
            {
                if (Lui_RegisterHostMethod(Runtime, "DesktopWindowMetrics", Name, Signature, Call, 0) == 0)
                    throw new InvalidOperationException("[LUI:WindowMetrics] Registration failed: " + Name);
            }
            Register("Read", "(Self: DesktopWindowMetrics, Name: string, Field: number) -> number", ReadCallback);
            Register("Place", "(Self: DesktopWindowMetrics, Name: string, X: number, Y: number, Width: number, Height: number) -> number", PlaceCallback);
            Register("Own", "(Self: DesktopWindowMetrics, Name: string, Owner: string) -> number", OwnCallback);
            Register("At", "(Self: DesktopWindowMetrics, X: number, Y: number) -> number", AtCallback);
        }

        public void Name(int Id, View Target, string Name)
        {
            if (Name.Length is > 0 and <= 128) Windows[Id] = (Name, Target);
            else Windows.Remove(Id);
        }
        public void Remove(int Id)
        {
            Windows.Remove(Id);
            OwnedHandles.Remove(Id);
        }

        public void ReleaseOwners()
        {
            // Win32 destroys owned HWNDs when their owner closes. Runtime disposal
            // closes each XAML Window itself, in runtime instance order.
            foreach (var Entry in OwnedHandles.Values)
                if (GetWindow(Entry.Handle, 4) == Entry.Owner)
                    SetWindowLongPtr(Entry.Handle, -8, 0);
            OwnedHandles.Clear();
        }

        private static readonly int ValueSize = Marshal.SizeOf<Value>();
        private static Value Argument(nint Args, int Index) => Marshal.PtrToStructure<Value>(Args + Index * ValueSize);
        private static Value Result(double Number = 0) => new() { Size = (uint)ValueSize, Type = 2, Number = Number };

        private static bool Integer(nint Args, int Index, out int Number)
        {
            Value Raw = Argument(Args, Index);
            Number = 0;
            if (Raw.Type != 2 || !double.IsFinite(Raw.Number) || Math.Abs(Raw.Number) > 1_000_000) return false;
            Number = (int)Math.Round(Raw.Number);
            return true;
        }
        private (int Id, View? Target) Find(nint Args, int Index = 0)
        {
            Value Raw = Argument(Args, Index);
            if (Raw.Type != 3 || Raw.Text == 0 || Raw.TextLength is 0 or > 512) return default;
            string? Name = Marshal.PtrToStringUTF8(Raw.Text, (int)Raw.TextLength);
            (int Id, View? Target) Match = default;
            foreach (var Entry in Windows)
            {
                var (Key, Target) = Entry.Value;
                if (Key != Name || !Target.IsAlive || Target.NativeClosed || Target.Window == null) continue;
                if (Match.Target != null) return default;
                Match = (Entry.Key, Target);
            }
            return Match;
        }
        private int Read(nint Context, nint Args, uint Count, out Value Output)
        {
            Output = Result();
            if (Count != 2 || !Integer(Args, 1, out int Field)) return 0;
            try
            {
                var (Id, Target) = Find(Args);
                if (Target?.Window is not {} Window) return 1;
                var Handle = WinRT.Interop.WindowNative.GetWindowHandle(Window);
                if (IsIconic(Handle)) return 1;
                var App = Window.AppWindow;
                Point Origin = default;
                if (Field is 6 or 7 && !ClientToScreen(Handle, ref Origin)) return 1;
                Output = Result(Field switch {
                    0 => Id, 1 => App.Position.X, 2 => App.Position.Y,
                    3 => App.Size.Width, 4 => App.Size.Height,
                    5 => Window.Content?.XamlRoot?.RasterizationScale ?? GetDpiForWindow(Handle) / 96.0,
                    6 => Origin.X, 7 => Origin.Y, _ => 0
                });
                return 1;
            }
            catch (Exception Error) when (Error is COMException or InvalidOperationException) { return 1; }
        }
        private int Own(nint Context, nint Args, uint Count, out Value Output)
        {
            Output = Result();
            if (Count != 2) return 0;
            try
            {
                var ChildEntry = Find(Args);
                if (ChildEntry.Target?.Window is not {} Child || Find(Args, 1).Target?.Window is not {} Owner) return 1;
                nint Handle = WinRT.Interop.WindowNative.GetWindowHandle(Child);
                nint Parent = WinRT.Interop.WindowNative.GetWindowHandle(Owner);
                if (Handle == 0 || Parent == 0) return 1;
                for (nint Current = Parent; Current != 0; Current = GetWindow(Current, 4))
                    if (Current == Handle) return 1;
                SetWindowLongPtr(Handle, -8, Parent);
                if (GetWindow(Handle, 4) == Parent)
                {
                    OwnedHandles[ChildEntry.Id] = (Handle, Parent);
                    Output = Result(1);
                }
                return 1;
            }
            catch (Exception Error) when (Error is COMException or InvalidOperationException) { return 1; }
        }
        private int At(nint Context, nint Args, uint Count, out Value Output)
        {
            Output = Result();
            if (Count != 2 || !Integer(Args, 0, out int X) || !Integer(Args, 1, out int Y)) return 0;
            var Root = GetAncestor(WindowFromPoint(new Point { X = X, Y = Y }), 2);
            if (Root == 0) return 1;
            try
            {
                foreach (var Entry in Windows)
                    if (Entry.Value.Target is { IsAlive: true, NativeClosed: false, Window: {} Window } &&
                        WinRT.Interop.WindowNative.GetWindowHandle(Window) == Root) { Output = Result(Entry.Key); break; }
                return 1;
            }
            catch (Exception Error) when (Error is COMException or InvalidOperationException) { return 1; }
        }
        private int Place(nint Context, nint Args, uint Count, out Value Output)
        {
            Output = Result();
            if (Count != 5 || !Integer(Args, 1, out int X) || !Integer(Args, 2, out int Y) ||
                !Integer(Args, 3, out int W) || !Integer(Args, 4, out int H) || W < 1 || H < 1) return 0;
            try
            {
                if (Find(Args).Target?.Window is not {} Window) return 1;
                var Requested = new Rect { Left=X, Top=Y, Right=X+W, Bottom=Y+H };
                var Info = new MonitorInfo { Size = Marshal.SizeOf<MonitorInfo>() };
                if (!GetMonitorInfo(MonitorFromRect(ref Requested, 2), ref Info)) return 1;
                Rect Area = Info.Work;
                W = Math.Min(W, Area.Right - Area.Left); H = Math.Min(H, Area.Bottom - Area.Top);
                if (W < 1 || H < 1) return 1;
                Window.AppWindow.MoveAndResize(new RectInt32(Math.Clamp(X,Area.Left,Area.Right-W),
                    Math.Clamp(Y,Area.Top,Area.Bottom-H), W,H));
                Output = Result(1);
                return 1;
            }
            catch (Exception Error) when (Error is COMException or InvalidOperationException) { return 1; }
        }
    }
}
