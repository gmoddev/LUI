using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace Lui.WinUI;

// One system Rich Edit HWND owns text, selection, IME, scrolling and Undo.
// This private adapter carries only transient style ranges across the boundary.
internal static class CodeEditorHost
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void EditorEvent(IntPtr Context, int Kind, IntPtr Text, int Start, int Length);

    [StructLayout(LayoutKind.Sequential)]
    private struct ColorSpan { internal int Start, End; internal uint Color; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct ClipRect { internal int Left, Top, Right, Bottom; }

    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern IntPtr LuiWinUITextCreateHostV1(IntPtr Parent, EditorEvent Callback, IntPtr Context);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern IntPtr LuiWinUITextCreatePopupHostV1(IntPtr Owner, EditorEvent Callback, IntPtr Context);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern IntPtr LuiWinUITextEditorV1(IntPtr Host);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextDestroyHostV1(IntPtr Host);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetParentV1(IntPtr Host, IntPtr Parent);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetOwnerV1(IntPtr Host, IntPtr Owner);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)]
    internal static extern int LuiWinUITextSetTextV1(IntPtr Host, string Text);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetLimitV1(IntPtr Host, int MaxLength);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetSelectionV1(IntPtr Host, int Start, int End);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetBoundsV1(IntPtr Host, int X, int Y, int Width, int Height);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetClipV1(IntPtr Host, int Left, int Top, int Right, int Bottom);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetClipV2(IntPtr Host,int Left,int Top,int Right,int Bottom,[In] ClipRect[] Holes,uint Count);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetVisibleV1(IntPtr Host, int Visible);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextSetEnabledV1(IntPtr Host, int Enabled);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int LuiWinUITextFocusV1(IntPtr Host);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    private static extern int LuiWinUITextClearTemporaryColorsV1(IntPtr Editor);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    private static extern unsafe int LuiWinUITextApplyTemporaryColorsV1(IntPtr Editor, ColorSpan* Spans, uint Count);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    private static extern unsafe int LuiWinUITextSetColorsV1(IntPtr Editor, ColorSpan* Spans, uint Count);

    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    private static extern int GetWindowTextLengthW(IntPtr Editor);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    private static extern int GetWindowTextW(IntPtr Editor, StringBuilder Text, int Capacity);

    internal static string? ReadText(IntPtr Host)
    {
        IntPtr Editor=LuiWinUITextEditorV1(Host);
        if(Editor==IntPtr.Zero) return null;
        int Length=GetWindowTextLengthW(Editor);
        if(Length<0 || Length>524288) return null;
        StringBuilder Text=new(Length+1);
        if(GetWindowTextW(Editor,Text,Text.Capacity)!=Length) return null;
        string Canonical=Text.ToString().Replace("\r\n","\n",StringComparison.Ordinal);
        return Canonical.Length>262144 || Canonical.Contains('\r') ? null : Canonical;
    }

    internal static bool SetSelection(IntPtr Host,int Start,int Length)
    {
        string? Text=ReadText(Host);
        if(Text is null) return false;
        Span<int> Bounds=stackalloc int[2] {Start,Start+Length};
        Span<int> Mapped=stackalloc int[2];
        return CanonicalTextOffsets.TryMapScalarBoundaries(Text,Bounds,Mapped) &&
            LuiWinUITextSetSelectionV1(Host,Mapped[0],Mapped[1])==1;
    }

    internal static unsafe bool SetStyles(IntPtr Host,string Encoded)
    {
        IntPtr Editor=LuiWinUITextEditorV1(Host);
        if(Editor==IntPtr.Zero) return false;
        if(Encoded.Length==0) return LuiWinUITextSetColorsV1(Editor,null,0)==1;
        string[] Parts=Encoded.Split(';');
        if(Parts.Length<1 || Parts.Length>129 ||
            !ulong.TryParse(Parts[0],NumberStyles.None,CultureInfo.InvariantCulture,out ulong Revision) || Revision==0) return false;
        string? Text=ReadText(Host);
        if(Text is null) return false;
        int Count=Parts.Length-1;
        int[] ScalarBounds=new int[Count*2],TomBounds=new int[Count*2];
        ColorSpan[] Spans=new ColorSpan[Count];
        int Previous=0;
        for(int Index=0;Index<Count;++Index)
        {
            string[] Fields=Parts[Index+1].Split(',');
            if(Fields.Length!=3 ||
                !int.TryParse(Fields[0],NumberStyles.None,CultureInfo.InvariantCulture,out int Start) ||
                !int.TryParse(Fields[1],NumberStyles.None,CultureInfo.InvariantCulture,out int End) ||
                !uint.TryParse(Fields[2],NumberStyles.None,CultureInfo.InvariantCulture,out uint Color) ||
                Start<Previous || End<=Start || Color>0xFFFFFF) return false;
            Previous=End;
            ScalarBounds[2*Index]=Start; ScalarBounds[2*Index+1]=End;
            Spans[Index].Color=((Color&0xFF)<<16)|(Color&0xFF00)|((Color>>16)&0xFF);
        }
        if(!CanonicalTextOffsets.TryMapScalarBoundaries(Text,ScalarBounds,TomBounds)) return false;
        for(int Index=0;Index<Count;++Index)
        {
            Spans[Index].Start=TomBounds[2*Index]; Spans[Index].End=TomBounds[2*Index+1];
        }
        if(Count==0) return LuiWinUITextSetColorsV1(Editor,null,0)==1;
        fixed(ColorSpan* Data=Spans)
            return LuiWinUITextSetColorsV1(Editor,Data,(uint)Count)==1;
    }
}
