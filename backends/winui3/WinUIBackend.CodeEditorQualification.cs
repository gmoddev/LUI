using System.Runtime.InteropServices;
using System.Text.Json;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Input;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    private const uint CodeWmChar=0x0102;
    private const uint CodeEmCanUndo=0x00C6;
    private const uint CodeEmUndo=0x00C7;
    private const uint CodeEmCanRedo=0x0455;
    private const uint CodeEmRedo=0x0454;
    private const uint CodeEmReplaceSel=0x00C2;
    private const uint CodeWmImeStart=0x010D;
    private const uint CodeWmImeEnd=0x010E;
    private const uint CodeScsSetStr=0x0009;
    private const uint CodeNiCompositionStr=0x0015;
    private const uint CodeCpsComplete=0x0001;
    private const uint CodeGcsCompStr=0x0008;
    private const int CodeSwMinimize=6,CodeSwRestore=9;

    [StructLayout(LayoutKind.Sequential)]
    private struct CodeTsfProfileResult
    {
        internal uint StructSize;
        internal uint OwnerThread,FocusedBefore,FocusedAfter,LayoutStable;
        internal uint CreateResult,ProfileBeforeResult,ActivateResult,ProfileAfterResult;
        internal uint ActiveBeforeResult,ActiveAfterResult;
        internal uint ProfileBeforeFlags,ProfileAfterFlags;
        internal uint ActiveBeforeFlags,ActiveAfterFlags;
        internal uint ActiveBeforeLanguage,ActiveAfterLanguage;
        internal uint ActiveBeforeClass,ActiveAfterClass;
        internal uint ActiveBeforeProfile,ActiveAfterProfile;
        internal uint JapaneseActiveAfter;
    }

    [DllImport("TsfRichEditUiProbe.dll",CallingConvention=CallingConvention.Cdecl)]
    private static extern int LuiProbeTsfOnRichEditUiThread(IntPtr Editor,
        ref CodeTsfProfileResult Result);

    [DllImport("user32.dll",EntryPoint="SendMessageW")]
    private static extern IntPtr CodeSendMessage(IntPtr Window,uint Message,IntPtr WParam,IntPtr LParam);
    [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)]
    private static extern IntPtr CodeSendMessageText(IntPtr Window,uint Message,IntPtr WParam,string Text);
    [DllImport("user32.dll",EntryPoint="IsWindow")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsCodeWindow(IntPtr Window);
    [DllImport("user32.dll",EntryPoint="IsWindowVisible")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsCodeWindowVisible(IntPtr Window);
    [DllImport("user32.dll",EntryPoint="GetFocus")]
    private static extern IntPtr GetCodeFocus();
    [StructLayout(LayoutKind.Sequential)]
    private struct CodeMouseInput {internal int X,Y;internal uint Data,Flags,Time;internal UIntPtr Extra;}
    [StructLayout(LayoutKind.Explicit,Size=32)]
    private struct CodeInputData {[FieldOffset(0)] internal CodeMouseInput Mouse;}
    [StructLayout(LayoutKind.Sequential)]
    private struct CodeNativeInput {internal uint Type;internal CodeInputData Data;}
    [DllImport("user32.dll",EntryPoint="SendInput",SetLastError=true)]
    private static extern uint SendCodeInput(uint Count,CodeNativeInput[] Inputs,int Size);
    [DllImport("user32.dll",EntryPoint="SetCursorPos")]
    [return:MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetCodeCursor(int X,int Y);
    [DllImport("user32.dll",EntryPoint="GetCursorPos")]
    [return:MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetCodeCursor(out CodePoint Point);
    [DllImport("user32.dll",EntryPoint="SetForegroundWindow")]
    [return:MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetCodeForeground(IntPtr Window);
    [DllImport("user32.dll",EntryPoint="GetSystemMetrics")]
    private static extern int CodeSystemMetric(int Index);
    private static void CodeMouseButton(bool Up) {
        CodeNativeInput Input=new() {Data=new() {Mouse=new() {Flags=Up?4u:2u}}};
        FrameRequire(SendCodeInput(1,[Input],Marshal.SizeOf<CodeNativeInput>())==1,"CodePointerNativeAdmission");
    }
    private static void CodeMouseMove(int X,int Y) {
        int Width=CodeSystemMetric(78),Height=CodeSystemMetric(79);
        FrameRequire(Width>1 && Height>1,"CodePointerVirtualDesktop");
        // Absolute virtual-desktop motion avoids pointer acceleration; the
        // caller converts logical fixture positions to physical screen pixels.
        CodeNativeInput Input=new() {Data=new() {Mouse=new() {
            X=(int)Math.Round((X-CodeSystemMetric(76))*65535.0/(Width-1)),
            Y=(int)Math.Round((Y-CodeSystemMetric(77))*65535.0/(Height-1)),Flags=0xC001}}};
        FrameRequire(SendCodeInput(1,[Input],Marshal.SizeOf<CodeNativeInput>())==1,"CodePointerNativeMoveAdmission");
    }
    [DllImport("user32.dll",EntryPoint="IsWindowEnabled")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsCodeWindowEnabled(IntPtr Window);
    [DllImport("user32.dll",EntryPoint="ShowWindow")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CodeShowWindow(IntPtr Window,int Command);
    [DllImport("gdi32.dll",EntryPoint="CreateRectRgn")]
    private static extern IntPtr CodeCreateRectRgn(int Left,int Top,int Right,int Bottom);
    [DllImport("gdi32.dll",EntryPoint="PtInRegion")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CodePtInRegion(IntPtr Region,int X,int Y);
    [DllImport("gdi32.dll",EntryPoint="DeleteObject")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CodeDeleteObject(IntPtr Object);
    [DllImport("user32.dll",EntryPoint="GetWindowRgn")]
    private static extern int CodeGetWindowRgn(IntPtr Window,IntPtr Region);
    [DllImport("user32.dll",EntryPoint="GetGuiResources")]
    private static extern uint CodeGetGuiResources(IntPtr Process,uint Flags);
    [DllImport("imm32.dll",EntryPoint="ImmGetContext")]
    private static extern IntPtr CodeImmGetContext(IntPtr Window);
    [DllImport("imm32.dll",EntryPoint="ImmReleaseContext")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CodeImmReleaseContext(IntPtr Window,IntPtr Context);
    [DllImport("imm32.dll",EntryPoint="ImmSetCompositionStringW",SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CodeImmSetCompositionString(IntPtr Context,uint Index,IntPtr Composition,uint CompositionBytes,IntPtr Reading,uint ReadingBytes);
    [DllImport("imm32.dll",EntryPoint="ImmNotifyIME",SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CodeImmNotifyIme(IntPtr Context,uint Action,uint Index,uint Value);
    [DllImport("imm32.dll",EntryPoint="ImmGetCompositionStringW")]
    private static extern int CodeImmGetCompositionString(IntPtr Context,uint Index,IntPtr Buffer,uint Length);

    [StructLayout(LayoutKind.Sequential)]
    private struct CodeSelection {internal int Start,End;}
    private const uint CodeEmExGetSel=0x0434;
    private const uint CodeEmGetScrollPos=0x04DD;
    [DllImport("user32.dll",EntryPoint="SendMessageW")]
    private static extern IntPtr CodeSendMessageSelection(IntPtr Window,uint Message,IntPtr WParam,ref CodeSelection Selection);
    [DllImport("user32.dll",EntryPoint="SendMessageW")]
    private static extern IntPtr CodeSendMessagePoint(IntPtr Window,uint Message,IntPtr WParam,ref CodePoint Point);

    public async Task<bool> RunCodeEditorObservationAsync(string ReportPath)
    {
        string? Failure=null;
        string? InitialText=null,FinalText=null,RuntimeText=null,SignalSummary=null;
        int InitialSelectionStart=0,InitialSelectionEnd=0,FinalSelectionStart=0,FinalSelectionEnd=0;
        int RuntimeSelectionStart=0,RuntimeSelectionLength=0;
        int OverlayClicks=0,AutomaticOverlayClicks=0;
        uint GdiBefore=0,GdiAfter=0;
        bool HostRetired=false,ClipApplied=false,AutomaticMaskApplied=false,AutomaticCutout=false;
        bool MovedMaskApplied=false,OwnerMaskApplied=false,RecoveredMaskApplied=false;
        bool NativePopupVisible=false,NativeEditorEnabled=false;
        string? AutomaticMaskReason=null,OwnerMaskReason=null,RecoveryReason=null;
        object? ImeProfileProbe=null;
        string MaskCase=Environment.GetEnvironmentVariable("LUI_CODE_MASK_CASE") ?? "opaque";
        IntPtr Host=IntPtr.Zero;
        try
        {
            FrameRequire(MaskCase is "opaque" or "unknown" or "partial" or "move" or "owner" or
                "overflow" or "full" or "hiddenparent" or "parentzero" or "parenthalf" or
                "ownzero" or "ownhalf" or "ownbackgroundhalf" or "clipped" or "disabled",
                "ObservationMaskCase");
            FrameRun("""
                W=Instance.new('Window',{Title='LUI CodeEditor live input',Visible=true,Size=UDim2.fromOffset(720,440)})
                Editor=Instance.new('CodeEditor',{Parent=W,Text='local value = 42\nreturn value',Position=UDim2.fromOffset(24,58),Size=UDim2.fromOffset(620,300)})
                Caption=Instance.new('TextLabel',{Parent=W,Name='ObservationSummary',Text='Native Rich Edit: type, select, drag, compose',Position=UDim2.fromOffset(24,16),Size=UDim2.fromOffset(620,32)})
                OverlayClicks=0;AutomaticOverlayClicks=0
                OverlayBackdrop=Instance.new('Frame',{Parent=W,Position=UDim2.fromOffset(120,130),Size=UDim2.fromOffset(310,34),ZIndex=20,BackgroundColor3=Color3.fromRGB(15,85,180),BackgroundTransparency=0,BorderSizePixel=0,CornerRadius=0})
                Overlay=Instance.new('TextButton',{Parent=OverlayBackdrop,Text='XAML BUTTON ABOVE EDITOR',Position=UDim2.fromOffset(0,0),Size=UDim2.fromOffset(310,34),ZIndex=21})
                Overlay.Activated:Connect(function() OverlayClicks+=1 end)
                Changes=0;Starts=0;Ends=0;Preedits=0
                Editor.TextChanged:Connect(function() Changes+=1 end)
                Editor.CompositionStarted:Connect(function() Starts+=1 end)
                Editor.CompositionChanged:Connect(function() Preedits+=1 end)
                Editor.CompositionEnded:Connect(function() Ends+=1 end)
                assert(not Editor:SetStyles(Editor.TextRevision,{{Start=0,End=5,Color=0xD12A44},{Start=6,End=11,Color=0x175AC8}}))
                assert(Editor:Focus())
                """);
            Pump();
            var Entry=Views.Single(Pair=>Pair.Value.ClassName=="CodeEditor");
            View Target=Entry.Value;
            Host=Target.CodeHost;
            IntPtr EditorWindow=CodeEditorHost.LuiWinUITextEditorV1(Host);
            FrameRequire(Host!=IntPtr.Zero && EditorWindow!=IntPtr.Zero,"ObservationHost");
            if(Environment.GetEnvironmentVariable("LUI_CODE_IME_PROFILE_PROBE")=="1")
            {
                // Let the disposable owner Window become foreground before
                // querying the current UI thread's focused text service.
                await Task.Delay(2000);
                Pump();
                CodeTsfProfileResult Profile=new() {StructSize=(uint)Marshal.SizeOf<CodeTsfProfileResult>()};
                int ProbeCall=LuiProbeTsfOnRichEditUiThread(EditorWindow,ref Profile);
                ImeProfileProbe=new {
                    ProbeCall,Profile.OwnerThread,Profile.FocusedBefore,Profile.FocusedAfter,
                    Profile.LayoutStable,Profile.CreateResult,Profile.ProfileBeforeResult,
                    Profile.ActivateResult,Profile.ProfileAfterResult,Profile.ActiveBeforeResult,
                    Profile.ActiveAfterResult,Profile.ProfileBeforeFlags,Profile.ProfileAfterFlags,
                    Profile.ActiveBeforeFlags,Profile.ActiveAfterFlags,
                    Profile.ActiveBeforeLanguage,Profile.ActiveAfterLanguage,
                    Profile.ActiveBeforeClass,Profile.ActiveAfterClass,
                    Profile.ActiveBeforeProfile,Profile.ActiveAfterProfile,Profile.JapaneseActiveAfter
                };
                FrameRequire(ProbeCall==1 && Profile.OwnerThread==1 && Profile.FocusedBefore==1,
                    "ObservationProcessOnlyImeProfileProbe");
            }
            InitialText=CodeEditorHost.ReadText(Host);
            CodeSelection Selection=new();
            CodeSendMessageSelection(EditorWindow,CodeEmExGetSel,IntPtr.Zero,ref Selection);
            InitialSelectionStart=Selection.Start;InitialSelectionEnd=Selection.End;
            if(MaskCase=="unknown") {FrameRun("OverlayBackdrop.BackgroundTransparency=0.5");Pump();}
            if(MaskCase=="partial") {FrameRun("Editor.Position=UDim2.fromOffset(24.5,58)");Pump();}
            if(MaskCase=="ownzero") {FrameRun("Editor.Opacity=0");Pump();}
            if(MaskCase=="ownhalf") {FrameRun("Editor.Opacity=0.5");Pump();}
            if(MaskCase=="ownbackgroundhalf") {FrameRun("Editor.BackgroundTransparency=0.5");Pump();}
            if(MaskCase=="disabled") {FrameRun("Editor.Enabled=false");Pump();}
            if(MaskCase is "hiddenparent" or "parentzero" or "parenthalf" or "clipped")
            {
                FrameRun("Wrapper=Instance.new('Frame',{Parent=W,Position=UDim2.fromOffset(0,0),Size=UDim2.fromOffset(720,440),BackgroundTransparency=1,BorderSizePixel=0});Editor.Parent=Wrapper");
                if(MaskCase=="hiddenparent") FrameRun("Wrapper.Visible=false");
                if(MaskCase=="parentzero") FrameRun("Wrapper.Opacity=0");
                if(MaskCase=="parenthalf") FrameRun("Wrapper.Opacity=0.5");
                if(MaskCase=="clipped") FrameRun("Wrapper.ClipsDescendants=true;Wrapper.Size=UDim2.fromOffset(10,10)");
                Pump();
            }
            if(MaskCase=="full") {
                FrameRun("OverlayBackdrop.Position=UDim2.fromOffset(24,58);OverlayBackdrop.Size=UDim2.fromOffset(620,300)");
                Pump();
            }
            if(MaskCase=="overflow") {
                FrameRun("for Index=1,17 do Instance.new('Frame',{Parent=W,Position=UDim2.fromOffset(24+Index*14,250),Size=UDim2.fromOffset(8,8),ZIndex=30,BackgroundColor3=Color3.fromRGB(40,100,180),BackgroundTransparency=0,BorderSizePixel=0,CornerRadius=0}) end");
                Pump();
            }
            AutomaticMaskApplied=CodeMaskStates.TryGetValue(Target,out CodeMaskState? Mask) && Mask.Applied;
            AutomaticMaskReason=Mask?.Reason;
            NativePopupVisible=IsCodeWindowVisible(Host);
            NativeEditorEnabled=IsCodeWindowEnabled(EditorWindow);
            IntPtr AutomaticRegion=CodeCreateRectRgn(0,0,0,0);
            FrameRequire(AutomaticRegion!=IntPtr.Zero,"ObservationAutomaticRegionCreated");
            try {AutomaticCutout=CodeGetWindowRgn(Host,AutomaticRegion)>0 &&
                CodePtInRegion(AutomaticRegion,5,5) && !CodePtInRegion(AutomaticRegion,150,85);}
            finally {CodeDeleteObject(AutomaticRegion);}
            if(MaskCase is "opaque" or "move" or "owner" or "disabled")
                FrameRequire(AutomaticMaskApplied && AutomaticCutout && NativePopupVisible &&
                    (MaskCase!="disabled" || !NativeEditorEnabled),
                    "ObservationAutomaticOpaqueCutout:"+(AutomaticMaskReason??"missing"));
            else if(MaskCase is "full" or "hiddenparent" or "parentzero" or "parenthalf" or
                "ownzero" or "ownhalf" or "ownbackgroundhalf" or "clipped")
                FrameRequire(!AutomaticMaskApplied && !NativePopupVisible &&
                    !string.IsNullOrEmpty(AutomaticMaskReason),
                    "ObservationNativePopupHidden:"+(AutomaticMaskReason??"missing"));
            else
                FrameRequire(!AutomaticMaskApplied && !AutomaticCutout && NativePopupVisible &&
                    !string.IsNullOrEmpty(AutomaticMaskReason),
                    "ObservationUnsupportedIntersectionReported:"+(AutomaticMaskReason??"missing"));
            if(MaskCase is "full" or "hiddenparent" or "parentzero" or "parenthalf" or
                "ownzero" or "ownhalf" or "ownbackgroundhalf" or "clipped")
            {
                if(MaskCase=="full") FrameRun("OverlayBackdrop.Position=UDim2.fromOffset(120,130);OverlayBackdrop.Size=UDim2.fromOffset(310,34)");
                if(MaskCase=="hiddenparent") FrameRun("Wrapper.Visible=true");
                if(MaskCase is "parentzero" or "parenthalf") FrameRun("Wrapper.Opacity=1");
                if(MaskCase is "ownzero" or "ownhalf") FrameRun("Editor.Opacity=1");
                if(MaskCase=="ownbackgroundhalf") FrameRun("Editor.BackgroundTransparency=0");
                if(MaskCase=="clipped") FrameRun("Wrapper.Size=UDim2.fromOffset(720,440)");
                Pump();
                CodeMaskState? Recovered=CodeMaskStates.TryGetValue(Target,out CodeMaskState? Found)?Found:null;
                RecoveryReason=Recovered?.Reason;
                IntPtr RecoveryRegion=CodeCreateRectRgn(0,0,0,0);
                FrameRequire(RecoveryRegion!=IntPtr.Zero,"ObservationRecoveryRegionCreated");
                try {RecoveredMaskApplied=Recovered?.Applied==true && IsCodeWindowVisible(Host) &&
                    CodeGetWindowRgn(Host,RecoveryRegion)>0 &&
                    CodePtInRegion(RecoveryRegion,5,5) && !CodePtInRegion(RecoveryRegion,150,85);}
                finally {CodeDeleteObject(RecoveryRegion);}
                FrameRequire(RecoveredMaskApplied,"ObservationRecoveredOpaqueCutout:"+(RecoveryReason??"missing"));
            }
            if(MaskCase=="move")
            {
                ulong BeforeRevision=Mask?.Revision ?? 0;
                FrameRun("OverlayBackdrop.Position=UDim2.fromOffset(200,180)");Pump();
                CodeMaskState? Moved=CodeMaskStates.TryGetValue(Target,out CodeMaskState? Found)?Found:null;
                IntPtr MovedRegion=CodeCreateRectRgn(0,0,0,0);
                FrameRequire(MovedRegion!=IntPtr.Zero,"ObservationMovedRegionCreated");
                try {MovedMaskApplied=Moved?.Applied==true && Moved.Revision!=BeforeRevision &&
                    CodeGetWindowRgn(Host,MovedRegion)>0 && CodePtInRegion(MovedRegion,150,85) &&
                    !CodePtInRegion(MovedRegion,250,135);}
                finally {CodeDeleteObject(MovedRegion);}
                FrameRequire(MovedMaskApplied,"ObservationMovedOpaqueCutout");
            }
            if(MaskCase=="owner")
            {
                int OldWindowId=Target.CodeWindowId;
                FrameRun("Second=Instance.new('Window',{Title='LUI CodeEditor second owner',Visible=true,Size=UDim2.fromOffset(720,440)});Editor.Parent=Second;OverlayBackdrop.Parent=Second");
                Pump();
                CodeMaskState? Reowned=CodeMaskStates.TryGetValue(Target,out CodeMaskState? Found)?Found:null;
                OwnerMaskReason=Reowned?.Reason;
                IntPtr OwnerRegion=CodeCreateRectRgn(0,0,0,0);
                FrameRequire(OwnerRegion!=IntPtr.Zero,"ObservationOwnerRegionCreated");
                try {OwnerMaskApplied=Target.CodeWindowId!=OldWindowId &&
                    !CodePresentationCaches.ContainsKey(OldWindowId) && Reowned?.Applied==true &&
                    CodeGetWindowRgn(Host,OwnerRegion)>0 &&
                    CodePtInRegion(OwnerRegion,5,5) && !CodePtInRegion(OwnerRegion,150,85);}
                finally {CodeDeleteObject(OwnerRegion);}
                FrameRequire(OwnerMaskApplied,"ObservationReownedOpaqueCutout:"+(OwnerMaskReason??"missing"));
            }
            // The observation window remains interactive while the dispatcher pumps.
            int Samples=MaskCase=="opaque"?100:12;
            for(int Index=0;Index<Samples && IsCodeWindow(Host);++Index)
            {
                await Task.Delay(500);
                Pump();
                if(Index==59)
                {
                    FrameRun("AutomaticOverlayClicks=OverlayClicks");
                    CodeEditorHost.ClipRect[] Holes=[new() {Left=96,Top=72,Right=406,Bottom=106}];
                    using(Process Current=Process.GetCurrentProcess())
                    {
                        GdiBefore=CodeGetGuiResources(Current.Handle,0);
                        for(int Repeat=0;Repeat<128;++Repeat)
                            FrameRequire(CodeEditorHost.LuiWinUITextSetClipV2(Host,0,0,620,300,Holes,1)==1,
                                "ObservationOpaqueCutout");
                        GdiAfter=CodeGetGuiResources(Current.Handle,0);
                    }
                    FrameRequire(GdiBefore>0 && GdiAfter>0 && GdiAfter<=GdiBefore+8,
                        "ObservationCutoutGdiLifetime");
                    FrameRequire(CodeEditorHost.LuiWinUITextSetClipV2(Host,0,0,620,300,
                        [new() {Left=4,Top=4,Right=3,Bottom=5}],1)==0 &&
                        CodeEditorHost.LuiWinUITextSetClipV2(Host,0,0,620,300,new CodeEditorHost.ClipRect[17],17)==0 &&
                        CodeEditorHost.LuiWinUITextSetClipV2(Host,-1000001,0,620,300,Holes,1)==0,
                        "ObservationCutoutAdmission");
                    IntPtr Region=CodeCreateRectRgn(0,0,0,0);
                    FrameRequire(Region!=IntPtr.Zero,"ObservationRegionReadback");
                    try {FrameRequire(CodeGetWindowRgn(Host,Region)>0 &&
                        CodePtInRegion(Region,5,5) && !CodePtInRegion(Region,150,85),
                        "ObservationCutoutRegion");}
                    finally {CodeDeleteObject(Region);}
                    ClipApplied=true;
                }
                if(Index==79) {FrameRun("Editor.Visible=false");Pump();}
            }
            FinalText=CodeEditorHost.ReadText(Host);
            CodeSendMessageSelection(EditorWindow,CodeEmExGetSel,IntPtr.Zero,ref Selection);
            FinalSelectionStart=Selection.Start;FinalSelectionEnd=Selection.End;
            FrameRun("assert(Starts>=Ends);Caption.Text=string.format('Changes=%d Starts=%d Preedits=%d Ends=%d Selection=%d,%d AutoOverlayClicks=%d OverlayClicks=%d',Changes,Starts,Preedits,Ends,Editor.SelectionStart,Editor.SelectionLength,AutomaticOverlayClicks,OverlayClicks)");
            Pump();
            string? Snapshot=Marshal.PtrToStringUTF8(Native.Lui_GetPreviewTreeJson(Runtime));
            FrameRequire(Snapshot is not null,"ObservationRuntimeSnapshot");
            using(JsonDocument Tree=JsonDocument.Parse(Snapshot!))
            {
                RuntimeText=Tree.RootElement.EnumerateArray().Single(Node=>Node.GetProperty("id").GetInt32()==Entry.Key).GetProperty("text").GetString();
                SignalSummary=Tree.RootElement.EnumerateArray().Single(Node=>Node.GetProperty("name").GetString()=="ObservationSummary").GetProperty("text").GetString();
            }
            string[] SelectionFields=SignalSummary!.Split("Selection=",StringSplitOptions.None);
            FrameRequire(SelectionFields.Length==2,"ObservationSelectionSummary");
            string[] Coordinates=SelectionFields[1].Split(',');
            RuntimeSelectionStart=int.Parse(Coordinates[0],System.Globalization.CultureInfo.InvariantCulture);
            RuntimeSelectionLength=int.Parse(Coordinates[1].Split(' ')[0],System.Globalization.CultureInfo.InvariantCulture);
            AutomaticOverlayClicks=int.Parse(SignalSummary.Split("AutoOverlayClicks=")[1].Split(' ')[0],
                System.Globalization.CultureInfo.InvariantCulture);
            OverlayClicks=int.Parse(SignalSummary.Split("OverlayClicks=")[^1],System.Globalization.CultureInfo.InvariantCulture);
            FrameRequire(FinalText is not null && RuntimeText==FinalText,"ObservationSingleTextAuthority");
            int[] ScalarSelection=[RuntimeSelectionStart,RuntimeSelectionStart+RuntimeSelectionLength];
            int[] NativeSelection=new int[2];
            FrameRequire(CanonicalTextOffsets.TryMapScalarBoundaries(FinalText!,ScalarSelection,NativeSelection) &&
                NativeSelection[0]==FinalSelectionStart && NativeSelection[1]==FinalSelectionEnd,
                "ObservationSelectionCoherence");
            FrameRun("Editor:Destroy();W:Destroy();if Second then Second:Destroy() end");
            Pump();
            HostRetired=!IsCodeWindow(Host);
            FrameRequire(HostRetired,"ObservationHostRetirement");
        }
        catch(Exception Error)
        {
            Failure=Error is FrameQualificationFailure Failed?Failed.Check:Error.ToString();
            LuiDiagnostics.Error("CodeEditor","Observation: "+Failure);
        }
        finally
        {
            try {Native.Lui_RunScript(Runtime,"if Editor then Editor:Destroy() end;if W then W:Destroy() end;if Second then Second:Destroy() end","ObservationCleanup");Pump();}
            catch(Exception Error) {LuiDiagnostics.Error("CodeEditor","Observation cleanup: "+Error.GetType().Name);}
        }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
        await File.WriteAllTextAsync(ReportPath,JsonSerializer.Serialize(new {Passed=Failure is null,MaskCase,InitialText,FinalText,RuntimeText,SignalSummary,InitialSelectionStart,InitialSelectionEnd,FinalSelectionStart,FinalSelectionEnd,RuntimeSelectionStart,RuntimeSelectionLength,AutomaticOverlayClicks,OverlayClicks,ClipApplied,AutomaticMaskApplied,AutomaticMaskReason,AutomaticCutout,NativePopupVisible,NativeEditorEnabled,RecoveredMaskApplied,RecoveryReason,MovedMaskApplied,OwnerMaskApplied,OwnerMaskReason,GdiBefore,GdiAfter,HostRetired,ImeProfileProbe,Failure},new JsonSerializerOptions {WriteIndented=true}));
        return Failure is null;
    }

    public async Task<bool> RunCodeEditorPerformanceAsync(string ReportPath)
    {
        const int SourceLength=32768,RangeCount=128,SampleCount=128;
        string? Failure=null;
        double[] LatencyMicroseconds=new double[SampleCount],BaselineLatencyMicroseconds=new double[SampleCount];
        long[] ManagedBytes=new long[SampleCount],BaselineManagedBytes=new long[SampleCount];
        long InitialPrivateBytes=0,BaselinePrivateBytes=0,FinalPrivateBytes=0;
        long InitialManagedLiveBytes=0,BaselineManagedLiveBytes=0,FinalManagedLiveBytes=0;
        string? FinalNativeText=null,FinalRuntimeText=null;
        bool HostRetired=false;
        IntPtr Host=IntPtr.Zero;
        try
        {
            FrameRun("""
                W=Instance.new('Window',{Title='LUI CodeEditor performance',Visible=true,Size=UDim2.fromOffset(680,360)})
                Ranges={}
                for Index=0,127 do Ranges[Index+1]={Start=Index*256,End=Index*256+4,Color=0xA43218} end
                Editor=Instance.new('CodeEditor',{Parent=W,Text=string.rep('a',32768),Position=UDim2.fromOffset(16,24),Size=UDim2.fromOffset(630,290)})
                PerfChanges=0;PerfStyleEnabled=false
                Editor.TextChanged:Connect(function()
                    PerfChanges+=1
                    if PerfStyleEnabled then assert(not Editor:SetStyles(Editor.TextRevision,Ranges)) end
                end)
                Editor.SelectionStart=32768
                Editor.SelectionLength=0
                assert(Editor:Focus() and #Ranges==128)
                """);
            Pump();
            var Entry=Views.Single(Pair=>Pair.Value.ClassName=="CodeEditor");
            Host=Entry.Value.CodeHost;
            IntPtr EditorWindow=CodeEditorHost.LuiWinUITextEditorV1(Host);
            FrameRequire(Host!=IntPtr.Zero && EditorWindow!=IntPtr.Zero &&
                CodeEditorHost.ReadText(Host)?.Length==SourceLength,"PerformanceSingleNativeBuffer");
            InitialPrivateBytes=Process.GetCurrentProcess().PrivateMemorySize64;
            InitialManagedLiveBytes=GC.GetTotalMemory(true);
            void RunSamples(double[] Latencies,long[] Allocations)
            {
                for(int Index=0;Index<SampleCount;++Index)
                {
                    long BeforeBytes=GC.GetAllocatedBytesForCurrentThread();
                    long Before=Stopwatch.GetTimestamp();
                    CodeSendMessage(EditorWindow,CodeWmChar,new IntPtr('x'),IntPtr.Zero);
                    Pump();
                    long After=Stopwatch.GetTimestamp();
                    Allocations[Index]=GC.GetAllocatedBytesForCurrentThread()-BeforeBytes;
                    Latencies[Index]=(After-Before)*1000000.0/Stopwatch.Frequency;
                }
            }
            RunSamples(BaselineLatencyMicroseconds,BaselineManagedBytes);
            BaselinePrivateBytes=Process.GetCurrentProcess().PrivateMemorySize64;
            BaselineManagedLiveBytes=GC.GetTotalMemory(true);
            FrameRun("PerfStyleEnabled=true");
            RunSamples(LatencyMicroseconds,ManagedBytes);
            FinalPrivateBytes=Process.GetCurrentProcess().PrivateMemorySize64;
            FinalManagedLiveBytes=GC.GetTotalMemory(true);
            FrameRun("assert(PerfChanges==256 and #Editor.Text==33024)");
            FinalNativeText=CodeEditorHost.ReadText(Host);
            string? Snapshot=Marshal.PtrToStringUTF8(Native.Lui_GetPreviewTreeJson(Runtime));
            FrameRequire(Snapshot is not null,"PerformanceRuntimeSnapshot");
            using(JsonDocument Tree=JsonDocument.Parse(Snapshot!))
                FinalRuntimeText=Tree.RootElement.EnumerateArray().Single(Node=>Node.GetProperty("id").GetInt32()==Entry.Key).GetProperty("text").GetString();
            FrameRequire(FinalNativeText is not null && FinalNativeText==FinalRuntimeText &&
                FinalNativeText.Length==SourceLength+2*SampleCount,"PerformanceSourcePreserved");
            FrameRun("Editor:Destroy();W:Destroy()");
            Pump();
            HostRetired=!IsCodeWindow(Host);
            FrameRequire(HostRetired,"PerformanceHostRetirement");
        }
        catch(Exception Error)
        {
            Failure=Error is FrameQualificationFailure Failed?Failed.Check:Error.ToString();
            LuiDiagnostics.Error("CodeEditor","Performance qualification: "+Failure);
        }
        finally
        {
            try {Native.Lui_RunScript(Runtime,"if Editor then Editor:Destroy() end;if W then W:Destroy() end","PerformanceCleanup");Pump();}
            catch(Exception Error) {LuiDiagnostics.Error("CodeEditor","Performance cleanup: "+Error.GetType().Name);}
        }
        Array.Sort(LatencyMicroseconds);Array.Sort(ManagedBytes);
        Array.Sort(BaselineLatencyMicroseconds);Array.Sort(BaselineManagedBytes);
        static double Percentile(double[] Samples,double Percent) => Samples[(int)Math.Ceiling(Percent*Samples.Length)-1];
        static long PercentileBytes(long[] Samples,double Percent) => Samples[(int)Math.Ceiling(Percent*Samples.Length)-1];
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
        await File.WriteAllTextAsync(ReportPath,JsonSerializer.Serialize(new {
            Passed=Failure is null,Failure,SourceLength,RangeCount,SampleCount,HostRetired,
            Boundary="Synchronous SendMessage(WM_CHAR) through owner Pump; baseline without style callback then 128-range style callback; excludes compositor paint and physical display",
            BaselineLatencyUs=new {P50=Percentile(BaselineLatencyMicroseconds,0.50),P95=Percentile(BaselineLatencyMicroseconds,0.95),P99=Percentile(BaselineLatencyMicroseconds,0.99)},
            StyledLatencyUs=new {P50=Percentile(LatencyMicroseconds,0.50),P95=Percentile(LatencyMicroseconds,0.95),P99=Percentile(LatencyMicroseconds,0.99)},
            BaselineManagedAllocationBytes=new {P50=PercentileBytes(BaselineManagedBytes,0.50),P95=PercentileBytes(BaselineManagedBytes,0.95),P99=PercentileBytes(BaselineManagedBytes,0.99)},
            StyledManagedAllocationBytes=new {P50=PercentileBytes(ManagedBytes,0.50),P95=PercentileBytes(ManagedBytes,0.95),P99=PercentileBytes(ManagedBytes,0.99)},
            InitialPrivateBytes,BaselinePrivateBytes,FinalPrivateBytes,
            InitialManagedLiveBytes,BaselineManagedLiveBytes,FinalManagedLiveBytes,
            FinalNativeLength=FinalNativeText?.Length,FinalRuntimeLength=FinalRuntimeText?.Length
        },new JsonSerializerOptions {WriteIndented=true}));
        return Failure is null;
    }

    public async Task<bool> RunCodeEditorQualificationAsync(string ReportPath)
    {
        List<string> Groups=new();
        List<object> PointerTrace=new();
        int OwnerThread=Environment.CurrentManagedThreadId;
        string? Failure=null;
        bool ImeContextAvailable=false,ImeSetStringAccepted=false,ImeCompleteAccepted=false;
        int ImeSetStringError=0;
        int ImeCompositionBytes=-1;
        string? ImeTextAfter=null,ImeRuntimeTextAfter=null,ImeSignalSummary=null;
        IntPtr Editor=IntPtr.Zero,Host=IntPtr.Zero;
        async Task Settle()
        {
            for(int Index=0;Index<5;++Index) {await Task.Delay(20); Pump();}
            FrameRequire(Environment.CurrentManagedThreadId==OwnerThread,"CodeEditorOwnerThread");
        }
        try
        {
            FrameRun("""
                assert(not app:GetService('PlatformService'):Supports('CodeEditorStyles'))
                W=Instance.new('Window',{Title='LUI CodeEditor runtime',Visible=true,Size=UDim2.fromOffset(480,320)})
                Editor=Instance.new('CodeEditor',{Parent=W,Text='A\u{1F600}\nreturn',Position=UDim2.fromOffset(20,30),Size=UDim2.fromOffset(400,220)})
                Changes=0
                Editor.Focused:Connect(function() Editor.Visible=true end)
                Editor.TextChanged:Connect(function()
                    Changes+=1
                    assert(not Editor:SetStyles(Editor.TextRevision,{{Start=0,End=1,Color=0xD12A44}}))
                end)
                assert(Editor:IsA('TextBox') and Editor.Multiline and Editor.TextRevision==2)
                """);
            await Settle();
            var Entry=Views.Single(Pair=>Pair.Value.ClassName=="CodeEditor");
            View Target=Entry.Value;
            Host=Target.CodeHost; Editor=CodeEditorHost.LuiWinUITextEditorV1(Host);
            FrameRequire(Host!=IntPtr.Zero && Editor!=IntPtr.Zero &&
                CodeEditorHost.ReadText(Host)=="A\U0001F600\nreturn" && Target.Element is null,
                "CodeEditorSingleNativeBuffer");
            Groups.Add("Runtime CodeEditor owns one native Rich Edit popup with canonical LF text");

            FrameRun("assert(Editor:Focus());Editor.SelectionStart=0;Editor.SelectionLength=0");
            await Settle();
            FrameRequire(GetCodeFocus()==Editor,"CodeEditorFocusCallbackPreservesNativeFocus");
            FrameRequire(CodeEditorHost.LuiWinUITextFocusV1(Host)==1,"CodeEditorNativeFocus");
            await Settle();
            FrameRequire(GetCodeFocus()==Editor,"CodeEditorFocusCallbackVisibility");
            FrameRun("Editor.Visible=true");
            await Settle();
            FrameRequire(GetCodeFocus()==Editor && IsCodeWindowVisible(Host),"CodeEditorRedundantVisibilityRetainsFocus");
            Groups.Add("Redundant visible updates preserve the focused native editor, including from Focused callbacks");
            CodeSendMessage(Editor,CodeWmChar,new IntPtr('x'),IntPtr.Zero);
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn' and Editor.TextRevision==3 and Changes==1)""");
            FrameRequire(CodeEditorHost.ReadText(Host)=="xA\U0001F600\nreturn" &&
                CodeSendMessage(Editor,CodeEmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorTypingAndStyleUndo");
            Groups.Add("Native character edit reaches Luau TextChanged and revision styled the same HWND");

            FrameRequire(CodeSendMessage(Editor,CodeEmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorNativeUndo");
            await Settle();
            FrameRun("""assert(Editor.Text=='A\u{1F600}\nreturn' and Editor.TextRevision==4 and Changes==2)""");
            FrameRequire(CodeSendMessage(Editor,CodeEmCanRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorRedoAfterListenerStyle");
            FrameRequire(CodeSendMessage(Editor,CodeEmRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorNativeRedo");
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn' and Editor.TextRevision==5 and Changes==3)""");
            Groups.Add("Native Undo and Redo remain available across runtime TextChanged styling");

            FrameRun("Editor.SelectionStart=10;Editor.SelectionLength=0");
            await Settle();
            CodeSendMessage(Editor,CodeWmChar,new IntPtr('!'),IntPtr.Zero);
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn!' and Editor.TextRevision==6 and Changes==4)""");
            FrameRequire(CodeSendMessage(Editor,CodeEmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorAfterNewlineUndo");
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn' and Editor.TextRevision==7 and Changes==5)""");
            Groups.Add("Typing after LF and surrogate pair maps native selection to canonical scalar positions");

            FrameRun("ImeStarts=0;ImeEnds=0;ImePreedits=0;Editor.CompositionStarted:Connect(function() ImeStarts+=1 end);"+
                "Editor.CompositionChanged:Connect(function() ImePreedits+=1 end);"+
                "Editor.CompositionEnded:Connect(function() ImeEnds+=1 end)");
            CodeSendMessage(Editor,CodeWmImeStart,IntPtr.Zero,IntPtr.Zero);
            await Settle();
            FrameRun("assert(Editor.IsComposing and ImeStarts==1 and ImeEnds==0)");
            CodeSendMessage(Editor,CodeWmImeEnd,IntPtr.Zero,IntPtr.Zero);
            await Settle();
            FrameRun("assert(not Editor.IsComposing and ImeStarts==1 and ImeEnds==1)");
            Groups.Add("Native IME start and end notifications route to Luau composition lifecycle");

            FrameRun("Editor.SelectionStart=0;Editor.SelectionLength=0");
            await Settle();
            CodeSendMessage(Editor,CodeWmImeStart,IntPtr.Zero,IntPtr.Zero);
            CodeSendMessageText(Editor,CodeEmReplaceSel,new IntPtr(1),"z");
            await Settle();
            FrameRun("""assert(Editor.IsComposing and Editor.Text=='xA\u{1F600}\nreturn' and Changes==5)""");
            FrameRequire(CodeEditorHost.ReadText(Host)=="zxA\U0001F600\nreturn",
                "CodeEditorPreeditRemainsNative");
            CodeSendMessage(Editor,CodeWmImeEnd,IntPtr.Zero,IntPtr.Zero);
            await Settle();
            FrameRun("""assert(not Editor.IsComposing and Editor.Text=='zxA\u{1F600}\nreturn' and Changes==6 and ImeEnds==2)""");
            FrameRequire(CodeSendMessage(Editor,CodeEmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorComposedEditUndo");
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn' and Changes==7)""");
            Groups.Add("IME-window text is deferred until composition end and native Undo restores it");

            FrameRun("PointerSequence='';PointerX=0;Editor.InputBegan:Connect(function(Event) "+
                "if Event.Device=='Mouse' then PointerSequence..=Event.Button..'+ ';PointerX=Event.Position.X end end);"+
                "Editor.InputEnded:Connect(function(Event) if Event.Device=='Mouse' then "+
                "PointerSequence..=Event.Button..'- ' end end)");
            foreach((int Down,int Up) in new[] {(10,11),(14,15),(16,17)}) {
                OnCodeEditorEvent(new IntPtr(Entry.Key),Down,IntPtr.Zero,15,25);
                OnCodeEditorEvent(new IntPtr(Entry.Key),Up,IntPtr.Zero,15,25);
            }
            await Settle();
            double Scale=Views[Target.CodeWindowId].Container?.XamlRoot?.RasterizationScale ?? 1;
            FrameRun("assert(PointerSequence=='Left+ Left- Right+ Right- Middle+ Middle- ' and "+
                "math.abs(PointerX-"+(15/Scale).ToString("R",System.Globalization.CultureInfo.InvariantCulture)+")<0.001)");
            Groups.Add("CodeEditor pointer bridge preserves left/right/middle buttons and logical coordinates");

            FrameRun("KeyStarts=0;Shortcuts=0;Editor.InputBegan:Connect(function(Event) "+
                "if Event.Device=='Keyboard' and Event.Key=='F1' then KeyStarts+=1 end end);"+
                "W.Shortcut:Connect(function() Shortcuts+=1 end)");
            OnCodeEditorEvent(new IntPtr(Entry.Key),4,IntPtr.Zero,112,0);
            OnCodeEditorEvent(new IntPtr(Entry.Key),5,IntPtr.Zero,112,0);
            await Settle();
            FrameRun("assert(KeyStarts==1 and Shortcuts==0)");
            Groups.Add("Native key events reach the editor without premature Window shortcut dispatch");

            FrameRun("Editor.MaxLength=20;Editor.SelectionStart=10;Editor.SelectionLength=0");
            await Settle();
            CodeSendMessageText(Editor,CodeEmReplaceSel,new IntPtr(1),"0123456789");
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn0123456789' and Editor.MaxLength==20)""");
            FrameRun("Editor.SelectionStart=10;Editor.SelectionLength=10");
            await Settle();
            CodeSendMessageText(Editor,CodeEmReplaceSel,new IntPtr(1),string.Empty);
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn' and not pcall(function() Editor.MaxLength=10 end) and Editor.MaxLength==20)""");
            FrameRequire(CodeEditorHost.LuiWinUITextSetLimitV1(Host,10)==0,
                "CodeEditorNativeLimitRetainsHistory");
            FrameRequire(CodeSendMessage(Editor,CodeEmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorLowerLimitUndo");
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn0123456789' and Editor.MaxLength==20)""");
            FrameRequire(CodeSendMessage(Editor,CodeEmRedo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorLowerLimitRedo");
            await Settle();
            FrameRun("""assert(Editor.Text=='xA\u{1F600}\nreturn' and Editor.MaxLength==20)""");
            Groups.Add("Lowering MaxLength after native edits is rejected so Undo and Redo stay admissible");

            if(Native.Lui_RunScript(Runtime,"""
                Second=Instance.new('Window',{Title='LUI CodeEditor redock',Visible=true,Size=UDim2.fromOffset(500,320)})
                Editor.Parent=Second
                ""","CodeEditorRedock")!=1) throw new InvalidOperationException("Redock: "+LastError);
            await Settle();
            FrameRequire(Target.CodeHost==Host && CodeEditorHost.LuiWinUITextEditorV1(Host)==Editor &&
                CodeEditorHost.ReadText(Host)=="xA\U0001F600\nreturn" &&
                CodeSendMessage(Editor,CodeEmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorRedockPreservesNativeEditor");
            if(Native.Lui_RunScript(Runtime,"local Ok,Reason=W:RequestClose();assert(Ok,'request:'..tostring(Reason))",
                "CodeEditorOldWindowClose")!=1) throw new InvalidOperationException("Old window close: "+LastError);
            await Settle();
            FrameRun("assert(W.IsClosed and Editor.Parent==Second)");
            FrameRequire(IsCodeWindow(Host) && Target.CodeHost==Host,"CodeEditorOldWindowRetirement");
            Groups.Add("Cross window redock and old window retirement retain native HWND and Undo");

            IntPtr ImeContext=CodeImmGetContext(Editor);
            ImeContextAvailable=ImeContext!=IntPtr.Zero;
            if(ImeContextAvailable)
            {
                IntPtr Composition=Marshal.StringToHGlobalUni("かな");
                try
                {
                    ImeSetStringAccepted=CodeImmSetCompositionString(ImeContext,CodeScsSetStr,
                        Composition,4,IntPtr.Zero,0);
                    if(!ImeSetStringAccepted) ImeSetStringError=Marshal.GetLastWin32Error();
                    if(ImeSetStringAccepted)
                    {
                        ImeCompositionBytes=CodeImmGetCompositionString(ImeContext,CodeGcsCompStr,IntPtr.Zero,0);
                        ImeCompleteAccepted=CodeImmNotifyIme(ImeContext,CodeNiCompositionStr,CodeCpsComplete,0);
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(Composition);
                    CodeImmReleaseContext(Editor,ImeContext);
                }
                await Settle();
                ImeTextAfter=CodeEditorHost.ReadText(Host);
                FrameRun("ImeProbe=Instance.new('TextLabel',{Parent=Second,Text=string.format('Starts=%d Preedits=%d Ends=%d Composing=%s',ImeStarts,ImePreedits,ImeEnds,tostring(Editor.IsComposing))})");
                Pump();
                string? Snapshot=Marshal.PtrToStringUTF8(Native.Lui_GetPreviewTreeJson(Runtime));
                FrameRequire(Snapshot is not null,"CodeEditorImeSnapshot");
                using(JsonDocument Tree=JsonDocument.Parse(Snapshot!))
                {
                    ImeRuntimeTextAfter=Tree.RootElement.EnumerateArray().Single(Node=>Node.GetProperty("id").GetInt32()==Entry.Key).GetProperty("text").GetString();
                    ImeSignalSummary=Tree.RootElement.EnumerateArray().Single(Node=>Node.GetProperty("className").GetString()=="TextLabel").GetProperty("text").GetString();
                }
            }
            Groups.Add("IMM process-local composition attempt recorded without changing keyboard profiles");

            FrameRequire(CodeEditorHost.LuiWinUITextSetClipV2(Host,0,0,400,220,
                [new() {Left=120,Top=80,Right=220,Bottom=120}],1)==1,
                "CodeEditorReownedCutout");
            IntPtr SecondOwner=WinRT.Interop.WindowNative.GetWindowHandle(
                Views.Values.Single(Value=>Value.ClassName=="Window" && Value.Window is not null && !Value.NativeClosed).Window!);
            string? BeforeMinimize=CodeEditorHost.ReadText(Host);
            FrameRequire(BeforeMinimize is not null,"CodeEditorSourceBeforeMinimize");
            CodeSelection BeforeSelection=new(),AfterSelection=new();
            CodePoint BeforeScroll=new(),AfterScroll=new();
            CodeSendMessageSelection(Editor,CodeEmExGetSel,IntPtr.Zero,ref BeforeSelection);
            CodeSendMessagePoint(Editor,CodeEmGetScrollPos,IntPtr.Zero,ref BeforeScroll);
            FrameRequire(IsCodeWindowVisible(Host),"CodeEditorVisibleBeforeMinimize");
            CodeShowWindow(SecondOwner,CodeSwMinimize);
            await Settle();
            FrameRequire(!IsCodeWindowVisible(Host),"CodeEditorPopupHiddenOnOwnerMinimize");
            CodeShowWindow(SecondOwner,CodeSwRestore);
            await Settle();
            CodeSendMessageSelection(Editor,CodeEmExGetSel,IntPtr.Zero,ref AfterSelection);
            CodeSendMessagePoint(Editor,CodeEmGetScrollPos,IntPtr.Zero,ref AfterScroll);
            FrameRequire(IsCodeWindowVisible(Host) && CodeEditorHost.LuiWinUITextEditorV1(Host)==Editor &&
                CodeEditorHost.ReadText(Host)==BeforeMinimize &&
                BeforeSelection.Start==AfterSelection.Start && BeforeSelection.End==AfterSelection.End &&
                BeforeScroll.X==AfterScroll.X && BeforeScroll.Y==AfterScroll.Y &&
                CodeSendMessage(Editor,CodeEmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorPopupRestoredWithSource");
            Groups.Add("Fixture manually applies V2 cutout after redock; owner minimize and restore preserve editor HWND, text, selection, scroll and Undo");

            FrameRun("Editor.MaxLength=262144;Editor.Text=string.rep('a',262144);"+
                "Editor.SelectionStart=262144;Editor.SelectionLength=0;"+
                "assert(not Editor:SetStyles(Editor.TextRevision,{{Start=262143,End=262144,Color=0x123456}}))");
            await Settle();
            string? FullSource=CodeEditorHost.ReadText(Host);
            FrameRequire(FullSource is not null && FullSource.Length==262144 &&
                FullSource==new string('a',262144) && CodeEditorHost.SetStyles(Host,
                    "1;262143,262144,1193046"),"CodeEditorFullSourceAndTailStyle");
            CodeSendMessage(Editor,CodeWmChar,new IntPtr('x'),IntPtr.Zero);
            await Settle();
            FrameRequire(CodeEditorHost.ReadText(Host)==FullSource,"CodeEditorFullSourceRejectsAppend");
            FrameRun("assert(#Editor.Text==262144 and Editor.SelectionStart==262144 and "+
                "not pcall(function() Editor.Text=Editor.Text..'x' end))");
            FrameRequire(CodeEditorHost.SetSelection(Host,262143,1),"CodeEditorTailReplacementSelection");
            CodeSendMessage(Editor,CodeWmChar,new IntPtr('z'),IntPtr.Zero);
            await Settle();
            FrameRequire(CodeEditorHost.ReadText(Host)==new string('a',262143)+"z" &&
                CodeSendMessage(Editor,CodeEmCanUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorFullSourceTailNativeEdit");
            FrameRun("assert(#Editor.Text==262144 and Editor.Text:sub(-1)=='z')");
            FrameRequire(CodeSendMessage(Editor,CodeEmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,
                "CodeEditorFullSourceNativeUndo");
            await Settle();
            FrameRequire(CodeEditorHost.ReadText(Host)==FullSource,"CodeEditorFullSourceUndoRestoresText");
            FrameRun("assert(Editor.Text==string.rep('a',262144) and Editor.MaxLength==262144)");
            Groups.Add("Full 256 KiB native source, tail style, rejected append, accepted replacement and native Undo preserve runtime text");

            FrameRun("Editor:Destroy();Editor=Instance.new('CodeEditor',{Parent=Second,Text='a\\nb\\n',MaxLength=4,Size=UDim2.fromOffset(300,200)})");
            await Settle();
            Target=Views.Single(Pair=>Pair.Value.ClassName=="CodeEditor").Value;
            Host=Target.CodeHost;Editor=CodeEditorHost.LuiWinUITextEditorV1(Host);
            // The native Select All endpoint includes Rich Edit's implicit
            // paragraph mark. Do not substitute the canonical bounded setter.
            CodeSendMessage(Editor,0x00B1,IntPtr.Zero,new IntPtr(-1));
            CodeSelection WholeDocument=new();
            CodeSendMessageSelection(Editor,CodeEmExGetSel,IntPtr.Zero,ref WholeDocument);
            FrameRequire(WholeDocument.Start==0 && WholeDocument.End==5,"CodeEditorNativeSelectAllIncludesParagraph");
            await Settle();
            FrameRun("assert(Editor.SelectionStart==0 and Editor.SelectionLength==4)");
            CodeSendMessage(Editor,CodeWmChar,new IntPtr('x'),IntPtr.Zero);
            await Settle();
            FrameRun("assert(Editor.Text=='x' and Editor.SelectionStart==1 and Editor.SelectionLength==0)");
            FrameRequire(CodeSendMessage(Editor,CodeEmUndo,IntPtr.Zero,IntPtr.Zero)!=IntPtr.Zero,"CodeEditorSelectAllReplacementUndo");
            await Settle();
            FrameRun("assert(Editor.Text=='a\\nb\\n')");
            CodeSendMessage(Editor,0x00B1,IntPtr.Zero,new IntPtr(-1));
            CodeSendMessage(Editor,CodeWmChar,new IntPtr('z'),IntPtr.Zero);
            await Settle();
            FrameRun("assert(Editor.Text=='z')");
            Groups.Add("Native whole-document selection replaces LF-terminated text within MaxLength, reports canonical selection and preserves Undo");

            FrameRun("Editor:Destroy();assert(not pcall(function() Editor:SetStyles(5,{}) end))");
            await Settle();
            FrameRequire(!IsCodeWindow(Host),"CodeEditorHostRetirement");
            Groups.Add("Destroy retires popup HWND and rejects stale style calls");

            FrameRun("""
                Second:Destroy()
                PointerWindow=Instance.new('Window',{Title='LUI nested pointer qualification',Visible=true,Size=UDim2.fromOffset(640,420)})
                PointerRoot=Instance.new('Frame',{Parent=PointerWindow,Size=UDim2.fromScale(1,1)})
                PointerPanel=Instance.new('Frame',{Parent=PointerRoot,Position=UDim2.fromOffset(20,40),Size=UDim2.fromOffset(600,300)})
                PointerGrip=Instance.new('Frame',{Parent=PointerPanel,AccessibilityLabel='Pointer divider',Position=UDim2.fromOffset(280,0),Size=UDim2.fromOffset(8,300),BackgroundColor3=Color3.fromRGB(93,129,199)})
                PointerEditor=Instance.new('CodeEditor',{Parent=PointerPanel,Position=UDim2.fromOffset(288,0),Size=UDim2.fromOffset(312,300),Text='Pointer capture target'})
                PointerBegins,PointerMoves,PointerEnds,PointerCanceled,PointerLast=0,0,0,0,0
                PointerActive=false
                PointerGrip.InputBegan:Connect(function(Input)
                    if Input.Device=='Mouse' and Input.Button=='Left' then PointerBegins+=1;PointerActive=true end
                end)
                PointerGrip.InputChanged:Connect(function(Input)
                    if PointerActive then PointerMoves+=1;PointerLast=Input.Position.X end
                end)
                PointerGrip.InputEnded:Connect(function(Input)
                    if Input.Button=='Left' then PointerEnds+=1;if Input.IsCanceled then PointerCanceled+=1 end;PointerActive=false end
                end)
                """);
            await Settle();
            View PointerGrip=Views.Values.Single(Value=>Value.Element is not null && AutomationProperties.GetName(Value.Element)=="Pointer divider");
            View PointerWindow=Views.Values.Single(Value=>!Value.NativeClosed && Value.Window?.Title=="LUI nested pointer qualification");
            foreach(var Pair in Views.Where(Pair=>Pair.Value.Element is not null).ToArray()) {
                int PointerTarget=Pair.Key;View PointerView=Pair.Value;
                void Observe(string Phase,PointerRoutedEventArgs Args) {
                    if(!PointerView.IsAlive || PointerView.Element is null || PointerTrace.Count>=64) return;
                    var Point=Args.GetCurrentPoint(PointerView.Element);
                    PointerTrace.Add(new {Phase,Target=PointerTarget,Source=GetViewId(Args.OriginalSource as DependencyObject),
                        X=Point.Position.X,Y=Point.Position.Y,UpdateKind=Point.Properties.PointerUpdateKind.ToString(),
                        NativeCaptures=PointerView.Element!.PointerCaptures?.Count ?? 0,ActivePointers=PointerView.ActivePointerButtons.Count,Args.Handled});
                }
                PointerView.Element!.AddHandler(UIElement.PointerPressedEvent,new PointerEventHandler((_,Args)=>Observe("Down",Args)),true);
                PointerView.Element.AddHandler(UIElement.PointerMovedEvent,new PointerEventHandler((_,Args)=>Observe("Move",Args)),true);
                PointerView.Element.AddHandler(UIElement.PointerReleasedEvent,new PointerEventHandler((_,Args)=>Observe("Up",Args)),true);
                PointerView.Element.AddHandler(UIElement.PointerCanceledEvent,new PointerEventHandler((_,Args)=>Observe("Cancel",Args)),true);
                PointerView.Element.AddHandler(UIElement.PointerCaptureLostEvent,new PointerEventHandler((_,Args)=>Observe("CaptureLost",Args)),true);
            }
            IntPtr PointerOwner=WinRT.Interop.WindowNative.GetWindowHandle(PointerWindow.Window!);
            var Origin=PointerGrip.Element!.TransformToVisual(PointerWindow.Container).TransformPoint(new Windows.Foundation.Point(4,30));
            double PointerScale=PointerGrip.Element.XamlRoot.RasterizationScale;
            CodePoint PointerPosition=new() {X=(int)Math.Round(Origin.X*PointerScale),Y=(int)Math.Round(Origin.Y*PointerScale)};
            FrameRequire(ClientToScreen(PointerOwner,ref PointerPosition),"CodePointerScreenPosition");
            bool SavedCursor=GetCodeCursor(out CodePoint PreviousCursor),PointerDown=false;
            try {
                SetCodeForeground(PointerOwner);await Settle();
                FrameRequire(SetCodeCursor(PointerPosition.X,PointerPosition.Y),"CodePointerPlaceCursor");await Settle();
                CodeMouseButton(false);PointerDown=true;await Settle();
                for(int Step=1;Step<=6;++Step) {
                    CodeMouseMove(PointerPosition.X+(int)Math.Round(Step*16*PointerScale),PointerPosition.Y);await Settle();
                }
                CodeMouseButton(true);PointerDown=false;await Settle();
            } finally {
                if(PointerDown) CodeMouseButton(true);
                if(SavedCursor) SetCodeCursor(PreviousCursor.X,PreviousCursor.Y);
            }
            FrameRun("assert(PointerBegins==1 and PointerMoves>=3 and PointerEnds==1 and PointerCanceled==0 and PointerLast>=80)");
            Groups.Add("A nested Frame receives one native mouse gesture and retains capture across the adjacent Rich Edit popup");
            FrameRun("PointerWindow:Destroy()");await Settle();
        }
        catch(Exception Error)
        {
            Failure=Error is FrameQualificationFailure Failed?Failed.Check:Error.ToString();
            LuiDiagnostics.Error("CodeEditor","Runtime qualification: " + Failure);
        }
        finally
        {
            try {Native.Lui_RunScript(Runtime,"if PointerWindow then PointerWindow:Destroy() end;if Second then Second:Destroy() end;if W then W:Destroy() end","CodeEditorCleanup");Pump();}
            catch(Exception Error) {LuiDiagnostics.Error("CodeEditor","Cleanup: " + Error.GetType().Name);}
        }
        bool Passed=Failure is null;
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
        await File.WriteAllTextAsync(ReportPath,JsonSerializer.Serialize(new {Passed,Groups,PointerTrace,OwnerThread,Failure,ImeContextAvailable,ImeSetStringAccepted,ImeSetStringError,ImeCompositionBytes,ImeCompleteAccepted,ImeTextAfter,ImeRuntimeTextAfter,ImeSignalSummary},
            new JsonSerializerOptions {WriteIndented=true}));
        return Passed;
    }
}
