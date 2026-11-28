param([Parameter(Mandatory)][string]$StagedSource,[switch]$ProjectDiagnostics,[switch]$SchedulingExperiment,[switch]$MatchedThroughput,[switch]$PassiveDisplay)

# Apply instrumentation only in a disposable source copy for paired runs.
$ErrorActionPreference = 'Stop'
$Backend = Join-Path (Resolve-Path -LiteralPath $StagedSource) 'backends/winui3'

function Add-Probe([string]$Name, [string]$Target, [string]$Replacement) {
    $Path = Join-Path $Backend $Name
    $Current = [IO.File]::ReadAllText($Path)
    if ($Current.Contains($Replacement)) { return }
    if (-not $Current.Contains($Target)) { throw "Probe point missing in $Name" }
    [IO.File]::WriteAllText($Path, $Current.Replace($Target, $Replacement))
}

if ($ProjectDiagnostics) {
    Add-Probe 'App.xaml.cs' '"--manifest"),Modular);' '"--manifest"),Modular && Environment.GetEnvironmentVariable("LUI_IMAGE_WORKLOAD_LOG_DIR") is null,Environment.GetEnvironmentVariable("LUI_IMAGE_WORKLOAD_LOG_DIR"));'
} else {
    Add-Probe 'App.xaml.cs' '"--manifest"),Modular);' '"--manifest"),Modular,Environment.GetEnvironmentVariable("LUI_IMAGE_WORKLOAD_LOG_DIR"));'
}
Add-Probe 'App.xaml.cs' 'PumpTimer?.Stop();' 'Backend?.ReportImageWorkload(); PumpTimer?.Stop();'
Add-Probe 'App.xaml.cs' 'Backend?.ReportImageWorkload(); PumpTimer?.Stop();' 'FrameTimingProbe.End(); Backend?.ReportImageWorkload(); PumpTimer?.Stop();'
Add-Probe 'App.xaml.cs' 'bool FrameQualification = Arguments.Contains("--frame-qualification");' 'FrameTimingProbe.Begin(); bool FrameQualification = Arguments.Contains("--frame-qualification");'
Add-Probe 'App.xaml.cs' 'if (Backend is not null) await Backend.WaitForImageRetirementAsync();' 'if (Backend is not null) { await Backend.WaitForImageRetirementAsync(); await Backend.WaitForFrameWorkloadRetirementAsync(); }'
Add-Probe 'WinUIBackend.cs' 'LuiDiagnostics.Log("WinUI", Message);' 'if (!ImageWorkload) LuiDiagnostics.Log("WinUI", Message);'
Add-Probe 'WinUIBackend.cs' 'OwnerPumpObservation?.Invoke();' 'ObserveOwnerWorkload(); OwnerPumpObservation?.Invoke();'
Add-Probe 'App.xaml.cs' 'PumpTimer.Tick+=(_,_)=>Backend.Pump();' 'PumpTimer.Tick+=(_,_)=>{FrameTimingProbe.TimerTick();Backend.Pump();};'
Add-Probe 'Images.cs' 'await Bitmap.SetSourceAsync(Stream)' '++ImageWorkloadDecodes; await Bitmap.SetSourceAsync(Stream)'
Add-Probe 'Frames.cs' 'internal long Started;' 'internal long Started, CopyAt, CopiedAt, PollAt, FenceAt, ReadyAt, CompleteAt, PublishedAt;'
Add-Probe 'Frames.cs' 'Native.LuiWinUI_FrameDeviceCreateV1(0,out IntPtr Value)' 'Native.LuiWinUI_FrameDeviceCreateV1(FrameTimingProbe.Warp ? 1u : 0u,out IntPtr Value)'
Add-Probe 'Frames.cs' 'int CopyResult=await Task.Run(()=>Native.LuiWinUI_FrameCopyV1(Copy));' 'int CopyResult=await Task.Run(()=>{Upload.CopyAt=System.Diagnostics.Stopwatch.GetTimestamp();int Result=Native.LuiWinUI_FrameCopyV1(Copy);Upload.CopiedAt=System.Diagnostics.Stopwatch.GetTimestamp();return Result;});'
$FrameSource = Get-Content -Raw -LiteralPath (Join-Path $Backend 'Frames.cs')
if (!$FrameSource.Contains('FrameTimingProbe.PollFence')) {
    $FenceWait = 'int Status=await WaitForUploadFenceAsync(Upload.Gpu);'
    if (!$FrameSource.Contains($FenceWait)) {
        $FenceWait = 'int Status;while((Status=Native.LuiWinUI_FramePollV1(Upload.Gpu))==0)await Task.Delay(1);'
    }
    Add-Probe 'Frames.cs' $FenceWait "Upload.PollAt=Stopwatch.GetTimestamp();${FenceWait}Upload.FenceAt=Stopwatch.GetTimestamp();"
}
Add-Probe 'Frames.cs' 'Upload.ResourcePrepared.TrySetResult();' 'Upload.ReadyAt=Stopwatch.GetTimestamp();Upload.ResourcePrepared.TrySetResult();'
Add-Probe 'Frames.cs' 'Upload.Completed=true;' 'Upload.Completed=true;Upload.CompleteAt=Stopwatch.GetTimestamp();'
Add-Probe 'Frames.cs' '++FramePublished;' 'Upload.PublishedAt=Stopwatch.GetTimestamp();ObserveNativePublication(Upload);++FramePublished;'
Add-Probe 'Frames.cs' 'Upload.Submitted=true;FrameSample' 'Upload.Submitted=true;ObserveFrameWorkload(Upload);FrameSample'
Add-Probe 'Frames.cs' 'Upload.Working=false;if(Counted)' 'ObserveFrameCopy(Upload);Upload.Working=false;if(Counted)'
if ($MatchedThroughput) {
    Add-Probe 'FrameDeadline.cs' 'EnableFrameSettlement = true;' 'EnableFrameSettlement = Environment.GetEnvironmentVariable("LUI_FRAME_MATCHED_FULL_SETTLEMENT") != "1";'
}
if ($PassiveDisplay) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'PassivePresentation.cs') -Destination $Backend
    Add-Probe 'WinUIBackend.cs' 'if (Value == "true") Target.Window.Activate();' 'if (Value == "true") PassivePresentation.Show(Target.Window);'
}
if ($SchedulingExperiment) {
    Add-Probe 'App.xaml.cs' 'Interval=TimeSpan.FromMilliseconds(16)' 'Interval=TimeSpan.FromMilliseconds(FrameTimingProbe.OwnerTimerMilliseconds)'
    Add-Probe 'App.xaml.cs' 'FrameTimingProbe.TimerTick();Backend.Pump();}; PumpTimer.Start();' 'FrameTimingProbe.TimerTick();Backend.Pump();}; PumpTimer.Start(); FrameTimingProbe.StartThreadTimer(Backend);'
    Add-Probe 'Frames.cs' 'if(FrameGpuPauseForTest)Marshal.ThrowExceptionForHR' 'if(!Recovery&&SkipSupersededFrame(Upload,1))return; if(FrameGpuPauseForTest)Marshal.ThrowExceptionForHR'
    Add-Probe 'Frames.cs' 'Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameSurfaceCreateV1(Upload.Gpu));' 'if(!Recovery&&SkipSupersededFrame(Upload,2))return; Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameSurfaceCreateV1(Upload.Gpu));'
    Add-Probe 'Frames.cs' 'long Began=Stopwatch.GetTimestamp();' 'if(!Recovery&&SkipSupersededFrame(Upload,0))return; long Began=Stopwatch.GetTimestamp();'
    Add-Probe 'Frames.cs' 'int Status=await WaitForUploadFenceAsync(Upload.Gpu);' 'int Status; if(FrameTimingProbe.PollFence){while((Status=Native.LuiWinUI_FramePollV1(Upload.Gpu))==0)await Task.Delay(1);}else Status=await WaitForUploadFenceAsync(Upload.Gpu);'
}
@'
using System.Diagnostics;
using System.Runtime.InteropServices;
namespace Lui.WinUI;

