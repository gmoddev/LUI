using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    // Reuses the desktop workload counters and budgets. Native submission is
    // measured by OnRendering, not by the delays used to pace this driver.
    public async Task<bool> RunWorkbenchQualificationAsync(string ReportPath,long StartupAt)
    {
        int Transitions=0,PeakViews=0,PeakEditors=0,PeakHandles=0;
        long Baseline=Process.GetCurrentProcess().WorkingSet64,Peak=Baseline;
        double Startup=0;
        JsonElement? Metrics=null;
        string? Failure=null;
        IntPtr[] Editors=[];
        List<double> EditSamples=new();
        static void FrameRequire(bool Passed,string Code) {if(!Passed)throw new InvalidOperationException(Code);}
        JsonElement Node(string Name) {
            using var Tree=JsonDocument.Parse(Marshal.PtrToStringUTF8(Native.Lui_GetPreviewTreeJson(Runtime))!);
            return Tree.RootElement.EnumerateArray().Single(Item=>Item.GetProperty("name").GetString()==Name).Clone();
        }
        View Target(string Name)=>Views[Node(Name).GetProperty("id").GetInt32()];
        string NoteEditor()=>new[]{"LeftEditor","RightEditor"}.Single(Name=>Node(Name).GetProperty("text").GetString()!.Contains("Synthetic document 3"));
        string Rows() {
            using var Tree=JsonDocument.Parse(Marshal.PtrToStringUTF8(Native.Lui_GetPreviewTreeJson(Runtime))!);
            return JsonSerializer.Serialize(Tree.RootElement.EnumerateArray().Where(Item=>Item.GetProperty("name").GetString()!.StartsWith("Open-"))
                .Select(Item=>new {Name=Item.GetProperty("name").GetString(),Bounds=Item.GetProperty("bounds").Clone()}).ToArray());
        }
        void Click(string Name) {
            var Peer=FrameworkElementAutomationPeer.CreatePeerForElement((Button)Target(Name).Element!);
            FrameRequire(Peer?.GetPattern(PatternInterface.Invoke) is IInvokeProvider,"WorkbenchInvoke");
            ((IInvokeProvider)Peer!.GetPattern(PatternInterface.Invoke)).Invoke();
        }
        async Task Settle() {await Task.Delay(32);Pump();await WaitForPresentationAsync();}
        async Task Until(Func<bool> Predicate,string Code) {
            var Deadline=Stopwatch.StartNew();
            while(!Predicate() && Deadline.Elapsed.TotalSeconds<5) await Settle();
            FrameRequire(Predicate(),Code);
        }
        try {
            await Until(()=>Node("Inspector").GetProperty("text").GetString()!.Contains("open documents"),"WorkbenchReady");
            Startup=Stopwatch.GetElapsedTime(StartupAt).TotalSeconds;FrameRequire(Startup<=10,"WorkbenchStartupBudget");
            View Owner=Target("Workbench");
            Editors=Views.Values.Where(Value=>Value.CodeHost!=IntPtr.Zero).Select(Value=>Value.CodeHost).ToArray();
            FrameRequire(Editors.Length==2,"WorkbenchNativeEditors");
            for(int Index=0;Index<200;++Index) {
                switch(Index%8) {
                    case 0: Click("Tab-Note-3");break;
                    case 1:
                        Click("Tab-Note-3");await Settle();
                        string Name=NoteEditor();
                        View Editor=Target(Name);string Before=CodeEditorHost.ReadText(Editor.CodeHost)!;
                        FrameRequire(CodeEditorHost.LuiWinUITextFocusV1(Editor.CodeHost)==1,"WorkbenchNativeFocus");
                        FrameRequire(CodeEditorHost.SetSelection(Editor.CodeHost,Before.EnumerateRunes().Count(),0),"WorkbenchNativeSelection");
                        long EditAt=Stopwatch.GetTimestamp();
                        CodeSendMessage(CodeEditorHost.LuiWinUITextEditorV1(Editor.CodeHost),CodeWmChar,new IntPtr('x'),IntPtr.Zero);Pump();
                        double EditMs=Stopwatch.GetElapsedTime(EditAt).TotalMilliseconds;EditSamples.Add(EditMs);Native.Lui_RecordPerformance(Runtime,1,EditMs);
                        FrameRequire(Node(Name).GetProperty("text").GetString()==Before+"x","WorkbenchNativeEdit");
                        FrameRequire(Node("Tab-Note-3").GetProperty("text").GetString()!.StartsWith("* "),"WorkbenchDirty");
                        break;
                    case 2:
                        Click("Document.Save");await Until(()=>!Node("Tab-Note-3").GetProperty("text").GetString()!.StartsWith("* "),"WorkbenchSave");break;
                    case 3:
                        string BeforeSide=NoteEditor();Click("Workspace.Move");await Settle();FrameRequire(NoteEditor()!=BeforeSide,"WorkbenchRedock");break;
                    case 4:
                        double BeforeSize=Target("LeftEditor").CodeWidth;
                        Owner.Window!.AppWindow.Resize(new Windows.Graphics.SizeInt32(Index%16==4?1220:1180,780));
                        await Until(()=>Math.Abs(Target("LeftEditor").CodeWidth-BeforeSize)>=8,"WorkbenchResize");break;
                    case 5:
                        string BeforeStyle=Target("LeftEditor").StyleJson;Click("View.Theme");await Settle();
                        FrameRequire(Target("LeftEditor").StyleJson!=BeforeStyle,"WorkbenchTheme");break;
                    case 6:
                        string BeforeRows=Rows();long ScrollAt=Stopwatch.GetTimestamp();
                        FrameRequire(Native.Lui_ScrollInput(Runtime,Node("Navigation").GetProperty("id").GetInt32(),0,Index%16==6?120:-120)==1,"WorkbenchScrollAdmission");
                        Pump();Native.Lui_RecordPerformance(Runtime,1,Stopwatch.GetElapsedTime(ScrollAt).TotalMilliseconds);
                        FrameRequire(Rows()!=BeforeRows,"WorkbenchScroll");break;
                    case 7:
                        Click("Tab-Note-2");await Settle();FrameRequire(Node("Inspector").GetProperty("text").GetString()!.Contains("\n\nNote 2\n"),"WorkbenchTab2");
                        Click("Tab-Note-3");await Settle();FrameRequire(Node("Inspector").GetProperty("text").GetString()!.Contains("\n\nNote 3\n"),"WorkbenchTab3");break;
                }
                await Settle();++Transitions;
                using var Process=System.Diagnostics.Process.GetCurrentProcess();
                Peak=Math.Max(Peak,Process.WorkingSet64);PeakHandles=Math.Max(PeakHandles,Process.HandleCount);
                var CurrentEditors=Views.Values.Where(Value=>Value.CodeHost!=IntPtr.Zero).Select(Value=>Value.CodeHost).ToArray();
                PeakViews=Math.Max(PeakViews,Views.Count);PeakEditors=Math.Max(PeakEditors,CurrentEditors.Length);
                FrameRequire(Views.Count<=256 && CurrentEditors.Length==2 && Editors.All(Editor=>IsCodeWindow(Editor) && CurrentEditors.Contains(Editor)) && ImageCacheBytes<=32*1024*1024 && ImageRequests.Count<=32,"WorkbenchResourceBudget");
                using var Sample=JsonDocument.Parse(Marshal.PtrToStringUTF8(Native.Lui_GetPerformanceJson(Runtime))!);
                FrameRequire(Sample.RootElement.GetProperty("LuaMemoryBytes").GetInt64()<=67108864,"WorkbenchVmBudget");
            }
            Click("Workspace.Save");await Until(()=>Node("Status").GetProperty("text").GetString()=="Layout saved locally.","WorkbenchLayoutSave");
            using var Document=JsonDocument.Parse(Marshal.PtrToStringUTF8(Native.Lui_GetPerformanceJson(Runtime))!);
            Metrics=Document.RootElement.Clone();var Value=Metrics.Value;
            FrameRequire(Peak<=512*1024*1024 && Value.GetProperty("LiveInstances").GetInt32()<=256 && Value.GetProperty("Connections").GetInt32()<=192,"WorkbenchMemoryBudget");
            FrameRequire(Value.GetProperty("Input").GetProperty("Samples").GetInt32()>=100 && Value.GetProperty("Input").GetProperty("P95Ms").GetDouble()<=100 && Value.GetProperty("Input").GetProperty("P99Ms").GetDouble()<=250,"WorkbenchInputBudget");
            FrameRequire(Value.GetProperty("Layout").GetProperty("Samples").GetInt32()>=200 && Value.GetProperty("Layout").GetProperty("P95Ms").GetDouble()<=50 && Value.GetProperty("Layout").GetProperty("P99Ms").GetDouble()<=100 && Value.GetProperty("Render").GetProperty("Samples").GetInt32()>=100 && Value.GetProperty("Render").GetProperty("P95Ms").GetDouble()<=50,"WorkbenchPresentationBudget");
            EditSamples.Sort();FrameRequire(EditSamples.Count==25 && EditSamples[(int)Math.Ceiling(EditSamples.Count*0.95)-1]<=100 && EditSamples[(int)Math.Ceiling(EditSamples.Count*0.99)-1]<=250,"WorkbenchEditBudget");
        } catch(Exception Error) {
            Failure=Error.Message;LuiDiagnostics.Error("WorkbenchQualification",Error.GetType().Name+": "+Failure);
            using var Current=JsonDocument.Parse(Marshal.PtrToStringUTF8(Native.Lui_GetPerformanceJson(Runtime))!);Metrics=Current.RootElement.Clone();
        }
        finally {Dispose();}
        bool Retired=Views.Count==0 && Editors.All(Editor=>!IsCodeWindow(Editor));
        bool Passed=Failure is null && Retired;
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(ReportPath))!);
        await File.WriteAllTextAsync(ReportPath,JsonSerializer.Serialize(new {Passed,Failure,Transitions,StartupSeconds=Startup,Metrics,
            WorkingSetBaselineBytes=Baseline,WorkingSetPeakBytes=Peak,PeakViews,PeakEditors,PeakHandles,Retired,EditSamplesMs=EditSamples,
            Boundary="Native widget callbacks and explicit scroll admission through owner Pump; existing native render submission counters; excludes physical input/display and automation pacing"},new JsonSerializerOptions{WriteIndented=true}));
        return Passed;
    }
}
