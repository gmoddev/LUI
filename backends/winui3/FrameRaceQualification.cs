using System.Diagnostics;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    private sealed class FrameQualificationFailure(string Code):Exception("FrameQualificationFailed") {internal readonly string Check=Code;}
    private static void FrameRequire(bool Value,string Code){if(!Value)throw new FrameQualificationFailure(Code);}
    private async Task FrameUntilAsync(Func<bool> Ready) {
        var Clock=Stopwatch.StartNew();
        while(!Ready()){FrameRequire(Clock.Elapsed.TotalSeconds<=10,"FrameBarrierTimeout");Pump();await Task.Delay(1);}
    }
    private void InvokeFrameInput() {
        var Button=Views.Values.Select(Value=>Value.Element).OfType<Button>().Single();
        ((IInvokeProvider)FrameworkElementAutomationPeer.CreatePeerForElement(Button).GetPattern(PatternInterface.Invoke)).Invoke();
    }
    private async Task QualifyActiveFrameCopiesAsync() {
        for(int Action=0;Action<2;++Action) {
            using var Entered=new EventWaitHandle(false,EventResetMode.ManualReset);
            using var Resume=new EventWaitHandle(false,EventResetMode.ManualReset);
            FrameCopyEntered=Entered;FrameCopyResume=Resume;
            FrameUpload? Upload=null;
            try {
                FrameRun("Before=Count;Pattern()");Pump();Upload=SurfaceView().PreparedFrame!;
                FrameRequire(await Task.Run(()=>Entered.WaitOne(TimeSpan.FromSeconds(5))),"ActiveCopyEntry");
                FrameRequire(GetFrameGpuStats().UploadBytes>0&&Upload.Working,"ActiveCopyAllocation");
                var Clock=Stopwatch.StartNew();
                FrameRun(Action==0?"S:ClearFrame();assert(S.FrameState=='Empty')":"S:Destroy();S:Destroy()");
                FrameRequire(Clock.Elapsed.TotalMilliseconds<100,"ActiveCopyOwnerBlocked");
                InvokeFrameInput();FrameRun("assert(Count==Before+1)");
                FrameRequire(GetFrameGpuStats().UploadBytes>0&&Native.LuiWinUI_FrameReleaseV1(Upload.Gpu)==0,"ActiveCopyPrematureRelease");
            } finally {FrameCopyEntered=null;FrameCopyResume=null;Resume.Set();}
            await Upload!.WorkFinished.Task.WaitAsync(TimeSpan.FromSeconds(5));Pump();
            await Upload.ReleaseFinished.Task.WaitAsync(TimeSpan.FromSeconds(10));await WaitRetiredFramesAsync();Pump();
            FrameRequire(FrameResources.Count==0&&GetFrameGpuStats().UploadBytes==0&&GetFrameStats().StagingBytes==0,"ActiveCopyQuiescence");
            if(Action==1)FrameRun("S=Instance.new('FrameSurface',{Parent=Root,Size=UDim2.fromOffset(500,280),ScaleMode='Stretch'})");
            FrameRun("Pattern()");await FlushFrameAsync();
        }
        using var ReloadEntered=new EventWaitHandle(false,EventResetMode.ManualReset);
        using var ReloadResume=new EventWaitHandle(false,EventResetMode.ManualReset);
        var Retired=new WinUIBackend(){FrameCopyEntered=ReloadEntered,FrameCopyResume=ReloadResume};
        FrameUpload? Old=null;
        try {
            Retired.FrameRun(FrameScene);Retired.Pump();Old=Retired.SurfaceView().PreparedFrame!;
            FrameRequire(await Task.Run(()=>ReloadEntered.WaitOne(TimeSpan.FromSeconds(5))),"ActiveReloadEntry");
            var Device=Retired.FrameDevice;var Clock=Stopwatch.StartNew();Retired.Dispose();
            FrameRequire(Clock.Elapsed.TotalMilliseconds<100&&GetFrameGpuStats().UploadBytes>0,"ActiveReloadOwnerBlocked");
            using var Replacement=new WinUIBackend();Replacement.FrameRun(FrameScene);await Replacement.FlushFrameAsync();
            FrameRequire(Replacement.FramePublished==1&&Replacement.SurfaceView().DisplayedFrame!.Epoch==Old.Epoch,"ActiveReloadIdentity");
            ReloadResume.Set();await Old.WorkFinished.Task.WaitAsync(TimeSpan.FromSeconds(5));await Old.ReleaseFinished.Task.WaitAsync(TimeSpan.FromSeconds(10));
            await Retired.WaitRetiredFramesAsync();if(Device?.Retirement is {} Cleanup)await Cleanup.WaitAsync(TimeSpan.FromSeconds(5));
            FrameRequire(Retired.FrameResources.Count==0&&Retired.FrameUploadsInProgress==0&&Replacement.FramePublished==1,"ActiveReloadLatePublication");
            var ReplacementDevice=Replacement.FrameDevice;Replacement.Dispose();await Replacement.WaitRetiredFramesAsync();
            if(ReplacementDevice?.Retirement is {} ReplacementCleanup)await ReplacementCleanup.WaitAsync(TimeSpan.FromSeconds(5));
        } finally {ReloadResume.Set();Retired.Dispose();}
        var Faulted=new WinUIBackend();Faulted.FrameRun(FrameScene);await Faulted.FlushFrameAsync();
        var FaultedDevice=Faulted.FrameDevice;var Displayed=Faulted.SurfaceView().DisplayedFrame!;
        Faulted.FrameQualificationFault=3;Faulted.Dispose(); // No further old-backend Pump is permitted.
        await Displayed.ReleaseFinished.Task.WaitAsync(TimeSpan.FromSeconds(10));
        if(FaultedDevice?.Retirement is {} FaultedCleanup)await FaultedCleanup.WaitAsync(TimeSpan.FromSeconds(5));
        FrameRequire(Faulted.FrameResources.Count==0&&Faulted.FrameUploadsInProgress==0,"ShutdownDetachRetry");
    }
    private async Task QualifyFrameGpuCancellationAsync() {
        FrameGpuPauseForTest=true;FrameUpload? Upload=null;
        try {
            FrameRun("Pattern()");Pump();Upload=SurfaceView().PreparedFrame!;
            await Upload.GpuSubmitted.Task.WaitAsync(TimeSpan.FromSeconds(5));
            FrameRequire(GetFrameGpuStats().UploadBytes>0&&Native.LuiWinUI_FramePollV1(Upload.Gpu)==0,"GpuReadEntry");
            FrameRun("Before=Count;S:ClearFrame();assert(S.FrameState=='Empty')");InvokeFrameInput();FrameRun("assert(Count==Before+1)");
            FrameRequire(GetFrameGpuStats().UploadBytes>0,"GpuReadPrematureRelease");
        } finally {FrameGpuPauseForTest=false;if(Upload?.Gpu is {} Handle&&Handle!=IntPtr.Zero)Native.LuiWinUI_FrameGpuGateForTestV1(Handle,0);}
        await Upload!.Finished.Task.WaitAsync(TimeSpan.FromSeconds(5));Pump();await WaitRetiredFramesAsync();Pump();
        FrameRun("assert(S.FrameState=='Empty');Pattern()");await FlushFrameAsync();
        FrameRequire(GetFrameGpuStats().UploadBytes==0,"GpuReadRelease");
    }
    private async Task QualifyFrameDeviceLossAsync(string DirectoryPath) {
        for(int Action=0;Action<3;++Action) {
            FrameGpuPauseForTest=Action>0;FrameUpload? Pending=null;
            try {
                if(Action>0){FrameRun("Pattern()");Pump();Pending=SurfaceView().PreparedFrame!;await Pending.GpuSubmitted.Task.WaitAsync(TimeSpan.FromSeconds(5));}
                var Ticket=FrameDevice!;IntPtr Device=await Ticket.Creation;
                FrameRequire(Native.LuiWinUI_FrameDeviceRemoveForTestV1(Device)<0,"DeviceRemovalReason");
                ulong Before=FrameRecoveries;
                if(Action==2)FrameRun("S:ClearFrame();assert(S.FrameState=='Empty')");
                Pump();FrameGpuPauseForTest=false;
                if(Pending is not null){await Pending.Finished.Task.WaitAsync(TimeSpan.FromSeconds(5));Pump();}
                if(Action==2){
                    await WaitRetiredFramesAsync();Pump();FrameRequire(FrameRecoveries==Before&&FrameResources.Count==0,"RemovedClearedPublication");
                    FrameRun("assert(S.FrameState=='Empty');Pattern()");await FlushFrameAsync();
                }else{
                    await FrameUntilAsync(()=>FrameRecoveries>Before);await WaitRetiredFramesAsync();
                }
                if(Ticket.Retirement is {} Cleanup)await Cleanup.WaitAsync(TimeSpan.FromSeconds(5));
                FrameRequire(FrameDevice is {Retired:false} Current&&Current.Creation.IsCompletedSuccessfully&&Native.LuiWinUI_FrameDeviceRemovedV1(Current.Creation.Result)>=0,"DeviceRecreation");
                var Root=(Microsoft.UI.Xaml.FrameworkElement)Views.Values.Single(Value=>Value.Window is not null).Window!.Content;
                var Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,$"12-recovery-{Action}.png"),Root);
                CheckPixel(Pixels,Root,50,40,0,255,0);
            }finally{FrameGpuPauseForTest=false;}
        }
    }
    private async Task QualifyRecoveryOrderingAsync(string DirectoryPath) {
        PauseFrameQualificationPump=true;
        try {
            for(int Action=0;Action<2;++Action) {
                using var Entered=new EventWaitHandle(false,EventResetMode.ManualReset);
                using var Resume=new EventWaitHandle(false,EventResetMode.ManualReset);
                var Displayed=SurfaceView().DisplayedFrame!;ulong Before=FrameRecoveries;
                FrameCopyEntered=Entered;FrameCopyResume=Resume;
                try {
                    FrameRequire(Native.LuiWinUI_FrameDeviceRemoveForTestV1((await FrameDevice!.Creation))<0,"OrderingDeviceRemoval");
                    await FrameUntilAsync(()=>Entered.WaitOne(0));
                    FrameRequire(Displayed.Working&&SurfaceView().PreparedFrame is null,"RecoveryCopyHeld");
                    if(Action==0)FrameRun("B=buffer.create(48);for O=0,44,4 do buffer.writeu32(B,O,0xffff0000) end;Pattern()");
                    else FrameRun("S.Parent=nil;S.Visible=false");
                }finally{FrameCopyEntered=null;FrameCopyResume=null;Resume.Set();}
                await Displayed.WorkFinished.Task.WaitAsync(TimeSpan.FromSeconds(5));
                FrameRequire(FrameRecoveries==Before,"RecoveryOutranOwnerPendingWork");
                if(Action==0){
                    FrameRequire(SurfaceView().PreparedFrame is not null,"OwnerPendingSubmissionNotDrained");
                    await FlushFrameAsync();
                    var Root=(Microsoft.UI.Xaml.FrameworkElement)Views.Values.Single(Value=>Value.Window is not null).Window!.Content;
                    var Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"13-pending-wins.png"),Root);CheckPixel(Pixels,Root,50,40,255,0,0);
                    FrameRun("B=buffer.create(48);for O=0,44,4 do buffer.writeu32(B,O,0xff00ff00) end;Pattern()");await FlushFrameAsync();
                }else{
                    FrameRun("S.Parent=Root;S.Visible=true");await FrameUntilAsync(()=>FrameRecoveries>Before);
                }
            }
            // Completed preparation is still cancelable before the owner has
            // published it. The released PreparedFrame pointer must not strand
            // recovery of the previous displayed resource.
            FrameRun("Pattern()");Pump();var Ready=SurfaceView().PreparedFrame!;
            await Ready.Finished.Task.WaitAsync(TimeSpan.FromSeconds(5));FrameRequire(Ready.Ready,"PreparedReady");
            ulong Recoveries=FrameRecoveries;
            FrameRequire(Native.LuiWinUI_FrameDeviceRemoveForTestV1((await FrameDevice!.Creation))<0,"ReadyDeviceRemoval");
            Pump();await FrameUntilAsync(()=>FrameRecoveries>Recoveries);await WaitRetiredFramesAsync();
            FrameRequire(SurfaceView().PreparedFrame is null&&!ReferenceEquals(SurfaceView().DisplayedFrame,Ready),"ReleasedPreparationBlocksRecovery");
        }finally{PauseFrameQualificationPump=false;FrameCopyEntered=null;FrameCopyResume=null;}
    }
    private async Task QualifySurfaceLossAsync(string DirectoryPath) {
        PauseFrameQualificationPump=true;
        try {
            for(int Action=0;Action<2;++Action) {
                var Gate=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);FrameUploadBarrier=Gate.Task;
                FrameUpload Upload;ulong Draws=GetFrameGpuStats().Draws;
                try {
                    if(Action==0){FrameRun("Pattern()");Pump();Upload=SurfaceView().PreparedFrame!;await Upload.ResourcePrepared.Task.WaitAsync(TimeSpan.FromSeconds(5));}
                    else {
                        Upload=SurfaceView().DisplayedFrame!;
                        FrameRequire(Native.LuiWinUI_FrameDeviceRemoveForTestV1((await FrameDevice!.Creation))<0,"SurfaceRecoveryDeviceRemoval");
                        await FrameUntilAsync(()=>Upload.Working&&Upload.ResourcePrepared.Task.IsCompleted);
                    }
                    OnFrameSurfaceContentsLost(null,null!);Pump();FrameRequire(Upload.SurfaceInvalidated,"SurfaceLossNotTracked");
                }finally{FrameUploadBarrier=null;Gate.TrySetResult();}
                await Upload.WorkFinished.Task.WaitAsync(TimeSpan.FromSeconds(5));Pump();await WaitRetiredFramesAsync();
                FrameRequire(!SurfaceView().DisplayedFrame!.SurfaceInvalidated&&GetFrameGpuStats().Draws>=Draws+2,"SurfaceLossNotRecreated");
                var Root=(Microsoft.UI.Xaml.FrameworkElement)Views.Values.Single(Value=>Value.Window is not null).Window!.Content;
                var Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,$"14-surface-loss-{Action}.png"),Root);CheckPixel(Pixels,Root,50,40,0,255,0);
            }
            FrameRun("S.Visible=false");Pump();var Hidden=SurfaceView().DisplayedFrame!;ulong Before=GetFrameGpuStats().Draws;
            OnFrameSurfaceContentsLost(null,null!);Pump();
            FrameRequire(Hidden.SurfaceInvalidated&&GetFrameGpuStats().Draws==Before,"HiddenLossPreparedWork");
            FrameRun("S.Visible=true");await FrameUntilAsync(()=>!Hidden.SurfaceInvalidated);
            FrameRequire(GetFrameGpuStats().Draws>Before,"HiddenSurfaceNeverRestored");
            // Retry the same detach and scheduling faults while retaining the
            // displayed CPU snapshot for recovery, rather than normal Clear.
            foreach(int Fault in new[]{3,5}) {
                FrameQualificationFault=Fault;ulong Count=FrameRecoveries;
                FrameRequire(Native.LuiWinUI_FrameDeviceRemoveForTestV1((await FrameDevice!.Creation))<0,"RecoveryFaultDeviceRemoval");
                await FrameUntilAsync(()=>FrameRecoveries>Count);
                FrameRequire(!SurfaceView().DisplayedFrame!.RetirementRequested,"RecoveryRetirementStranded");
            }
        }finally{FrameUploadBarrier=null;PauseFrameQualificationPump=false;}
    }
}