internal static class FrameTimingProbe
{
    [DllImport("winmm.dll", EntryPoint = "timeBeginPeriod")]
    private static extern uint TimeBeginPeriod(uint Milliseconds);
    [DllImport("winmm.dll", EntryPoint = "timeEndPeriod")]
    private static extern uint TimeEndPeriod(uint Milliseconds);
    private static bool Requested;
    internal static readonly bool PollFence = Environment.GetEnvironmentVariable("LUI_FRAME_POLL_FENCE") == "1";
    internal static readonly bool Warp = Environment.GetEnvironmentVariable("LUI_FRAME_FORCE_WARP") == "1";
    internal static readonly int OwnerTimerMilliseconds = Environment.GetEnvironmentVariable("LUI_FRAME_OWNER_TIMER") switch {
        "4" => 4, "8" => 8, _ => 16,
    };
    private static readonly bool Active = Environment.GetEnvironmentVariable("LUI_IMAGE_WORKLOAD_LOG_DIR") is not null;
    private static readonly List<double> TimerIntervals = new();
    private static long LastTimerTick;
    private static int TimerTicks;
    private static System.Threading.Timer? ThreadTimer;
    private static int ThreadTimerRunning, ThreadTimerQueued;
    private static int ThreadTimerDeliveries;

    internal static void StartThreadTimer(WinUIBackend Backend)
    {
        if (!Active || Environment.GetEnvironmentVariable("LUI_FRAME_THREAD_TIMER") != "1") return;
        var Queue = Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread();
        Volatile.Write(ref ThreadTimerRunning, 1);
        ThreadTimer = new System.Threading.Timer(_ => {
            if (Volatile.Read(ref ThreadTimerRunning) == 0 || Interlocked.Exchange(ref ThreadTimerQueued, 1) != 0) return;
            if (!Queue.TryEnqueue(() => {
                Interlocked.Exchange(ref ThreadTimerQueued, 0);
                if (Volatile.Read(ref ThreadTimerRunning) == 0) return;
                ++ThreadTimerDeliveries;
                Backend.Pump();
            })) Interlocked.Exchange(ref ThreadTimerQueued, 0);
        }, null, 16, 16);
    }

