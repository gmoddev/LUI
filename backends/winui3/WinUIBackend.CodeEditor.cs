using System.Globalization;
using System.Runtime.InteropServices;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    [StructLayout(LayoutKind.Sequential)]
    private struct CodePoint {internal int X,Y;}
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ClientToScreen(IntPtr Window,ref CodePoint Point);
    private static void RequireCodeEditor(bool Success,string Operation)
    {
        if(!Success) throw new InvalidOperationException("[LUI:CodeEditor] " + Operation + " failed");
    }

    private void ConfigureCodeEditor(View Target,string Value)
    {
        string[] Fields=Value.Split(',',4);
        if(Fields.Length!=4 || Fields[0]!="1" || Fields[1]!="0" ||
            !int.TryParse(Fields[2],NumberStyles.None,CultureInfo.InvariantCulture,out int Limit) ||
            Limit<0 || Limit>262144) throw new InvalidOperationException("[LUI:CodeEditor] Invalid native configuration");
        Target.Multiline=true;
        Target.MaxLength=Limit;
        Target.Placeholder=Fields[3];
        RequireCodeEditor(CodeEditorHost.LuiWinUITextSetLimitV1(Target.CodeHost,Limit)==1,"Limit");
    }

    private void OnCodeEditorEvent(IntPtr Context,int Kind,IntPtr Text,int Start,int Length)
    {
        try
        {
            int Id=Context.ToInt32();
            if(IsDisposed || IsDisposing || !Views.TryGetValue(Id,out View? Target) ||
                !Target.IsAlive || Target.ClassName!="CodeEditor" || Target.ApplyingProperty) return;
            if(Kind==1)
            {
                string Source=Marshal.PtrToStringUni(Text) ?? string.Empty;
                if(Source.Contains('\r')) return;
                Native.Lui_TextInput(Runtime,Id,Source,Start,Length);
            }
            else if(Kind==2 || Kind==3)
            {
                if(Kind==2) {
                    if(FocusedViewId!=0 && FocusedViewId!=Id) Native.Lui_FocusChanged(Runtime,FocusedViewId,0);
                    FocusedViewId=Id;
                } else if(FocusedViewId==Id) FocusedViewId=0;
                ++FocusGeneration;
                Native.Lui_FocusChanged(Runtime,Id,Kind==2?1:0);
            }
            else if(Kind>=6 && Kind<=8)
                Native.Lui_TextComposition(Runtime,Id,Kind-6,
                    Kind==7?Marshal.PtrToStringUni(Text) ?? string.Empty:string.Empty);
            else if(Kind==4 || Kind==5)
            {
                string? Key=GetPortableKey((Windows.System.VirtualKey)Start);
                if(Key is null) return;
                int Phase=Kind==5?2:Length!=0?1:0;
                Native.Lui_KeyInput(Runtime,Id,Phase,Key);
                // The HWND subclass has no reliable "unhandled after native editing"
                // signal. Do not send a Window shortcut before Rich Edit's default action.
            }
            else if(Kind>=10 && Kind<=17)
            {
                if(Kind==12) Native.Lui_HoverChanged(Runtime,Id,1);
                if(Kind==13) {Native.Lui_HoverChanged(Runtime,Id,0);
                    Native.Lui_PointerInputV2(Runtime,Id,3,0,1,0,0,0);return;}
                int Phase=Kind is 10 or 14 or 16?0:Kind is 11 or 15 or 17?2:1;
                int Button=Kind is 10 or 11?1:Kind is 14 or 15?2:Kind is 16 or 17?3:0;
                if(Target.CodeWindowId==0 || !Views.TryGetValue(Target.CodeWindowId,out View? Window)) return;
                double Scale=Window.Container?.XamlRoot?.RasterizationScale ?? 1;
                if(!double.IsFinite(Scale) || Scale<=0) return;
                Native.Lui_PointerInputV2(Runtime,Id,Phase,0,1,Start/Scale,Length/Scale,Button);
            }
        }
        catch(Exception Error) {LuiDiagnostics.Error("CodeEditor","Native event: " + Error.GetType().Name);}
    }

    private int CodeWindow(int Id)
    {
        HashSet<int> Visited=new();
        while(Id!=0 && Visited.Add(Id) && Views.TryGetValue(Id,out View? Current))
        {
            if(Current.Window is not null && !Current.NativeClosed) return Id;
            Id=Current.ParentId;
        }
        return 0;
    }

    private void AttachCodeEditors()
    {
        foreach(KeyValuePair<int,View> Entry in Views)
        {
            View Target=Entry.Value;
            if(Target.CodeHost==IntPtr.Zero || !Target.IsAlive) continue;
            int WindowId=CodeWindow(Entry.Key);
            if(WindowId==Target.CodeWindowId) continue;
            if(Target.CodeWindowId!=0) CodePresentationCaches.Remove(Target.CodeWindowId);
            RequireCodeEditor(CodeEditorHost.LuiWinUITextSetVisibleV1(Target.CodeHost,0)==1,"Hide before owner transfer");
            IntPtr Parent=WindowId!=0?WinRT.Interop.WindowNative.GetWindowHandle(Views[WindowId].Window!):IntPtr.Zero;
            RequireCodeEditor(CodeEditorHost.LuiWinUITextSetOwnerV1(Target.CodeHost,Parent)==1,"Owner");
            Target.CodeWindowId=WindowId;
            if(WindowId!=0) PositionCodeEditor(Target,Target.CodeWidth,Target.CodeHeight);
            // The next committed Present decides render visibility and applies
            // a region for this owner before showing the popup again.
        }
    }

    private void DetachCodeEditors(int WindowId)
    {
        CodePresentationCaches.Remove(WindowId);
        foreach(View Target in Views.Values)
        {
            if(Target.CodeHost==IntPtr.Zero || Target.CodeWindowId!=WindowId) continue;
            RequireCodeEditor(CodeEditorHost.LuiWinUITextSetVisibleV1(Target.CodeHost,0)==1,"Hide");
            RequireCodeEditor(CodeEditorHost.LuiWinUITextSetOwnerV1(Target.CodeHost,IntPtr.Zero)==1,"Park");
            Target.CodeWindowId=0;
        }
    }

    private static int ToPixel(double Value,double Scale) =>
        (int)Math.Clamp(Math.Round(Value*Scale),-1000000,1000000);

    private void PositionCodeEditor(View Target,double Width,double Height)
    {
        Target.CodeWidth=Width; Target.CodeHeight=Height;
        if(Target.CodeHost==IntPtr.Zero || Target.CodeWindowId==0) return;
        View Window=Views[Target.CodeWindowId];
        double Scale=Window.Container?.XamlRoot?.RasterizationScale ?? 1;
        CodePoint Origin=new();
        RequireCodeEditor(ClientToScreen(WinRT.Interop.WindowNative.GetWindowHandle(Window.Window!),ref Origin),"Client origin");
        RequireCodeEditor(CodeEditorHost.LuiWinUITextSetBoundsV1(Target.CodeHost,
            Origin.X+ToPixel(Target.BoundsX,Scale),Origin.Y+ToPixel(Target.BoundsY,Scale),
            Math.Max(0,ToPixel(Width,Scale)),Math.Max(0,ToPixel(Height,Scale)))==1,"Bounds");
    }

    private void PresentCodeEditor(int Id,View Target,int HasClip,double X,double Y,double Width,double Height)
    {
        if(Target.CodeHost==IntPtr.Zero) return;
        double Scale=Target.CodeWindowId!=0?
            Views[Target.CodeWindowId].Container?.XamlRoot?.RasterizationScale ?? 1:1;
        int Left=HasClip!=0?ToPixel(X,Scale):0;
        int Top=HasClip!=0?ToPixel(Y,Scale):0;
        int Right=HasClip!=0?ToPixel(X+Width,Scale):Math.Max(0,ToPixel(Target.CodeWidth,Scale));
        int Bottom=HasClip!=0?ToPixel(Y+Height,Scale):Math.Max(0,ToPixel(Target.CodeHeight,Scale));
        bool Applied=TryApplyCodeEditorMask(Id,Target,Scale,
            out ulong Revision,out bool? DisplayEligible,out string Reason);
        if(Applied)
            RecordCodeMask(Target,true,Revision,string.Empty);
        else
        {
            // This retains the previous experimental editor behavior, but its
            // airspace is not a qualified compositing result for unsupported paint.
            if(DisplayEligible==true)
                RequireCodeEditor(CodeEditorHost.LuiWinUITextSetClipV1(Target.CodeHost,Left,Top,Right,Bottom)==1,"Clip");
            RecordCodeMask(Target,false,Revision,Reason);
        }
        RequireCodeEditor(CodeEditorHost.LuiWinUITextSetVisibleV1(Target.CodeHost,
            Target.Visible && Target.CodeWindowId!=0 && DisplayEligible==true?1:0)==1,"Visible");
        RequireCodeEditor(CodeEditorHost.LuiWinUITextSetEnabledV1(Target.CodeHost,
            Target.Enabled && Target.InputVisible?1:0)==1,"Enabled");
    }
}
