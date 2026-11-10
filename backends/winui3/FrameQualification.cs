using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.UI.Windowing;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    private const string FrameScene = """
        assert(app:GetService('PlatformService'):Supports('FrameSurface'))
        W=Instance.new('Window',{Title='LUI DSP-1 WinUI Qualification',Visible=true,Size=UDim2.fromOffset(640,480)})
        Root=Instance.new('Frame',{Parent=W,Size=UDim2.fromScale(1,1),BackgroundColor3=Color3.fromRGB(255,255,255)})
        S=Instance.new('FrameSurface',{Name='Surface',Parent=Root,Position=UDim2.fromOffset(20,20),Size=UDim2.fromOffset(200,100),ScaleMode='Stretch',AccessibilityLabel='Synthetic frame surface'})
        B=buffer.create(48); buffer.fill(B,0,170)
        function Pixel(O,Bb,G,R,A) buffer.writeu8(B,O,Bb); buffer.writeu8(B,O+1,G); buffer.writeu8(B,O+2,R); buffer.writeu8(B,O+3,A) end
        Pixel(0,0,0,255,255);Pixel(4,0,0,255,255);Pixel(8,0,128,0,128);Pixel(12,0,128,0,128)
        Pixel(24,255,0,0,255);Pixel(28,255,0,0,255);Pixel(32,0,255,255,255);Pixel(36,0,255,255,255)
        function Pattern() assert(S:SubmitFrame(B,4,2,24,'BGRA8Premultiplied')) end
        Count=0
        Action=Instance.new('TextButton',{Parent=Root,Position=UDim2.fromOffset(20,350),Size=UDim2.fromOffset(180,40),Text='Frame input 0',AccessibilityLabel='Frame input',BackgroundColor3=Color3.fromRGB(230,230,230),TextColor3=Color3.fromRGB(0,0,0)})
        Action.Activated:Connect(function() Count+=1; Action.Text='Frame input '..Count end)
        Pattern()
        """;
    private int FrameQualificationStep;
    private void FrameRun(string Source) {
        ++FrameQualificationStep;
        if (Native.Lui_RunScript(Runtime, Source, "FrameQualification") != 1) throw new InvalidOperationException("PublicSubmission");
    }
    private View SurfaceView() => Views.Values.Single(Value => Value.ClassName == "FrameSurface");
    private Native.FrameStats GetFrameStats() {
        Native.FrameStats Value = new() {StructSize=(uint)Marshal.SizeOf<Native.FrameStats>(),AbiVersion=1};
        if (Native.Lui_GetFrameStats(Runtime,ref Value)!=1) throw new InvalidOperationException("FrameStats"); return Value;
    }
    private static Native.FrameGpuStats GetFrameGpuStats() {
        Native.FrameGpuStats Value=new(){StructSize=(uint)Marshal.SizeOf<Native.FrameGpuStats>(),Version=1};
        Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameGpuStatsV1(ref Value));return Value;
    }
    private static object FrameGpuMetrics() {
        var Value=GetFrameGpuStats();return new{Value.Uploads,Value.UploadBytes,Value.PeakUploadBytes,Value.Devices,Value.Sources,Value.Frames,Value.Copies,Value.Draws,Value.Removed};
    }
    private async Task WaitRetiredFramesAsync() {
        await Task.WhenAll(FrameResources.Where(Value=>Value.Canceled).Select(Value=>Value.ReleaseFinished.Task)).WaitAsync(TimeSpan.FromSeconds(10));
    }
    private async Task FlushFrameAsync() {
        Pump();
        await WaitRetiredFramesAsync();
        var Upload=SurfaceView().PreparedFrame;
        if (Upload is not null) await Upload.Finished.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Pump();
        await WaitRetiredFramesAsync();
    }
    private static object Aggregate(IEnumerable<double> Values) {
        double[] Sorted=Values.Order().ToArray();
        double Rank(double Fraction) => Sorted.Length==0?0:Sorted[Math.Clamp((int)Math.Ceiling(Fraction*Sorted.Length)-1,0,Sorted.Length-1)];
        return new {Samples=Sorted.Length,MedianMs=Rank(.5),P95Ms=Rank(.95),P99Ms=Rank(.99),MaxMs=Sorted.LastOrDefault()};
    }
    private async Task<byte[]> CaptureFrameAsync(string Path, FrameworkElement Root) {
        await WaitForPresentationAsync(); Root.UpdateLayout();
        RenderTargetBitmap Target=new(); await Target.RenderAsync(Root);
        var Buffer=await Target.GetPixelsAsync(); byte[] Pixels=new byte[Buffer.Length];
        using(var Reader=DataReader.FromBuffer(Buffer)) Reader.ReadBytes(Pixels);
        using var Stream=new InMemoryRandomAccessStream();
        var Encoder=await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId,Stream);
        Encoder.SetPixelData(BitmapPixelFormat.Bgra8,BitmapAlphaMode.Premultiplied,(uint)Target.PixelWidth,(uint)Target.PixelHeight,96,96,Pixels);
        await Encoder.FlushAsync(); Stream.Seek(0);
        using var Encoded=new DataReader(Stream); await Encoded.LoadAsync((uint)Stream.Size);
        byte[] Png=new byte[(int)Stream.Size]; Encoded.ReadBytes(Png); File.WriteAllBytes(Path,Png);
        FrameCaptureWidth=Target.PixelWidth; FrameCaptureHeight=Target.PixelHeight; return Pixels;
    }
    private int FrameCaptureWidth, FrameCaptureHeight;
    private void CheckPixel(byte[] Pixels, FrameworkElement Root, double X, double Y, int R, int G, int B, int Tolerance=12) {
        int Px=Math.Clamp((int)(X*FrameCaptureWidth/Root.ActualWidth),0,FrameCaptureWidth-1);
        int Py=Math.Clamp((int)(Y*FrameCaptureHeight/Root.ActualHeight),0,FrameCaptureHeight-1);
        int At=(Py*FrameCaptureWidth+Px)*4;
        if (Math.Abs(Pixels[At]-B)>Tolerance || Math.Abs(Pixels[At+1]-G)>Tolerance || Math.Abs(Pixels[At+2]-R)>Tolerance)
            throw new InvalidOperationException("PixelCorrectness");
    }
    public async Task<bool> RunFrameQualificationAsync(string ReportPath, bool Observation=false) {
        string DirectoryPath=Path.GetDirectoryName(Path.GetFullPath(ReportPath))!; Directory.CreateDirectory(DirectoryPath);
        var Passed=new List<string>(); object? Comparison=null;
        var Admission=new List<double>(); long Baseline=0,Peak=0;
        void Require(bool Condition,string Code) {if(!Condition)throw new InvalidOperationException(Code);}
        void Pass(string Name){Passed.Add(Name);}
        try {
            Require(Marshal.SizeOf<Native.FrameView>()==56&&Marshal.SizeOf<Native.FrameCallbacks>()==48&&Marshal.SizeOf<Native.FrameStats>()==104,"PrivateAbi");
            FrameRun(FrameScene); await FlushFrameAsync(); await WaitForPresentationAsync();
            var Window=Views.Values.Single(Value=>Value.Window is not null).Window!;
            var Root=(FrameworkElement)Window.Content;
            var Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"01-pattern.png"),Root);
            CheckPixel(Pixels,Root,50,40,255,0,0);CheckPixel(Pixels,Root,50,100,0,0,255);CheckPixel(Pixels,Root,170,100,255,255,0);
            CheckPixel(Pixels,Root,170,40,127,255,127);
            // Premultiplied green over white must be green, translucent, and free
            // of red/blue asymmetry. Raw BGRA/alpha is independently checked below.
            Native.FrameView Raw=new(){StructSize=(uint)Marshal.SizeOf<Native.FrameView>(),AbiVersion=1};
            Native.Lui_FrameView(SurfaceView().DisplayedFrame!.Lease,ref Raw);
            byte[] Bytes=new byte[32];Marshal.Copy(Raw.Pixels,Bytes,0,32);Require(Bytes[8]==0&&Bytes[9]==128&&Bytes[11]==128&&Raw.RowStride==16,"AlphaStride");
            FrameRun("buffer.fill(B,0,0)");Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"02-immutable.png"),Root);CheckPixel(Pixels,Root,50,40,255,0,0);
            Pass("BGRA/alpha/orientation/stride/caller mutation");
            FrameRun("S.Size=UDim2.fromOffset(200,200); S.ScaleMode='Fit'");
            Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"03-fit.png"),Root);CheckPixel(Pixels,Root,50,30,255,255,255);CheckPixel(Pixels,Root,50,90,255,0,0);
            FrameRun("S.ScaleMode='Fill'");Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"04-fill.png"),Root);CheckPixel(Pixels,Root,50,30,255,0,0);
            FrameRun("S.ScaleMode='Stretch'");Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"05-stretch.png"),Root);CheckPixel(Pixels,Root,50,30,255,0,0);
            FrameRun("S.Opacity=0.5");Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"05-opacity.png"),Root);CheckPixel(Pixels,Root,50,40,255,128,128);
            FrameRun("S.Opacity=1; Clip=Instance.new('Frame',{Parent=Root,Position=UDim2.fromOffset(300,20),Size=UDim2.fromOffset(80,80),ClipsDescendants=true}); S.Parent=Clip; S.Position=UDim2.fromOffset(0,0); S.Size=UDim2.fromOffset(200,100)");
            Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"06-clip.png"),Root);CheckPixel(Pixels,Root,320,40,255,0,0);CheckPixel(Pixels,Root,410,40,255,255,255);
            FrameRun("Cover=Instance.new('Frame',{Parent=Root,Position=UDim2.fromOffset(310,30),Size=UDim2.fromOffset(40,40),BackgroundColor3=Color3.fromRGB(0,0,255),ZIndex=10})");
            Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"07-stack.png"),Root);CheckPixel(Pixels,Root,320,40,0,0,255);
            FrameRun("Cover.ZIndex=-10");Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"07-lowered.png"),Root);CheckPixel(Pixels,Root,320,40,255,0,0);
            FrameRun("S.Visible=false");Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"08-hidden.png"),Root);CheckPixel(Pixels,Root,320,40,0,0,255);
            FrameRun("S.Visible=true; S.Parent=Root; S.Position=UDim2.fromOffset(20,20); Cover:Destroy(); Clip:Destroy()");
            var Presenter=(OverlappedPresenter)Window.AppWindow.Presenter; Presenter.Maximize(); await WaitForPresentationAsync(); Pump(); Presenter.Restore();
            Window.AppWindow.Resize(new Windows.Graphics.SizeInt32(700,520));await WaitForPresentationAsync();Pump();
            Require(((FrameworkElement)SurfaceView().Element!).Width==200,"ResizeGeometry");
            await CaptureFrameAsync(Path.Combine(DirectoryPath,"09-resize.png"),Root);Pass("Fit/Fill/Stretch/clip/stack/visibility/opacity/reparent/resize/maximize/restore");
            ulong ExpectedFailures=FrameUploadFailures;
            foreach (int Fault in new[]{1,2,4}) {
                FrameQualificationFault=Fault;FrameRun("Pattern()");Pump();
                Require(FrameUploadsInProgress==0&&FrameResources.Count==1&&GetFrameStats().InFlight==0,"CallbackAllocationFailure");
                FrameRun("assert(S.FrameState=='Failed')");
                ++ExpectedFailures;Require(FrameUploadFailures==ExpectedFailures,"CallbackAllocationFailure");
            }
            FrameQualificationFault=3;FrameRun("S:ClearFrame(); assert(S.FrameState=='Empty')");
            Require(FrameResources.Count==1&&GetFrameStats().StagingBytes>0,"DetachFailureAccounting");
            Pump();await WaitRetiredFramesAsync();Require(FrameResources.Count==0&&GetFrameStats().StagingBytes==0,"DetachFailureRecovery");
            FrameRun("Pattern()");await FlushFrameAsync();Pass("Callback allocation failure/detach failure/retry accounting");
            var BeforeFailedPublish=SurfaceView().DisplayedFrame;
            FrameQualificationFault=6;FrameRun("Pattern()");await FlushFrameAsync();FrameRun("assert(S.FrameState=='Failed')");
            FrameRequire(SurfaceView().PreparedFrame is null&&FrameResources.Count==1&&ReferenceEquals(BeforeFailedPublish,SurfaceView().DisplayedFrame),"PublishFailureRetirement");
            Pass("Publish failure preserves displayed content and retires failed preparation");
            FrameQualificationFault=5;FrameRun("S:ClearFrame()");
            Require(FrameResources.Count==1&&GetFrameStats().StagingBytes>0,"RetireSchedulingAccounting");
            Pump();await WaitRetiredFramesAsync();Require(GetFrameStats().StagingBytes==0,"RetireSchedulingRecovery");
            FrameRun("Pattern()");await FlushFrameAsync();Pass("Retirement scheduling allocation failure/reachable charged retry");
            FrameRun("Pattern()");Pump();var BeforePrepare=SurfaceView().PreparedFrame!;
            FrameRun("S:ClearFrame()");await BeforePrepare.Finished.Task.WaitAsync(TimeSpan.FromSeconds(5));Pump();
            await BeforePrepare.ReleaseFinished.Task.WaitAsync(TimeSpan.FromSeconds(5));
            await WaitRetiredFramesAsync();
            Require(BeforePrepare.Gpu==IntPtr.Zero&&FrameResources.Count==0,"CancelBeforePreparation");
            FrameRun("Pattern()");await FlushFrameAsync();Pass("Clear before native preparation starts");
            var Gate=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);FrameUploadBarrier=Gate.Task;
            FrameRun("buffer.fill(B,0,0); Pattern()");Pump();var Old=SurfaceView().PreparedFrame!;await Old.Entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
            FrameRun("B=buffer.create(48); for O=0,44,4 do buffer.writeu32(B,O,0xff00ff00) end; Pattern()");
            ulong Before=FramePublished;Gate.SetResult();await Old.Finished.Task;await FlushFrameAsync();
            Require(FramePublished==Before+1&&GetFrameStats().Stale>0,"StalePublication");
            Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"10-latest.png"),Root);CheckPixel(Pixels,Root,50,40,0,255,0);
            Gate=new(TaskCreationOptions.RunContinuationsAsynchronously);FrameUploadBarrier=Gate.Task;FrameRun("Pattern()");Pump();Old=SurfaceView().PreparedFrame!;await Old.Entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
            FrameRun("S:ClearFrame(); assert(S.FrameState=='Empty')");Require(((Image)SurfaceView().Content!).Source is null,"ClearSource");Gate.SetResult();await Old.Finished.Task;Pump();
            FrameRun("assert(S.FrameState=='Empty')");FrameUploadBarrier=null;FrameRun("Pattern()");await FlushFrameAsync();
            Gate=new(TaskCreationOptions.RunContinuationsAsynchronously);FrameUploadBarrier=Gate.Task;FrameRun("Pattern()");Pump();Old=SurfaceView().PreparedFrame!;await Old.Entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
            FrameRun("S:Destroy(); S:Destroy()");Gate.SetResult();await Old.Finished.Task;Pump();FrameUploadBarrier=null;
            await WaitRetiredFramesAsync();Require(FrameResources.Count==0&&FrameUploadsInProgress==0,"DestroyRelease");Pass("Delayed upload/newer frame/clear/destroy/cancellation");
            // Fresh backend generations reuse IDs/epochs while old native pixels
            // are held behind an explicit completion barrier.
            var Retired=new WinUIBackend();Retired.FrameRun(FrameScene);
            var RetireGate=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);Retired.FrameUploadBarrier=RetireGate.Task;Retired.Pump();
            var RetiredUpload=Retired.SurfaceView().PreparedFrame!;await RetiredUpload.Entered.Task.WaitAsync(TimeSpan.FromSeconds(5));Retired.Dispose();
            using(var Replacement=new WinUIBackend()) {Replacement.FrameRun(FrameScene);await Replacement.FlushFrameAsync();RetireGate.SetResult();await RetiredUpload.Finished.Task;
                await Retired.WaitRetiredFramesAsync();Require(Retired.FrameResources.Count==0&&Retired.FrameUploadsInProgress==0&&Replacement.FramePublished==1,"GenerationRetirement");}
            FrameRun("S=Instance.new('FrameSurface',{Parent=Root,Name='Surface',Size=UDim2.fromOffset(500,280),ScaleMode='Fit'}); Pattern()");await FlushFrameAsync();Pass("Reload/late native completion/shutdown/identical IDs");
            await QualifyActiveFrameCopiesAsync();Pass("Actual mapped half-copy/clear/destroy/reload/input/nonblocking retirement");
            await QualifyFrameGpuCancellationAsync();Pass("Actual queued GPU read/cancel/retained upload/fence release");
            await QualifyFrameDeviceLossAsync(DirectoryPath);Pass("Real task-private device removal/displayed/in-flight/clear/recreate/pixel recovery");
            await QualifyRecoveryOrderingAsync(DirectoryPath);Pass("Authoritative pending/parent/visibility ordering and ready-before-publish device loss");
            await QualifySurfaceLossAsync(DirectoryPath);Pass("Surface-only loss/prepared/recovery/hidden/retry fault boundaries");
            FrameRequire(FrameUploadFailures==8,"UnexpectedNativeFailure");
            FrameRun("Extra={};for I=1,3 do Extra[I]=Instance.new('FrameSurface',{Parent=Root,Size=UDim2.fromOffset(80,40),Position=UDim2.fromOffset(I*90,300),ScaleMode='Stretch'}) end");
            async Task FlushAll() {
                Pump();await Task.WhenAll(Views.Values.Where(Value=>Value.PreparedFrame is not null).Select(Value=>Value.PreparedFrame!.Finished.Task)).WaitAsync(TimeSpan.FromSeconds(5));Pump();await WaitRetiredFramesAsync();Pump();
            }
            for(int Index=1;Index<=3;Index++) {FrameRun($"assert(Extra[{Index}]:SubmitFrame(B,4,2,24,'BGRA8Premultiplied'))");await FlushAll();}
            FrameRun("Pattern();for _,F in Extra do assert(F:SubmitFrame(B,4,2,24,'BGRA8Premultiplied')) end");await FlushAll();
            Require(FrameStagingRefusals==1&&GetFrameStats().PixelBytes+GetFrameStats().StagingBytes<=64*1024*1024,"NativeStagingQuota");
            FrameRun("for _,F in Extra do if F.FrameState=='Failed' then assert(F:SubmitFrame(B,4,2,24,'BGRA8Premultiplied')) end end");await FlushAll();
            FrameRun("assert(S.FrameState=='Ready');for _,F in Extra do assert(F.FrameState=='Ready');F:Destroy() end;Extra=nil");
            await WaitRetiredFramesAsync();Require(FrameResources.Count==1,"MultiSurfaceRetirement");Pass("Four native surfaces/staging saturation/refusal/fair retry");
            Comparison=new{Selected="SurfaceImageSource/D3D12 UPLOAD/D3D11On12",PriorMeasuredAlternatives="505c64b93e071631a03b711418fc0078519b5a34",CpuAllocation="One queried committed UPLOAD buffer; no full-frame XAML fallback"};
            Pass("Prior measured bitmap alternatives/current explicit upload selection");
            FrameRun("function Solid(C) local V=buffer.create(1280*720*4);buffer.writeu32(V,0,C);local N=4;while N<buffer.len(V) do local L=math.min(N,buffer.len(V)-N);buffer.copy(V,N,V,0,L);N+=L end;return V end;P=Solid(0xffff0000);Q=Solid(0xff0000ff);FrameNumber=0");
            FrameRun("Large=buffer.create(1920*1080*4);buffer.writeu32(Large,0,0xffff0000);local N=4;while N<buffer.len(Large) do local L=math.min(N,buffer.len(Large)-N);buffer.copy(Large,N,Large,0,L);N+=L end;assert(S:SubmitFrame(Large,1920,1080,7680,'BGRA8Premultiplied'));Large=nil;S.ScaleMode='Stretch'");
            await FlushFrameAsync();Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"10-boundary-1080.png"),Root);CheckPixel(Pixels,Root,50,40,255,0,0);
            FrameRun("S:ClearFrame()");Pump();Pass("1920x1080 native profile boundary");
            Baseline=Process.GetCurrentProcess().WorkingSet64;Peak=Baseline;
            ulong PublishedBeforeWorkload=FramePublished;
            var Clock=Stopwatch.StartNew();
            var Action=Views.Values.Select(Value=>Value.Element).OfType<Button>().Single();
            var Invoke=(IInvokeProvider)FrameworkElementAutomationPeer.CreatePeerForElement(Action).GetPattern(PatternInterface.Invoke);
            for(int Index=0;Index<1000;Index++) {
                double Due=Index*1000.0/30-Clock.Elapsed.TotalMilliseconds;if(Due>0)await Task.Delay(TimeSpan.FromMilliseconds(Due));
                long Started=Stopwatch.GetTimestamp();FrameRun("FrameNumber+=1; assert(S:SubmitFrame(FrameNumber%2==0 and P or Q,1280,720,5120,'BGRA8Premultiplied'))");Admission.Add(Stopwatch.GetElapsedTime(Started).TotalMilliseconds);SampleFrameResources();
                if(Index%5==0)Invoke.Invoke();
                if(Index%120==0)Window.AppWindow.Resize(new Windows.Graphics.SizeInt32(Index%240==0?700:720,520));
                await FlushFrameAsync();await WaitForPresentationAsync();
                var Stats=GetFrameStats();SampleFrameResources();
                Require(Stats.PixelBytes+Stats.StagingBytes<=64*1024*1024&&Stats.Frames<=3&&Stats.Pending<=1&&Stats.InFlight<=1,"ResourceBudget");
                Peak=Math.Max(Peak,Process.GetCurrentProcess().WorkingSet64);
            }
            double WorkloadSeconds=Clock.Elapsed.TotalSeconds;
            ulong WorkloadPublished=FramePublished-PublishedBeforeWorkload;
            FrameRequire(WorkloadPublished==1000&&WorkloadSeconds<=35,"WorkloadCadenceBudget");
            FrameRun("assert(Count>=200)");Pass("1000 accepted 720p frames/30Hz/native submission/input/resize");
            FrameRun("for I=1,120 do assert(S:SubmitFrame(P,1280,720,5120,'BGRA8Premultiplied')) end");var Burst=GetFrameStats();await FlushFrameAsync();Require(GetFrameStats().Replaced>=119,"LatestWins");Pass("120-frame overproduction");
            using var Metrics=JsonDocument.Parse(Marshal.PtrToStringUTF8(Native.Lui_GetPerformanceJson(Runtime))!);
            static double Percent(Queue<double> Values,double P){var A=Values.Order().ToArray();return A[(int)Math.Ceiling(A.Length*P)-1];}
            Require(Admission.Order().ElementAt(949)<=8&&Admission.Order().ElementAt(989)<=16,"AdmissionBudget");
            Require(Percent(FrameSubmissionSamples,.95)<=50&&Percent(FrameSubmissionSamples,.99)<=100,"SubmissionBudget");
            Require(Metrics.RootElement.GetProperty("Input").GetProperty("P95Ms").GetDouble()<=100&&Metrics.RootElement.GetProperty("Input").GetProperty("P99Ms").GetDouble()<=250,"InputBudget");
            Require(Metrics.RootElement.GetProperty("Layout").GetProperty("P95Ms").GetDouble()<=50&&Metrics.RootElement.GetProperty("Layout").GetProperty("P99Ms").GetDouble()<=100,"LayoutBudget");
            long WholeProcessPeak=Process.GetCurrentProcess().PeakWorkingSet64;
            Require(WholeProcessPeak<=512*1024*1024,"ProcessBudget");
            await CaptureFrameAsync(Path.Combine(DirectoryPath,"11-sustained.png"),Root);
            var AdmissionAggregate=Aggregate(Admission);var UploadAggregate=Aggregate(FrameUploadSamples);var SubmissionAggregate=Aggregate(FrameSubmissionSamples);
            ulong BenchmarkPublished=FramePublished;
            if(Observation) {FrameRun("W.Title='LUI DSP-1 Frame Observation'; S.Position=UDim2.fromOffset(20,20); Action.Position=UDim2.fromOffset(20,350); function Tick() FrameNumber+=1;S:SubmitFrame(FrameNumber%2==0 and P or Q,1280,720,5120,'BGRA8Premultiplied');task.delay(1/30,Tick) end; Tick()");await Task.Delay(TimeSpan.FromSeconds(30));}
            FrameRun("S:ClearFrame(); S:Destroy()");Pump();
            await WaitRetiredFramesAsync();
            Require(FrameResources.Count==0&&FrameUploadsInProgress==0&&GetFrameStats().PixelBytes==0&&GetFrameStats().StagingBytes==0,"Quiescence");
            var FinalDevice=FrameDevice;
            StopFrames();
            if(FinalDevice?.Retirement is {} Retirement)await Retirement.WaitAsync(TimeSpan.FromSeconds(10));
            var NativeClock=Stopwatch.StartNew();
            while(GetFrameGpuStats().Devices!=0&&NativeClock.Elapsed.TotalSeconds<10)await Task.Delay(1);
            var FinalGpu=GetFrameGpuStats();
            FrameRequire(FinalGpu.Devices==0&&FinalGpu.Frames==0&&FinalGpu.Sources==0&&FinalGpu.UploadBytes==0,"NativeQuiescence");
            Pass("Final native frames/sources/uploads/devices and shared charges released");
            using(var Ordinary=new WinUIBackend()) {
                Require(Native.Lui_RunScript(Ordinary.Runtime,"assert(app:GetService('PlatformService'):Supports('FrameSurface'));local S=Instance.new('FrameSurface');assert(S:IsA('GuiObject'));local C=S:Clone();assert(C.FrameState=='Empty');C:Destroy();S:Destroy();S:Destroy()","FrameCapabilityGate")==1,"OrdinaryCapabilityGate");
            }
            Pass("Ordinary WinUI capability/creation/inheritance/clone/idempotent destruction");
            File.WriteAllText(ReportPath,JsonSerializer.Serialize(new {Success=true,Scope="NativeFixture",Platform=RuntimeInformation.OSDescription,Architecture=RuntimeInformation.ProcessArchitecture.ToString(),Framework=RuntimeInformation.FrameworkDescription,DpiScale=Root.XamlRoot.RasterizationScale,Passed,Comparison,WorkloadSeconds,WorkloadPublished,WorkloadHz=1000/WorkloadSeconds,Admission=AdmissionAggregate,Upload=UploadAggregate,Submission=SubmissionAggregate,BaselineBytes=Baseline,PeakBytes=Peak,WholeProcessPeakBytes=WholeProcessPeak,ObservedPeakPixels=FramePeakPixels,ObservedPeakStaging=FramePeakStaging,ObservedPeakAccounted=FramePeakAccounted,ObservedMaxPending=FrameMaxPending,ObservedMaxInFlight=FrameMaxInFlight,ObservedMaxAllocations=FrameMaxAllocations,Accepted=Burst.Accepted,Replaced=Burst.Replaced,Rejected=Burst.Rejected,Stale=Burst.Stale,Published=BenchmarkPublished,ObservationPublished=FramePublished-BenchmarkPublished,NativeFailures=FrameUploadFailures,InjectedFailures=8,StagingRefusals=FrameStagingRefusals,DeviceLosses=FrameDeviceLosses,Recoveries=FrameRecoveries,Gpu=FrameGpuMetrics(),Metrics=Metrics.RootElement.Clone()}));
            return true;
        } catch(Exception Error) {
            string[] Codes={"PrivateAbi","PublicSubmission","FrameStats","PixelCorrectness","AlphaStride","Opacity","ResizeGeometry","StalePublication","ClearSource","DestroyRelease","GenerationRetirement","ResourceBudget","LatestWins","AdmissionBudget","SubmissionBudget","InputBudget","LayoutBudget","ProcessBudget","Quiescence","CallbackAllocationFailure","DetachFailureAccounting","DetachFailureRecovery","CancelBeforePreparation","OrdinaryCapabilityGate","NativeStagingQuota","MultiSurfaceRetirement"};
            string Code=Error is FrameQualificationFailure Failure?Failure.Check:Codes.Contains(Error.Message)?Error.Message:"FrameQualificationFailed";
            File.WriteAllText(ReportPath,JsonSerializer.Serialize(new {Success=false,Code,ExceptionType=Error.GetType().Name,Error.HResult,Step=FrameQualificationStep,NativeError=LastError,FrameLastHResult,Gpu=FrameGpuMetrics(),Passed,FramePublished,FrameUploadFailures,FrameDeviceLosses,FrameRecoveries,FrameStagingRefusals,Resources=FrameResources.Select(Value=>new{Value.Canceled,Value.Working,Value.Retiring,Value.NeedsRecovery,Value.Ready,Value.Completed,Value.Epoch,Value.Sequence,Gpu=Value.Gpu!=IntPtr.Zero}),InFlight=FrameUploadsInProgress,Comparison}));
            return false;
        }
    }
}