    internal static void TimerTick()
    {
        if (!Active) return;
        long Now = Stopwatch.GetTimestamp();
        ++TimerTicks;
        if (LastTimerTick != 0 && TimerIntervals.Count < 4096)
            TimerIntervals.Add(Stopwatch.GetElapsedTime(LastTimerTick, Now).TotalMilliseconds);
        LastTimerTick = Now;
    }

    internal static void ReportTimer()
    {
        if (TimerIntervals.Count == 0) return;
        TimerIntervals.Sort();
        double Gap(double P) => TimerIntervals[(int)Math.Ceiling(TimerIntervals.Count * P) - 1];
        LuiDiagnostics.Log("ImageResources", FormattableString.Invariant(
            $"stage=OwnerTimer configuredMs={OwnerTimerMilliseconds} ticks={TimerTicks} threadTimer={ThreadTimerDeliveries} p50Ms={Gap(.50):F3} p95Ms={Gap(.95):F3} p99Ms={Gap(.99):F3}"));
    }

    internal static void Begin()
    {
        if (Requested || Environment.GetEnvironmentVariable("LUI_IMAGE_WORKLOAD_LOG_DIR") is null ||
            Environment.GetEnvironmentVariable("LUI_FRAME_TIMER_PROBE") != "1") return;
        Requested = TimeBeginPeriod(1) == 0;
        LuiDiagnostics.Log("ImageResources", $"timerProbe1ms={Requested}");
    }

    internal static void End()
    {
        Volatile.Write(ref ThreadTimerRunning, 0);
        ThreadTimer?.Dispose();
        ThreadTimer = null;
        if (!Requested) return;
        Requested = false;
        _ = TimeEndPeriod(1);
    }
}

internal sealed partial class WinUIBackend
{
    private static readonly bool ImageWorkload = Environment.GetEnvironmentVariable("LUI_IMAGE_WORKLOAD_LOG_DIR") is not null;
    private long ImageWorkloadDecodes;
    private readonly Queue<double>[] FramePhases = Enumerable.Range(0, 8).Select(_ => new Queue<double>()).ToArray();
    private readonly Queue<double> NativePublicationGaps = new();
    private readonly List<double> AttemptedFrameCopies = new();
    private readonly List<(ulong Sequence, long Time)> PublicationTimes = new();
    private readonly List<(ulong Sequence, long Time)> RenderingTimes = new();
    private readonly int[] SupersededFrames = new int[3];
    private readonly List<double> OwnerIntervals = new();
    private long LastOwnerWork;
    private int OwnerTurns;
    private static readonly bool ProbeSkipSuperseded = Environment.GetEnvironmentVariable("LUI_FRAME_SKIP_SUPERSEDED") == "1";
    private long FirstNativePublication, LastNativePublication;

