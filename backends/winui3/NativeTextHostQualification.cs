using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Lui.WinUI;

// Bounded hosting probe. This is not an LUI TextBox implementation or a capability grant.
internal static class NativeTextHostQualificationRunner
{
    private const int WsChild = 0x40000000;
    private const int WsVisible = 0x10000000;
    private const int WsVScroll = 0x00200000;
    private const int WsTabStop = 0x00010000;
    private const int EsMultiline = 0x0004;
    private const int EsAutoVScroll = 0x0040;
    private const int EsWantReturn = 0x1000;
    private const uint WmChar = 0x0102;
    private const uint WmKeyDown = 0x0100;
    private const uint EmEmptyUndoBuffer = 0x00CD;
    private const uint EmCanUndo = 0x00C6;
    private const uint EmUndo = 0x00C7;
    private const uint EmCanRedo = 0x0455;
    private const uint EmRedo = 0x0454;
    private const uint EmExSetSel = 0x0437;
    private const uint EmExGetSel = 0x0434;
    private const uint EmGetScrollPos = 0x04DD;
    private const uint EmSetModify = 0x00B9;
    private const uint EmGetModify = 0x00B8;
    private const uint EmSetSel = 0x00B1;
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpNoZOrder = 0x0004;

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    private struct TextSelection { public int Start, End; }
    [StructLayout(LayoutKind.Sequential)]
    private struct TextColorSpan { public int Start, End; public uint Color; }
    private delegate bool EnumWindowsProc(IntPtr Window, IntPtr Context);

    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    private static extern unsafe int LuiWinUITextApplyTemporaryColorsV1(IntPtr Editor, TextColorSpan* Spans, uint Count);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    private static extern int LuiWinUITextSetShortcutProbeV1(IntPtr Host,int Enabled);
    [DllImport("LuiWinUIText.dll", CallingConvention=CallingConvention.Cdecl)]
    private static extern int LuiWinUITextTestSuppressedImeEndV1(IntPtr Host);

    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern IntPtr LoadLibraryW(string Name);
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool FreeLibrary(IntPtr Module);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern IntPtr CreateWindowExW(int ExtendedStyle, string ClassName, string Text, int Style,
        int X, int Y, int Width, int Height, IntPtr Parent, IntPtr Menu, IntPtr Instance, IntPtr Parameter);
    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyWindow(IntPtr Window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowTextW(IntPtr Window, string Text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern int GetWindowTextW(IntPtr Window, StringBuilder Text, int Capacity);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern int GetWindowTextLengthW(IntPtr Window);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern IntPtr GetParent(IntPtr Window);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern IntPtr SetParent(IntPtr Window, IntPtr NewParent);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern IntPtr GetFocus();
    [DllImport("user32.dll", SetLastError=true)]
    private static extern IntPtr SetFocus(IntPtr Window);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern IntPtr SendMessageW(IntPtr Window, uint Message, IntPtr WParam, IntPtr LParam);
    [DllImport("user32.dll", EntryPoint="SendMessageW", SetLastError=true)]
    private static extern IntPtr SendSelectionMessage(IntPtr Window, uint Message, IntPtr WParam, ref TextSelection Selection);
    [DllImport("user32.dll", EntryPoint="SendMessageW", SetLastError=true)]
    private static extern IntPtr SendPointMessage(IntPtr Window, uint Message, IntPtr WParam, ref NativePoint Point);
    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessageW(IntPtr Window, uint Message, IntPtr WParam, IntPtr LParam);
    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowPos(IntPtr Window, IntPtr InsertAfter, int X, int Y, int Width, int Height, uint Flags);
    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindow(IntPtr Window);
    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetWindowRect(IntPtr Window, out NativeRect Rect);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern IntPtr WindowFromPoint(NativePoint Point);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern IntPtr GetTopWindow(IntPtr Parent);
    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr Window);
    [DllImport("gdi32.dll", SetLastError=true)]
    private static extern IntPtr CreateRectRgn(int Left, int Top, int Right, int Bottom);
    [DllImport("gdi32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteObject(IntPtr Object);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern int SetWindowRgn(IntPtr Window, IntPtr Region, [MarshalAs(UnmanagedType.Bool)] bool Redraw);
    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumChildWindows(IntPtr Parent, EnumWindowsProc Callback, IntPtr Context);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern int GetClassNameW(IntPtr Window, StringBuilder ClassName, int Capacity);

    private static void Require(bool Condition, string Name)
    {
        if(!Condition) throw new InvalidOperationException("[LUI:NativeTextHost] " + Name);
    }

    private static string ReadText(IntPtr Editor)
    {
        int Length=GetWindowTextLengthW(Editor);
        StringBuilder Text=new(Length+1);
        Require(GetWindowTextW(Editor,Text,Text.Capacity)==Length,"ReadText");
        return Text.ToString();
    }

    private static List<string> ChildClasses(IntPtr Host)
    {
        List<string> Classes=new();
        EnumChildWindows(Host,(Child,_)=>
        {
            StringBuilder Name=new(256);
            GetClassNameW(Child,Name,Name.Capacity);
            Classes.Add(Name.ToString());
            return true;
        },IntPtr.Zero);
        return Classes;
    }

    private static string WindowClass(IntPtr Window)
    {
        StringBuilder Name=new(256);
        GetClassNameW(Window,Name,Name.Capacity);
        return Name.ToString();
    }

    private static unsafe int TryApplyColors(IntPtr Editor, TextColorSpan[] Spans)
    {
        fixed(TextColorSpan* Data=Spans)
            return LuiWinUITextApplyTemporaryColorsV1(Editor,Data,(uint)Spans.Length);
    }

    private static void ApplyColors(IntPtr Editor, TextColorSpan[] Spans)
    {
        Require(TryApplyColors(Editor,Spans)==1,"TemporaryNativeColors");
    }

    public static async Task<bool> RunAsync(string ReportPath)
    {
        List<string> Passed=new();
        Microsoft.UI.Xaml.Window? Window=null;
        Microsoft.UI.Xaml.Window? SecondWindow=null;
        IntPtr Module=IntPtr.Zero;
        IntPtr Editor=IntPtr.Zero;
        IntPtr CodeHost=IntPtr.Zero;
        int OwnerThread=Environment.CurrentManagedThreadId;
        bool Success=false;
        string? Failure=null;
        List<string> BeforeChildClasses=new(), AfterChildClasses=new();
        bool EditorHitTest=false;
        bool EditorVisible=false;
        string TopChildClass=string.Empty;
        string HitWindowClass=string.Empty;
        double BatchP50Us=0, BatchP95Us=0, BatchP99Us=0;
        double EditToStyleP50Us=0, EditToStyleP95Us=0, EditToStyleP99Us=0;
        int BatchUtf16Units=0;
        try
        {
            Module=LoadLibraryW("Msftedit.dll");
            Require(Module!=IntPtr.Zero,"SystemRichEditLoad");
            Window=new Microsoft.UI.Xaml.Window { Title="LUI native text host qualification" };
            Grid Root=new();
            Root.Children.Add(new TextBlock { Text="Native Rich Edit host qualification", Margin=new Thickness(8) });
            Window.Content=Root;
            Window.AppWindow.Resize(new Windows.Graphics.SizeInt32(480,320));
            Window.Activate();
            await Task.Delay(80);
            Require(Environment.CurrentManagedThreadId==OwnerThread,"OwnerThread");
            IntPtr Host=WinRT.Interop.WindowNative.GetWindowHandle(Window);
            Require(Host!=IntPtr.Zero,"WinUIWindowHandle");
            BeforeChildClasses=ChildClasses(Host);
            Editor=CreateWindowExW(0,"RICHEDIT50W",string.Empty,
                WsChild|WsVisible|WsVScroll|WsTabStop|EsMultiline|EsAutoVScroll|EsWantReturn,
                20,50,420,220,Host,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);
            Require(Editor!=IntPtr.Zero && GetParent(Editor)==Host,"RichEditChildOwnership");
            Require(SetWindowPos(Editor,IntPtr.Zero,20,50,420,220,SwpNoActivate),"RaiseRichEditAboveWinUIContent");
            AfterChildClasses=ChildClasses(Host);
            TopChildClass=WindowClass(GetTopWindow(Host));
            EditorVisible=IsWindowVisible(Editor);
            Require(GetWindowRect(Editor,out NativeRect EditorRect),"EditorScreenRect");
            IntPtr HitWindow=WindowFromPoint(new NativePoint {X=(EditorRect.Left+EditorRect.Right)/2,Y=(EditorRect.Top+EditorRect.Bottom)/2});
            EditorHitTest=HitWindow==Editor;
            HitWindowClass=WindowClass(HitWindow);
            Require(EditorHitTest,"EditorWinsNativeHitTest");
            Require(SetWindowTextW(Editor,"local Value = '\U0001F600'\r\nreturn Value"),"InitialText");
            string InitialText=ReadText(Editor);
            int[] ScalarBoundaries=[3,9];
            int[] TomBoundaries=new int[2];
            Require(CanonicalTextOffsets.TryMapScalarBoundaries("A\U0001F600\nreturn",ScalarBoundaries,TomBoundaries) &&
                TomBoundaries[0]==4 && TomBoundaries[1]==10,"LfScalarSurrogateTomMapping");
            Require(!CanonicalTextOffsets.TryMapScalarBoundaries("A\r\nreturn",ScalarBoundaries,TomBoundaries),
                "StyledCrLfRejected");
            Require(!CanonicalTextOffsets.TryMapScalarBoundaries("A\uD800",[0,1],TomBoundaries),
                "StyledInvalidSurrogateRejected");
            SendMessageW(Editor,EmEmptyUndoBuffer,IntPtr.Zero,IntPtr.Zero);
            Passed.Add("WinUI top-level HWND owns one native system Rich Edit child");

            SetFocus(Editor);
            Require(GetFocus()==Editor,"NativeFocus");
            Require(PostMessageW(Editor,WmChar,new IntPtr('x'),IntPtr.Zero),"QueueNativeCharacter");
            await Task.Delay(80);
            Require(Environment.CurrentManagedThreadId==OwnerThread,"InputOwnerThread");
            string EditedText=ReadText(Editor);
            Require(EditedText=="x"+InitialText,"NativeTyping");
            Require(SendMessageW(Editor,EmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,"NativeUndoAvailable");
            TextSelection Selection=new() {Start=2,End=8};
            SendSelectionMessage(Editor,EmExSetSel,IntPtr.Zero,ref Selection);
            NativePoint Scroll=new();
            SendPointMessage(Editor,EmGetScrollPos,IntPtr.Zero,ref Scroll);
            SendMessageW(Editor,EmSetModify,IntPtr.Zero,IntPtr.Zero);
            for(int Index=0;Index<8;++Index)
                ApplyColors(Editor,[new TextColorSpan {Start=0,End=5,Color=(uint)(0x00BE5000+Index)}]);
            TextSelection AfterSelection=new();
            SendSelectionMessage(Editor,EmExGetSel,IntPtr.Zero,ref AfterSelection);
            NativePoint AfterScroll=new();
            SendPointMessage(Editor,EmGetScrollPos,IntPtr.Zero,ref AfterScroll);
            Require(ReadText(Editor)==EditedText,"StyleRetainsExactUnicodeText");
            Require(AfterSelection.Start==Selection.Start && AfterSelection.End==Selection.End,"StyleRetainsSelection");
            Require(AfterScroll.X==Scroll.X && AfterScroll.Y==Scroll.Y,"StyleRetainsScroll");
            Require(SendMessageW(Editor,EmGetModify,IntPtr.Zero,IntPtr.Zero)==IntPtr.Zero,"StyleRetainsModifiedFlag");
            Require(TryApplyColors(Editor,[new TextColorSpan {Start=8,End=2,Color=0x000000FF}])==0,
                "MalformedRangeRejected");
            Require(TryApplyColors(Editor,[new TextColorSpan {Start=0,End=5,Color=0x000000FF},
                new TextColorSpan {Start=4,End=8,Color=0x000000FF}])==0,"OverlappingRangesRejected");
            Require(TryApplyColors(Editor,new TextColorSpan[129])==0,"OversizedBatchRejected");
            Require(TryApplyColors(Host,[new TextColorSpan {Start=0,End=1,Color=0x000000FF}])==0,
                "ForeignControlRejected");
            Require(ReadText(Editor)==EditedText,"RejectedBatchRetainsSource");
            Require(SendMessageW(Editor,EmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,"StyleRetainsUndo");
            Require(SendMessageW(Editor,EmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero && ReadText(Editor)==InitialText,"OneNativeUndo");
            Require(SendMessageW(Editor,EmCanRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,"NativeRedoAvailable");
            ApplyColors(Editor,[new TextColorSpan {Start=0,End=5,Color=0x00E6780A}]);
            Require(SendMessageW(Editor,EmCanRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,"StyleRetainsRedo");
            Require(SendMessageW(Editor,EmRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero && ReadText(Editor)==EditedText,"OneNativeRedo");
            Passed.Add("Hosted temporary TOM color preserves exact Unicode text, selection, scroll, modified flag, one native Undo and Redo");

            IntPtr Region=CreateRectRgn(0,0,180,180);
            Require(Region!=IntPtr.Zero,"EditorClipRegion");
            if(SetWindowRgn(Editor,Region,true)==0)
            {
                DeleteObject(Region);
                throw new InvalidOperationException("[LUI:NativeTextHost] ApplyEditorClip");
            }
            Require(GetWindowRect(Editor,out EditorRect),"ClippedEditorScreenRect");
            Require(WindowFromPoint(new NativePoint {X=EditorRect.Left+90,Y=EditorRect.Top+90})==Editor,
                "VisibleEditorClipHit");
            Require(WindowFromPoint(new NativePoint {X=EditorRect.Left+300,Y=EditorRect.Top+90})!=Editor,
                "ExcludedEditorClipHit");
            Require(SetWindowRgn(Editor,IntPtr.Zero,true)!=0,"ClearEditorClip");
            Passed.Add("HWND region clips editor pointer hit testing without replacing its text buffer");

            Require(SetWindowPos(Editor,IntPtr.Zero,25,55,360,180,SwpNoActivate|SwpNoZOrder),"MoveResizeChild");
            Require(GetParent(Editor)==Host && GetFocus()==Editor,"MoveRetainsOwnershipAndFocus");
            Passed.Add("Child movement/resizing retains parent and focus");

            SecondWindow=new Microsoft.UI.Xaml.Window { Title="LUI native text redock qualification", Content=new Grid() };
            SecondWindow.AppWindow.Resize(new Windows.Graphics.SizeInt32(480,320));
            SecondWindow.Activate();
            await Task.Delay(40);
            IntPtr SecondHost=WinRT.Interop.WindowNative.GetWindowHandle(SecondWindow);
            Require(SecondHost!=IntPtr.Zero && SecondHost!=Host,"SecondWinUIWindowHandle");
            Require(SetParent(Editor,SecondHost)==Host && GetParent(Editor)==SecondHost,"ReparentNativeEditor");
            Require(SetWindowPos(Editor,IntPtr.Zero,20,20,420,220,SwpNoActivate),"RearrangeNativeEditor");
            Require(ReadText(Editor)==EditedText && SendMessageW(Editor,EmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "ReparentRetainsBufferAndUndo");
            Window.Close();
            Window=null;
            Require(IsWindow(Editor) && GetParent(Editor)==SecondHost && ReadText(Editor)==EditedText,
                "RetiredOldOwnerKeepsRedockedEditor");
            Passed.Add("Second WinUI HWND adopts the same editor; old-window retirement preserves text and Undo");

            StringBuilder Large=new();
            while(Large.Length<32000) Large.Append("local Value = 'text' -- comment\r\n");
            Require(SetWindowTextW(Editor,Large.ToString()),"LargeSourceFixture");
            string LargeBefore=ReadText(Editor);
            BatchUtf16Units=LargeBefore.Length;
            TextColorSpan[] Spans=new TextColorSpan[128];
            for(int Index=0;Index<Spans.Length;++Index)
                Spans[Index]=new TextColorSpan {Start=Index*32,End=Index*32+5,Color=0x00B46E14};
            List<double> Samples=new(128);
            for(int Sample=0;Sample<136;++Sample)
            {
                for(int Index=0;Index<Spans.Length;++Index) Spans[Index].Color=(uint)(0x00B46E14+Sample%100);
                long Started=Stopwatch.GetTimestamp();
                ApplyColors(Editor,Spans);
                if(Sample>=8) Samples.Add(Stopwatch.GetElapsedTime(Started).TotalMicroseconds);
            }
            Require(ReadText(Editor)==LargeBefore,"LargeStyleRetainsExactSource");
            Samples.Sort();
            BatchP50Us=Samples[63];BatchP95Us=Samples[121];BatchP99Us=Samples[126];
            Passed.Add("128 bounded temporary ranges on 32k UTF-16 source retain exact text");

            SendMessageW(Editor,EmSetSel,new IntPtr(-1),new IntPtr(-1));
            List<double> EditSamples=new(128);
            for(int Sample=0;Sample<136;++Sample)
            {
                Spans[0].Color=(uint)(0x00B46E14+Sample%100);
                long Started=Stopwatch.GetTimestamp();
                SendMessageW(Editor,WmChar,new IntPtr('z'),IntPtr.Zero);
                ApplyColors(Editor,Spans);
                if(Sample>=8) EditSamples.Add(Stopwatch.GetElapsedTime(Started).TotalMicroseconds);
            }
            Require(ReadText(Editor)==LargeBefore+new string('z',136),"EditThenStyleExactSource");
            EditSamples.Sort();
            EditToStyleP50Us=EditSamples[63];EditToStyleP95Us=EditSamples[121];EditToStyleP99Us=EditSamples[126];
            Passed.Add("136 synchronous native character edits followed by 128-span styling retain exact source");
            int CodeEdits=0;
            string LastCodeEdit=string.Empty;
            string KeyProbeSequence=string.Empty;
            CodeEditorHost.EditorEvent CodeEvent=(Context,Kind,Text,Start,Length)=>
            {
                if(Kind==4 && Start==120) KeyProbeSequence+="before;";
                if(Kind==18 && Start==120) KeyProbeSequence+="after;";
                if(Kind!=1) return;
                ++CodeEdits;
                LastCodeEdit=Marshal.PtrToStringUni(Text) ?? string.Empty;
            };
            CodeHost=CodeEditorHost.LuiWinUITextCreateHostV1(IntPtr.Zero,CodeEvent,IntPtr.Zero);
            Require(CodeHost!=IntPtr.Zero && CodeEditorHost.LuiWinUITextEditorV1(CodeHost)!=IntPtr.Zero,
                "ProductionCodeEditorHostCreated");
            Require(CodeEditorHost.LuiWinUITextSetTextV1(CodeHost,"A\U0001F600\nreturn")==1 &&
                CodeEditorHost.ReadText(CodeHost)=="A\U0001F600\nreturn","ProductionCodeEditorCanonicalText");
            Require(CodeEditorHost.LuiWinUITextSetParentV1(CodeHost,SecondHost)==1 &&
                CodeEditorHost.LuiWinUITextSetBoundsV1(CodeHost,30,30,320,180)==1,
                "ProductionCodeEditorDockAndBounds");
            IntPtr CodeEditor=CodeEditorHost.LuiWinUITextEditorV1(CodeHost);
            Require(CodeEditorHost.SetSelection(CodeHost,0,0),"ProductionCodeEditorSelection");
            SendMessageW(CodeEditor,EmEmptyUndoBuffer,IntPtr.Zero,IntPtr.Zero);
            SendMessageW(CodeEditor,WmChar,new IntPtr('x'),IntPtr.Zero);
            Require(CodeEdits>0 && LastCodeEdit=="xA\U0001F600\nreturn" &&
                CodeEditorHost.ReadText(CodeHost)==LastCodeEdit,"ProductionCodeEditorNativeNotification");
            Require(CodeEditorHost.SetStyles(CodeHost,"1;1,3,16711680") &&
                SendMessageW(CodeEditor,EmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "ProductionCodeEditorUndoSafeStyle");
            Require(SendMessageW(CodeEditor,EmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero &&
                CodeEditorHost.ReadText(CodeHost)=="A\U0001F600\nreturn","ProductionCodeEditorNativeUndo");
            Require(SendMessageW(CodeEditor,EmCanRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero &&
                CodeEditorHost.SetStyles(CodeHost,"1;0,2,255") &&
                SendMessageW(CodeEditor,EmCanRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "ProductionCodeEditorRedoSurvivesStyle");
            Require(SendMessageW(CodeEditor,EmRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero &&
                CodeEditorHost.ReadText(CodeHost)=="xA\U0001F600\nreturn",
                "ProductionCodeEditorNativeRedo");
            Require(CodeEditorHost.LuiWinUITextSetParentV1(CodeHost,IntPtr.Zero)==1 &&
                IsWindow(CodeHost) && CodeEditorHost.ReadText(CodeHost)=="xA\U0001F600\nreturn" &&
                CodeEditorHost.LuiWinUITextSetParentV1(CodeHost,SecondHost)==1,
                "ProductionCodeEditorParkAndRedock");
            Passed.Add("Production child host delivers native edit notifications and retains text, Undo and Redo through styling and parking");
            Require(LuiWinUITextTestSuppressedImeEndV1(CodeHost)==1 &&
                CodeEditorHost.ReadText(CodeHost)=="xA\U0001F600\nreturn" &&
                SendMessageW(CodeEditor,EmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "SuppressedImeEndRetiresNativeComposition");
            Passed.Add("Synthetic IME end retires native composition during suppressed notifications without changing text or Undo");
            Require(LuiWinUITextSetShortcutProbeV1(CodeHost,1)==1 &&
                CodeEditorHost.LuiWinUITextFocusV1(CodeHost)==1,"PostDefaultKeyProbeEnabled");
            SendMessageW(CodeEditor,WmKeyDown,new IntPtr(120),IntPtr.Zero);
            Require(KeyProbeSequence=="before;after;","PostDefaultKeyProbeOrder");
            Require(LuiWinUITextSetShortcutProbeV1(CodeHost,0)==1,"PostDefaultKeyProbeDisabled");
            KeyProbeSequence=string.Empty;
            SendMessageW(CodeEditor,WmKeyDown,new IntPtr(120),IntPtr.Zero);
            Require(KeyProbeSequence=="before;","DisabledKeyProbeDoesNotForward");
            Passed.Add("Private F9 probe observes native default ordering and never emits a public Window shortcut");
            string FullSource=new('a',262144);
            Require(CodeEditorHost.LuiWinUITextSetLimitV1(CodeHost,262144)==1 &&
                CodeEditorHost.LuiWinUITextSetTextV1(CodeHost,FullSource)==1 &&
                CodeEditorHost.ReadText(CodeHost)==FullSource &&
                CodeEditorHost.SetSelection(CodeHost,262144,0) &&
                CodeEditorHost.SetStyles(CodeHost,"1;262143,262144,1193046"),
                "FullSourceTailStyleAndSelection");
            SendMessageW(CodeEditor,EmEmptyUndoBuffer,IntPtr.Zero,IntPtr.Zero);
            SendMessageW(CodeEditor,WmChar,new IntPtr('x'),IntPtr.Zero);
            Require(CodeEditorHost.ReadText(CodeHost)==FullSource &&
                SendMessageW(CodeEditor,EmCanUndo,IntPtr.Zero,IntPtr.Zero)==IntPtr.Zero,
                "FullSourceRejectedAppendPreservesUndo");
            Require(CodeEditorHost.SetSelection(CodeHost,262143,1),"FullSourceReplacementSelection");
            SendMessageW(CodeEditor,WmChar,new IntPtr('z'),IntPtr.Zero);
            Require(CodeEditorHost.ReadText(CodeHost)==new string('a',262143)+"z" &&
                SendMessageW(CodeEditor,EmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "FullSourceNativeReplacement");
            Require(SendMessageW(CodeEditor,EmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero &&
                CodeEditorHost.ReadText(CodeHost)==FullSource,
                "FullSourceNativeUndo");
            Passed.Add("Full 256 KiB native source, tail style, rejected append, replacement and Undo share one Rich Edit buffer");
            Success=true;
        }
        catch(Exception Error)
        {
            Failure=Error.ToString();
            LuiDiagnostics.Error("NativeTextHost","Qualification failed: " + Error.GetType().Name);
        }
        finally
        {
            if(CodeHost!=IntPtr.Zero) CodeEditorHost.LuiWinUITextDestroyHostV1(CodeHost);
            if(Editor!=IntPtr.Zero)
            {
                if(IsWindow(Editor)) DestroyWindow(Editor);
                if(IsWindow(Editor)) {Success=false; Failure="RichEditChildRetirement";}
            }
            Window?.Close();
            SecondWindow?.Close();
            if(Module!=IntPtr.Zero) FreeLibrary(Module);
        }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
        await File.WriteAllTextAsync(ReportPath,JsonSerializer.Serialize(new { Passed=Success, Groups=Passed, OwnerThread, BeforeChildClasses, AfterChildClasses, TopChildClass, EditorVisible, EditorHitTest, HitWindowClass, BatchP50Us, BatchP95Us, BatchP99Us, EditToStyleP50Us, EditToStyleP95Us, EditToStyleP99Us, BatchUtf16Units, Failure },new JsonSerializerOptions {WriteIndented=true}));
        return Success;
    }
}
