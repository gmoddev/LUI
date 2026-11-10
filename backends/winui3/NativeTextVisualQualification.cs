using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Lui.WinUI;

// Directly inspect this owned WinUI window with Computer Use screenshots.
// A suspended-Undo color is visible beside the failed temporary mode.
internal static class NativeTextVisualQualificationRunner
{
    [StructLayout(LayoutKind.Sequential)]
    private struct ColorSpan { internal int Start,End; internal uint Color; }
    [DllImport("LuiWinUIText.dll",CallingConvention=CallingConvention.Cdecl)]
    private static extern int LuiWinUITextApplyCandidateColorsV1(IntPtr Editor,ref ColorSpan Spans,uint Count);
    [DllImport("LuiWinUIText.dll",CallingConvention=CallingConvention.Cdecl)]
    private static extern int LuiWinUITextSetFixtureBaseColorV1(IntPtr Editor,uint Color);
    [DllImport("LuiWinUIText.dll",CallingConvention=CallingConvention.Cdecl)]
    private static extern int LuiWinUITextGetFixtureColorV1(IntPtr Editor,int Start,int End,out int Color);
    [DllImport("LuiWinUIText.dll",CallingConvention=CallingConvention.Cdecl)]
    private static extern IntPtr LuiWinUITextCreatePopupHostV1(IntPtr Owner,CodeEditorHost.EditorEvent Callback,IntPtr Context);
    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect {internal int Left,Top,Right,Bottom;}
    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint {internal int X,Y;}
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetWindowRect(IntPtr Window,out NativeRect Rect);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ClientToScreen(IntPtr Window,ref NativePoint Point);
    [DllImport("user32.dll")]
    private static extern IntPtr GetDC(IntPtr Window);
    [DllImport("user32.dll")]
    private static extern int ReleaseDC(IntPtr Window,IntPtr Dc);
    [StructLayout(LayoutKind.Sequential)]
    private struct BitmapInfoHeader {
        internal uint Size; internal int Width,Height; internal ushort Planes,BitCount;
        internal uint Compression,SizeImage; internal int XPelsPerMeter,YPelsPerMeter;
        internal uint ClrUsed,ClrImportant;
    }
    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateCompatibleDC(IntPtr Dc);
    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateDIBSection(IntPtr Dc,ref BitmapInfoHeader Info,uint Usage,
        out IntPtr Pixels,IntPtr Section,uint Offset);
    [DllImport("gdi32.dll")]
    private static extern IntPtr SelectObject(IntPtr Dc,IntPtr Object);
    [DllImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool BitBlt(IntPtr Dest,int X,int Y,int Width,int Height,
        IntPtr Source,int SourceX,int SourceY,uint Operation);
    [DllImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteDC(IntPtr Dc);
    [DllImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteObject(IntPtr Object);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr Window);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr Window);
    [DllImport("user32.dll")]
    private static extern IntPtr WindowFromPoint(NativePoint Point);
    private static (int Red,int Dark) ScreenPixels(IntPtr Host)
    {
        IntPtr Editor=CodeEditorHost.LuiWinUITextEditorV1(Host);
        if(!GetWindowRect(Editor,out NativeRect Rect)) return (-1,-1);
        int Width=Math.Clamp(Rect.Right-Rect.Left,0,1024),Height=Math.Clamp(Rect.Bottom-Rect.Top,0,512);
        if(Width==0 || Height==0) return (-1,-1);
        IntPtr Dc=GetDC(IntPtr.Zero);
        if(Dc==IntPtr.Zero) return (-1,-1);
        IntPtr Memory=IntPtr.Zero,Bitmap=IntPtr.Zero,Previous=IntPtr.Zero;
        int Red=0,Dark=0;
        try {
            Memory=CreateCompatibleDC(Dc);
            BitmapInfoHeader Info=new() {Size=(uint)Marshal.SizeOf<BitmapInfoHeader>(),Width=Width,
                Height=-Height,Planes=1,BitCount=32};
            Bitmap=CreateDIBSection(Dc,ref Info,0,out IntPtr Pixels,IntPtr.Zero,0);
            if(Memory==IntPtr.Zero || Bitmap==IntPtr.Zero || Pixels==IntPtr.Zero) return (-1,-1);
            Previous=SelectObject(Memory,Bitmap);
            if(!BitBlt(Memory,0,0,Width,Height,Dc,Rect.Left,Rect.Top,0x00CC0020)) return (-1,-1);
            byte[] Bytes=new byte[Width*Height*4];
            Marshal.Copy(Pixels,Bytes,0,Bytes.Length);
            for(int Y=5;Y<Height-5;Y+=2)
                for(int X=5;X<Width-5;X+=2) {
                    int Offset=(Y*Width+X)*4;
                    int B=Bytes[Offset],G=Bytes[Offset+1],R=Bytes[Offset+2];
                    if(R>140 && R>G*2 && R>B*2) ++Red;
                    if(R<130 && G<130 && B<130) ++Dark;
                }
        } finally {
            if(Previous!=IntPtr.Zero) SelectObject(Memory,Previous);
            if(Bitmap!=IntPtr.Zero) DeleteObject(Bitmap);
            if(Memory!=IntPtr.Zero) DeleteDC(Memory);
            ReleaseDC(IntPtr.Zero,Dc);
        }
        return (Red,Dark);
    }
    private static void Require(bool Value,string Code)
    {
        if(!Value) throw new InvalidOperationException("[LUI:CodeEditor] " + Code);
    }

    public static async Task<bool> RunAsync(string ReportPath)
    {
        Microsoft.UI.Xaml.Window? Window=null;
        List<IntPtr> Hosts=new();
        CodeEditorHost.EditorEvent Ignore=(_,_,_,_,_)=>{};
        string? Failure=null;
        int OwnerThread=Environment.CurrentManagedThreadId;
        string Source="COLOR COLOR COLOR    plain plain plain";
        (int Red,int Dark)[] StyledPixels=new (int,int)[4],ClearedPixels=new (int,int)[4];
        int[] StyledColors=new int[4],ClearedColors=new int[4];
        bool[] Visible=new bool[4],Hit=new bool[4];
        try
        {
            Window=new Microsoft.UI.Xaml.Window {Title="LUI native color visual: styled"};
            Grid Root=new() {Background=new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.Colors.White)};
            TaskCompletionSource<bool> Loaded=new(TaskCreationOptions.RunContinuationsAsynchronously);
            Root.Loaded+=(_,_)=>Loaded.TrySetResult(true);
            Window.Content=Root;
            Window.AppWindow.Resize(new Windows.Graphics.SizeInt32(600,550));
            Window.AppWindow.Show();
            Window.Activate();
            await Loaded.Task.WaitAsync(TimeSpan.FromSeconds(5));
            await Task.Delay(100);
            Window.Activate();
            IntPtr Parent=WinRT.Interop.WindowNative.GetWindowHandle(Window);
            Require(Parent!=IntPtr.Zero,"WindowHandle");
            NativePoint Origin=new();
            Require(ClientToScreen(Parent,ref Origin),"ClientOrigin");
            string[] Labels=["Plain native Rich Edit","Suspended Undo color",
                "Temporary format candidate","Permanent red positive control"];
            for(int Index=0;Index<4;++Index)
            {
                TextBlock Label=new() {Text=Labels[Index],FontSize=18,
                    Margin=new Thickness(20,20+Index*125,0,0),
                    HorizontalAlignment=HorizontalAlignment.Left,VerticalAlignment=VerticalAlignment.Top,
                    Foreground=new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.Colors.Black)};
                Root.Children.Add(Label);
                IntPtr Host=LuiWinUITextCreatePopupHostV1(Parent,Ignore,IntPtr.Zero);
                Require(Host!=IntPtr.Zero,"CreateHost"); Hosts.Add(Host);
                Require(CodeEditorHost.LuiWinUITextSetBoundsV1(Host,Origin.X+20,Origin.Y+52+Index*125,550,70)==1 &&
                    CodeEditorHost.LuiWinUITextSetTextV1(Host,Source)==1,"EditorText");
                Require(LuiWinUITextSetFixtureBaseColorV1(CodeEditorHost.LuiWinUITextEditorV1(Host),
                    Index==3?0x000000F0u:0u)==1,
                    "BlackBaseline");
            }
            await Task.Delay(300);
            for(int Index=0;Index<Hosts.Count;++Index) {
                Require(CodeEditorHost.LuiWinUITextSetBoundsV1(Hosts[Index],Origin.X+20,Origin.Y+52+Index*125,550,70)==1,
                    "RaiseHostAboveXaml");
                Require(CodeEditorHost.LuiWinUITextSetVisibleV1(Hosts[Index],1)==1,"ShowEditorHost");
                IntPtr Editor=CodeEditorHost.LuiWinUITextEditorV1(Hosts[Index]);
                Visible[Index]=IsWindowVisible(Editor);
                if(GetWindowRect(Editor,out NativeRect Rect))
                    Hit[Index]=WindowFromPoint(new NativePoint {X=(Rect.Left+Rect.Right)/2,Y=(Rect.Top+Rect.Bottom)/2})==Editor;
            }
            Require(CodeEditorHost.SetStyles(Hosts[1],"1;0,17,15728640"),"CurrentColor");
            ColorSpan Red=new() {Start=0,End=17,Color=0x000000F0};
            Require(LuiWinUITextApplyCandidateColorsV1(CodeEditorHost.LuiWinUITextEditorV1(Hosts[2]),ref Red,1)==1,
                "CandidateColor");
            Require(CodeEditorHost.ReadText(Hosts[0])==Source && CodeEditorHost.ReadText(Hosts[1])==Source &&
                CodeEditorHost.ReadText(Hosts[2])==Source,"ExactSourceAfterStyle");
            SetForegroundWindow(Parent);
            await Task.Delay(1000);
            Require(CodeEditorHost.SetStyles(Hosts[1],"1;0,17,15728640") &&
                LuiWinUITextSetFixtureBaseColorV1(CodeEditorHost.LuiWinUITextEditorV1(Hosts[3]),0x000000F0u)==1,
                "LateColorAfterActivation");
            await Task.Delay(4000);
            Require(Environment.CurrentManagedThreadId==OwnerThread,"StyledOwnerThread");
            for(int Index=0;Index<4;++Index) {
                StyledPixels[Index]=ScreenPixels(Hosts[Index]);
                Require(LuiWinUITextGetFixtureColorV1(CodeEditorHost.LuiWinUITextEditorV1(Hosts[Index]),0,5,
                    out StyledColors[Index])==1,"StyledFontColor");
            }
            Require(CodeEditorHost.SetStyles(Hosts[1],"") && CodeEditorHost.SetStyles(Hosts[2],""),"ClearColors");
            Window.Title="LUI native color visual: cleared";
            await Task.Delay(5000);
            for(int Index=0;Index<4;++Index) {
                ClearedPixels[Index]=ScreenPixels(Hosts[Index]);
                Require(LuiWinUITextGetFixtureColorV1(CodeEditorHost.LuiWinUITextEditorV1(Hosts[Index]),0,5,
                    out ClearedColors[Index])==1,"ClearedFontColor");
            }
            Require(StyledPixels[1].Red>=StyledPixels[0].Red+20 &&
                ClearedPixels[1].Red<=StyledPixels[0].Red+10 &&
                StyledPixels[3].Red>=StyledPixels[0].Red+20 &&
                StyledColors[1]==240 && ClearedColors[1]==-9999997,
                "VisibleSyntaxColorAndClear");
            Require(Environment.CurrentManagedThreadId==OwnerThread &&
                CodeEditorHost.ReadText(Hosts[1])==Source && CodeEditorHost.ReadText(Hosts[2])==Source,
                "ClearRetainsSource");
        }
        catch(Exception Error)
        {
            Failure=Error.GetType().Name+": "+Error.Message;
            LuiDiagnostics.Error("CodeEditor","Visual fixture: "+Error.GetType().Name);
        }
        finally
        {
            foreach(IntPtr Host in Hosts) CodeEditorHost.LuiWinUITextDestroyHostV1(Host);
            Window?.Close();
        }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
        await File.WriteAllTextAsync(ReportPath,JsonSerializer.Serialize(new
        {Passed=Failure is null,OwnerThread,Source,Visible,Hit,
            StyledPixels=StyledPixels.Select(Pair=>new {Pair.Red,Pair.Dark}).ToArray(),
            ClearedPixels=ClearedPixels.Select(Pair=>new {Pair.Red,Pair.Dark}).ToArray(),
            StyledColors,ClearedColors,Failure},
            new JsonSerializerOptions {WriteIndented=true}));
        return Failure is null;
    }
}