    [DllImport("LuiRuntime.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Lui_FrameStillCurrentProbeV1(IntPtr Runtime, IntPtr Frame);

    private bool SkipSupersededFrame(FrameUpload Upload, int Stage)
    {
        if (!ProbeSkipSuperseded || Lui_FrameStillCurrentProbeV1(Runtime, Upload.Lease) == 1) return false;
        ++SupersededFrames[Stage];
        return true;
    }

    private void ObserveOwnerWorkload()
    {
        if (!ImageWorkload) return;
        long Now = Stopwatch.GetTimestamp();
        ++OwnerTurns;
        if (LastOwnerWork != 0 && OwnerIntervals.Count < 4096)
            OwnerIntervals.Add(Stopwatch.GetElapsedTime(LastOwnerWork, Now).TotalMilliseconds);
        LastOwnerWork = Now;
    }

    private void ObserveNativePublication(FrameUpload Upload)
    {
        if (!ImageWorkload) return;
        long Now = Upload.PublishedAt;
        if (PublicationTimes.Count < 4096) PublicationTimes.Add((Upload.Sequence, Now));
        if (LastNativePublication != 0)
            FrameSample(NativePublicationGaps, Stopwatch.GetElapsedTime(LastNativePublication, Now).TotalMilliseconds);
        else FirstNativePublication = Now;
        LastNativePublication = Now;
    }

    private void ObserveFrameWorkload(FrameUpload Upload)
    {
        if (!ImageWorkload || Upload.PublishedAt == 0 || Upload.CopyAt == 0) return;
        if (RenderingTimes.Count < 4096) RenderingTimes.Add((Upload.Sequence, Stopwatch.GetTimestamp()));
        Span<long> Points = stackalloc long[] { Upload.Started, Upload.CopyAt, Upload.CopiedAt,
            Upload.PollAt, Upload.FenceAt, Upload.ReadyAt, Upload.CompleteAt, Upload.PublishedAt, Stopwatch.GetTimestamp() };
        for (int Index = 0; Index < FramePhases.Length; Index++)
            FrameSample(FramePhases[Index], Stopwatch.GetElapsedTime(Points[Index], Points[Index+1]).TotalMilliseconds);
    }

    private void ObserveFrameCopy(FrameUpload Upload)
    {
        if (ImageWorkload && Upload.CopyAt != 0 && Upload.CopiedAt != 0 && AttemptedFrameCopies.Count < 4096)
            AttemptedFrameCopies.Add(Stopwatch.GetElapsedTime(Upload.CopyAt, Upload.CopiedAt).TotalMilliseconds);
    }

    internal async Task WaitForFrameWorkloadRetirementAsync()
    {
        if (!ImageWorkload || !FrameResources.Any() && FramePublished == 0 && FrameUploadSamples.Count == 0) return;
        long Started = Stopwatch.GetTimestamp();
        Native.FrameGpuStats Gpu;
        while (true) {
            Gpu = GetFrameGpuStats();
            if (Gpu.Uploads == 0 && Gpu.Devices == 0 && Gpu.Frames == 0 && Gpu.Sources == 0 && FrameResources.Count == 0) break;
            if (Stopwatch.GetElapsedTime(Started) >= TimeSpan.FromSeconds(2)) break;
            await Task.Delay(4);
        }
        LuiDiagnostics.Log("ImageResources", FormattableString.Invariant(
            $"stage=FrameRetirement shutdownMs={Stopwatch.GetElapsedTime(Started).TotalMilliseconds:F3} leases={FrameResources.Count} uploads={Gpu.Uploads} uploadBytes={Gpu.UploadBytes} sources={Gpu.Sources} gpuFrames={Gpu.Frames} devices={Gpu.Devices}"));
    }

    private static void ReportFrameTimes(string Name, List<(ulong Sequence, long Time)> Times)
    {
        double ToMilliseconds = 1000.0 / Stopwatch.Frequency;
        for (int Offset = 0; Offset < Times.Count; Offset += 32) {
            var Batch = Times.Skip(Offset).Take(32).Select(Item =>
                FormattableString.Invariant($"{Item.Sequence}:{Item.Time * ToMilliseconds:F6}"));
            LuiDiagnostics.Log("ImageResources", $"{Name} ms={string.Join(",", Batch)}");
        }
    }

    internal void ReportImageWorkload()
    {
        if (!ImageWorkload) return;
        FrameTimingProbe.ReportTimer();
        if (OwnerIntervals.Count != 0) {
            OwnerIntervals.Sort();
            double Gap(double P) => OwnerIntervals[(int)Math.Ceiling(OwnerIntervals.Count * P) - 1];
            LuiDiagnostics.Log("ImageResources", FormattableString.Invariant(
                $"stage=OwnerPump turns={OwnerTurns} p50Ms={Gap(.50):F3} p95Ms={Gap(.95):F3} p99Ms={Gap(.99):F3}"));
        }
        var Sorted = FrameSubmissionSamples.Order().ToArray();
        double Percent(double P) => Sorted.Length == 0 ? 0 : Sorted[(int)Math.Ceiling(Sorted.Length * P) - 1];
        var Stats = GetFrameStats();
        LuiDiagnostics.Log("ImageResources", FormattableString.Invariant(
            $"sdkDecodes={ImageWorkloadDecodes} published={FramePublished} frameSamples={Sorted.Length} frameSubmitP50Ms={Percent(.50):F3} frameSubmitP95Ms={Percent(.95):F3} frameSubmitP99Ms={Percent(.99):F3} frameSubmitMaxMs={Percent(1):F3} replaced={Stats.Replaced} stale={Stats.Stale} failures={FrameUploadFailures}"));
        if (PublicationTimes.Count != 0 || SupersededFrames.Any(Count => Count != 0))
            LuiDiagnostics.Log("ImageResources", $"stage=WorkAvoidance beforeCopy={SupersededFrames[0]} beforeGpuSubmit={SupersededFrames[1]} beforeSurface={SupersededFrames[2]}");
        ReportFrameTimes("PublicationTimes", PublicationTimes);
        ReportFrameTimes("RenderingTimes", RenderingTimes);
        if (AttemptedFrameCopies.Count != 0) {
            AttemptedFrameCopies.Sort();
            double Rank(double P) => AttemptedFrameCopies[(int)Math.Ceiling(AttemptedFrameCopies.Count * P) - 1];
            LuiDiagnostics.Log("ImageResources", FormattableString.Invariant(
                $"stage=AttemptedFrameCopies forceWarp={FrameTimingProbe.Warp} n={AttemptedFrameCopies.Count} p50Ms={Rank(.50):F3} p95Ms={Rank(.95):F3} p99Ms={Rank(.99):F3}"));
        }
        if (NativePublicationGaps.Count != 0)
        {
            var Gpu = GetFrameGpuStats();
            var Gaps = NativePublicationGaps.Order().ToArray();
            double Gap(double P) => Gaps[(int)Math.Ceiling(Gaps.Length * P) - 1];
            double Elapsed = Stopwatch.GetElapsedTime(FirstNativePublication, LastNativePublication).TotalSeconds;
            LuiDiagnostics.Log("ImageResources", FormattableString.Invariant(
                $"stage=NativePublication seconds={Elapsed:F3} hz={(FramePublished - 1) / Elapsed:F3} gapSamples={Gaps.Length} gapP50Ms={Gap(.50):F3} gapP95Ms={Gap(.95):F3} gapP99Ms={Gap(.99):F3} gapMaxMs={Gaps[^1]:F3} gpuCopies={Gpu.Copies} gpuDraws={Gpu.Draws} peakUploadBytes={Gpu.PeakUploadBytes}"));
        }
        string[] Phases = ["StartToCopy", "CpuCopy", "CopyToFencePoll", "FencePoll", "Surface", "SurfaceToComplete", "CompleteToPublish", "PublishToRendering"];
        for (int Index = 0; Index < FramePhases.Length; Index++)
        {
            var Values = FramePhases[Index].Order().ToArray();
            if (Values.Length == 0) continue;
            LuiDiagnostics.Log("ImageResources", FormattableString.Invariant(
                $"phase={Phases[Index]} n={Values.Length} p50Ms={Values[(int)Math.Ceiling(Values.Length*.50)-1]:F3} p95Ms={Values[(int)Math.Ceiling(Values.Length*.95)-1]:F3} p99Ms={Values[(int)Math.Ceiling(Values.Length*.99)-1]:F3} maxMs={Values[^1]:F3}"));
        }
    }
}
'@ | Set-Content -LiteralPath (Join-Path $Backend 'ImageWorkloadDiagnostics.cs') -Encoding ascii
if ($MatchedThroughput) {
    Add-Probe 'ImageWorkloadDiagnostics.cs' 'FrameTimingProbe.ReportTimer();' 'FrameTimingProbe.ReportTimer(); LuiDiagnostics.Log("ImageResources", $"stage=MatchedSettlement mode={(EnableFrameSettlement ? "Scoped" : "Full")}");'
}
