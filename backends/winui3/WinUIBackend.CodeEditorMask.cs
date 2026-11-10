using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    [StructLayout(LayoutKind.Sequential)]
    private struct CodePresentationEntry
    {
        internal int Id, ParentId;
        internal uint PaintOrder, Flags;
        internal double X, Y, Width, Height;
        internal double ClipX, ClipY, ClipWidth, ClipHeight;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct CodePresentationHeader
    {
        internal uint StructSize, AbiVersion, Capacity, Count;
        internal IntPtr Entries;
        internal ulong Revision;
    }

    private static partial class Native
    {
        [DllImport("LuiRuntime.dll", CallingConvention=CallingConvention.Cdecl)]
        internal static extern int Lui_GetWindowPresentation(IntPtr Runtime,int WindowId,
            ref CodePresentationHeader Snapshot);
        [DllImport("LuiRuntime.dll", CallingConvention=CallingConvention.Cdecl)]
        internal static extern int Lui_IsWindowPresentationCurrent(IntPtr Runtime,int WindowId,ulong Revision);
    }

    private readonly record struct CodeMaskRect(double Left,double Top,double Right,double Bottom)
    {
        internal bool Contains(CodeMaskRect Other) => Left<=Other.Left && Top<=Other.Top &&
            Right>=Other.Right && Bottom>=Other.Bottom;
    }

    private sealed class CodePresentationCache
    {
        internal CodePresentationEntry[] Entries=new CodePresentationEntry[128];
        internal uint Count;
        internal ulong Revision;
    }

    private sealed class CodeMaskState
    {
        internal bool Applied;
        internal ulong Revision;
        internal string Reason=string.Empty;
    }

    private const uint CodePresentationVisible=1;
    private const uint CodePresentationOpaqueRectangle=4;
    private const int CodePresentationCacheWindowLimit=4;
    private readonly Dictionary<int,CodePresentationCache> CodePresentationCaches=new();
    private readonly ConditionalWeakTable<View,CodeMaskState> CodeMaskStates=new();

    private static bool TryCodeMaskRect(CodePresentationEntry Entry,out CodeMaskRect Rect)
    {
        Rect=default;
        if(!double.IsFinite(Entry.X) || !double.IsFinite(Entry.Y) ||
            !double.IsFinite(Entry.Width) || !double.IsFinite(Entry.Height) ||
            !double.IsFinite(Entry.ClipX) || !double.IsFinite(Entry.ClipY) ||
            !double.IsFinite(Entry.ClipWidth) || !double.IsFinite(Entry.ClipHeight) ||
            Entry.Width<0 || Entry.Height<0 || Entry.ClipWidth<0 || Entry.ClipHeight<0) return false;
        double Left=Math.Max(Entry.X,Entry.ClipX);
        double Top=Math.Max(Entry.Y,Entry.ClipY);
        double Right=Math.Min(Entry.X+Entry.Width,Entry.ClipX+Entry.ClipWidth);
        double Bottom=Math.Min(Entry.Y+Entry.Height,Entry.ClipY+Entry.ClipHeight);
        if(!double.IsFinite(Right) || !double.IsFinite(Bottom)) return false;
        Rect=new CodeMaskRect(Left,Top,Math.Max(Left,Right),Math.Max(Top,Bottom));
        return true;
    }

    private static bool TryCodeMaskIntersect(CodeMaskRect Left,CodeMaskRect Right,out CodeMaskRect Intersection)
    {
        double X=Math.Max(Left.Left,Right.Left),Y=Math.Max(Left.Top,Right.Top);
        double EndX=Math.Min(Left.Right,Right.Right),EndY=Math.Min(Left.Bottom,Right.Bottom);
        Intersection=new CodeMaskRect(X,Y,EndX,EndY);
        return EndX>X && EndY>Y;
    }

    private static bool CodeMaskContainsAny(List<CodeMaskRect> Opaque,CodeMaskRect Rect)
    {
        foreach(CodeMaskRect Cover in Opaque)
            if(Cover.Contains(Rect)) return true;
        return false;
    }

    private static bool CodeMaskFullyCovered(CodeMaskRect Visible,List<CodeMaskRect> Opaque)
    {
        if(Opaque.Count==0 || Visible.Right<=Visible.Left || Visible.Bottom<=Visible.Top) return false;
        List<double> XEdges=new(2+Opaque.Count*2) {Visible.Left,Visible.Right};
        List<double> YEdges=new(2+Opaque.Count*2) {Visible.Top,Visible.Bottom};
        foreach(CodeMaskRect Hole in Opaque)
        {
            XEdges.Add(Hole.Left);XEdges.Add(Hole.Right);
            YEdges.Add(Hole.Top);YEdges.Add(Hole.Bottom);
        }
        XEdges.Sort();YEdges.Sort();
        for(int X=1;X<XEdges.Count;++X)
        {
            if(XEdges[X]<=XEdges[X-1]) continue;
            double MiddleX=(XEdges[X]+XEdges[X-1])/2;
            for(int Y=1;Y<YEdges.Count;++Y)
            {
                if(YEdges[Y]<=YEdges[Y-1]) continue;
                double MiddleY=(YEdges[Y]+YEdges[Y-1])/2;
                bool Covered=false;
                foreach(CodeMaskRect Hole in Opaque)
                    if(Hole.Left<=MiddleX && MiddleX<Hole.Right &&
                        Hole.Top<=MiddleY && MiddleY<Hole.Bottom)
                    {Covered=true;break;}
                if(!Covered) return false;
            }
        }
        return true;
    }

    private static bool TryCodeMaskPixel(double Value,double Scale,out int Pixel)
    {
        Pixel=0;
        double Scaled=Value*Scale;
        if(!double.IsFinite(Scaled) || Scaled< -1000000 || Scaled>1000000) return false;
        double Rounded=Math.Round(Scaled);
        if(Math.Abs(Scaled-Rounded)>0.000001) return false;
        Pixel=(int)Rounded;
        return true;
    }

    private static bool TryCodeMaskPixels(CodeMaskRect Rect,CodePresentationEntry Editor,double Scale,
        out CodeEditorHost.ClipRect Pixels)
    {
        Pixels=default;
        if(!TryCodeMaskPixel(Rect.Left-Editor.X,Scale,out Pixels.Left) ||
            !TryCodeMaskPixel(Rect.Top-Editor.Y,Scale,out Pixels.Top) ||
            !TryCodeMaskPixel(Rect.Right-Editor.X,Scale,out Pixels.Right) ||
            !TryCodeMaskPixel(Rect.Bottom-Editor.Y,Scale,out Pixels.Bottom)) return false;
        return Pixels.Right>=Pixels.Left && Pixels.Bottom>=Pixels.Top;
    }

    private bool TryCodeMaskUnitOpacity(int Id,out string Reason)
    {
        int EditorId=Id;
        HashSet<int> Visited=new();
        while(Id!=0 && Visited.Count<4096 && Visited.Add(Id))
        {
            if(!Views.TryGetValue(Id,out View? Current) || !Current.IsAlive)
            {Reason="editor ancestry unavailable";return false;}
            if(Current.StyleJson.Length!=0)
            {
                using JsonDocument Style=JsonDocument.Parse(Current.StyleJson);
                if(Style.RootElement.TryGetProperty("Opacity",out JsonElement Opacity) &&
                    Opacity.GetDouble()!=1)
                {Reason="editor or ancestor alpha unsupported";return false;}
                if(Id==EditorId &&
                    Style.RootElement.TryGetProperty("BackgroundTransparency",out JsonElement BackgroundTransparency) &&
                    BackgroundTransparency.GetDouble()!=0)
                {Reason="editor background alpha unsupported";return false;}
            }
            Id=Current.ParentId;
        }
        Reason=Id==0?string.Empty:"editor ancestry limit";
        return Id==0;
    }

    private unsafe bool TryCodePresentation(int WindowId,out CodePresentationCache Cache,out string Reason)
    {
        if(!CodePresentationCaches.TryGetValue(WindowId,out Cache!))
        {
            // The cache is an optimization, never an unbounded registry of
            // former Windows or editor source. Eviction only costs a requery.
            if(CodePresentationCaches.Count==CodePresentationCacheWindowLimit)
                CodePresentationCaches.Remove(CodePresentationCaches.Keys.First());
            Cache=new CodePresentationCache();
            CodePresentationCaches.Add(WindowId,Cache);
        }
        if(Cache.Revision!=0 &&
            Native.Lui_IsWindowPresentationCurrent(Runtime,WindowId,Cache.Revision)==1)
        {
            Reason=string.Empty;
            return true;
        }
        Cache.Revision=0;
        while(true)
        {
            fixed(CodePresentationEntry* Entries=Cache.Entries)
            {
                CodePresentationHeader Snapshot=new()
                {
                    StructSize=(uint)Marshal.SizeOf<CodePresentationHeader>(),
                    AbiVersion=1,
                    Capacity=(uint)Cache.Entries.Length,
                    Entries=(IntPtr)Entries
                };
                int Result=Native.Lui_GetWindowPresentation(Runtime,WindowId,ref Snapshot);
                if(Result==1 && Snapshot.Revision!=0 && Snapshot.Count<=Snapshot.Capacity)
                {
                    Cache.Count=Snapshot.Count;
                    Cache.Revision=Snapshot.Revision;
                    Reason=string.Empty;
                    return true;
                }
                if(Result!=2 || Cache.Entries.Length==4096)
                {
                    Reason=Result==2?"presentation entry limit":"presentation unavailable";
                    return false;
                }
            }
            Cache.Entries=new CodePresentationEntry[Math.Min(4096,Cache.Entries.Length*2)];
        }
    }

    private bool TryApplyCodeEditorMask(int Id,View Target,double Scale,
        out ulong Revision,out bool? DisplayEligible,out string Reason)
    {
        Revision=0;
        DisplayEligible=null;
        Reason=string.Empty;
        int WindowId=Target.CodeWindowId;
        IntPtr Host=Target.CodeHost;
        if(WindowId==0 || Host==IntPtr.Zero || !double.IsFinite(Scale) || Scale<=0)
        {
            Reason="unattached or invalid scale";
            return false;
        }
        try
        {
            if(!TryCodePresentation(WindowId,out CodePresentationCache Cache,out Reason)) return false;
            Revision=Cache.Revision;
            int EditorIndex=-1;
            for(int Index=0;Index<Cache.Count;++Index)
                if(Cache.Entries[Index].Id==Id) {EditorIndex=Index;break;}
            if(EditorIndex<0) {Reason="editor absent from owner presentation";return false;}
            CodePresentationEntry Editor=Cache.Entries[EditorIndex];
            DisplayEligible=(Editor.Flags&CodePresentationVisible)!=0;
            if(DisplayEligible==false)
            {Reason="editor not render-visible";return false;}
            if(!TryCodeMaskUnitOpacity(Id,out Reason))
            {DisplayEligible=false;return false;}
            if(!TryCodeMaskRect(Editor,out CodeMaskRect Visible))
            {Reason="invalid editor geometry";return false;}
            if(Math.Abs(Editor.X-Target.BoundsX)>0.000001 ||
                Math.Abs(Editor.Y-Target.BoundsY)>0.000001 ||
                Math.Abs(Editor.Width-Target.CodeWidth)>0.000001 ||
                Math.Abs(Editor.Height-Target.CodeHeight)>0.000001)
            {Reason="editor bounds differ from owner";return false;}
            if(!TryCodeMaskPixel(Editor.X,Scale,out _) ||
                !TryCodeMaskPixel(Editor.Y,Scale,out _) ||
                !TryCodeMaskPixel(Editor.X+Editor.Width,Scale,out _) ||
                !TryCodeMaskPixel(Editor.Y+Editor.Height,Scale,out _))
            {Reason="partial-pixel editor bounds";return false;}
            if(!TryCodeMaskPixels(Visible,Editor,Scale,out CodeEditorHost.ClipRect Base))
            {Reason="partial-pixel editor clip";return false;}
            List<CodeMaskRect> Opaque=new(16);
            for(int Index=EditorIndex+1;Index<Cache.Count;++Index)
            {
                CodePresentationEntry Above=Cache.Entries[Index];
                if((Above.Flags&CodePresentationVisible)==0 ||
                    (Above.Flags&CodePresentationOpaqueRectangle)==0) continue;
                if(!TryCodeMaskRect(Above,out CodeMaskRect Rect))
                {Reason="invalid higher geometry";return false;}
                if(!TryCodeMaskIntersect(Rect,Visible,out CodeMaskRect Hole)) continue;
                if(CodeMaskContainsAny(Opaque,Hole)) continue;
                for(int Existing=Opaque.Count-1;Existing>=0;--Existing)
                    if(Hole.Contains(Opaque[Existing])) Opaque.RemoveAt(Existing);
                Opaque.Add(Hole);
                if(Opaque.Count>16) {Reason="opaque region count exceeds 16";return false;}
            }
            for(int Index=EditorIndex+1;Index<Cache.Count;++Index)
            {
                CodePresentationEntry Above=Cache.Entries[Index];
                if((Above.Flags&CodePresentationVisible)==0 ||
                    (Above.Flags&CodePresentationOpaqueRectangle)!=0) continue;
                if(!TryCodeMaskRect(Above,out CodeMaskRect Rect))
                {Reason="invalid higher geometry";return false;}
                if(TryCodeMaskIntersect(Rect,Visible,out CodeMaskRect Unknown) &&
                    !CodeMaskContainsAny(Opaque,Unknown))
                {Reason="higher paint lacks opaque rectangle proof";return false;}
            }
            if(CodeMaskFullyCovered(Visible,Opaque))
            {DisplayEligible=false;Reason="fully occluded editor focus policy unqualified";return false;}
            CodeEditorHost.ClipRect[] Holes=new CodeEditorHost.ClipRect[Opaque.Count];
            for(int Index=0;Index<Opaque.Count;++Index)
                if(!TryCodeMaskPixels(Opaque[Index],Editor,Scale,out Holes[Index]))
                {Reason="partial-pixel opaque overlap";return false;}
            if(!Target.IsAlive || Target.CodeWindowId!=WindowId || Target.CodeHost!=Host ||
                !Views.TryGetValue(WindowId,out View? Window) || Window.NativeClosed ||
                Native.Lui_IsWindowPresentationCurrent(Runtime,WindowId,Revision)!=1)
            {DisplayEligible=false;Reason="presentation changed before mask application";return false;}
            if(CodeEditorHost.LuiWinUITextSetClipV2(Host,Base.Left,Base.Top,
                Base.Right,Base.Bottom,Holes,(uint)Holes.Length)!=1)
            {Reason="native region rejected";return false;}
            return true;
        }
        catch(Exception Error)
        {
            DisplayEligible=false;
            Reason="presentation exception: "+Error.GetType().Name;
            return false;
        }
    }

    private void RecordCodeMask(View Target,bool Applied,ulong Revision,string Reason)
    {
        CodeMaskState Status=CodeMaskStates.GetValue(Target,_=>new CodeMaskState());
        if(!Applied && Status.Reason!=Reason)
            LuiDiagnostics.Log("CodeEditor","Opaque mask unsupported: "+Reason);
        Status.Applied=Applied;
        Status.Revision=Revision;
        Status.Reason=Applied?string.Empty:Reason;
    }
}
