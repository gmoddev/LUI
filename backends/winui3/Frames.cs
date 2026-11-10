using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using WinRT;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    private const ulong MaxFrameStaging = 8 * 1024 * 1024;
    private sealed class FrameDeviceTicket {
        internal required Task<IntPtr> Creation;
        internal bool Retired;
        internal Task? Retirement;
    }
    private sealed class FrameUpload {
        internal IntPtr Lease, Gpu, RetiredGpu;
        internal ulong Epoch, Sequence;
        internal required View Target;
        internal FrameDeviceTicket? Device;
        internal bool Canceled, Ready, Released, Submitted, Completed, Attached, Working, Retiring, NeedsRecovery;
        internal bool SurfaceInvalidated, RetirementRequested, RetireKeepPixels;
        internal long Started;
        internal TaskCompletionSource Entered = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal TaskCompletionSource Finished = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal TaskCompletionSource WorkFinished = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal TaskCompletionSource ReleaseFinished = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal TaskCompletionSource GpuSubmitted = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal TaskCompletionSource ResourcePrepared = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
    private FrameDeviceTicket? FrameDevice;
    private readonly HashSet<FrameUpload> FrameResources = new();
    private readonly Queue<double> FrameUploadSamples = new(), FrameSubmissionSamples = new();
    private Native.FrameSubmitCallback? FrameSubmitCallback, FramePublishCallback;
    private Native.FrameClearCallback? FrameClearCallback;
    private ulong FramePublished, FrameUploadFailures, FrameStagingRefusals, FrameDeviceLosses, FrameRecoveries;
    private int FrameUploadsInProgress, FrameLastHResult;
    private ulong FramePeakPixels, FramePeakStaging, FramePeakAccounted, FrameMaxPending, FrameMaxInFlight, FrameMaxAllocations;
    // Qualification controls do not change production scheduling or copy logic.
    private Task? FrameUploadBarrier;
    private EventWaitHandle? FrameCopyEntered, FrameCopyResume;
    private int FrameQualificationFault;
    private bool FrameGpuPauseForTest;
    private bool FrameSurfacesInvalidated;
    private void StartFrames() {
        try {if(Native.LuiWinUI_FramePresenterVersionV1()!=1)return;}
        catch(DllNotFoundException){return;}catch(EntryPointNotFoundException){return;}
        FrameSubmitCallback=OnSubmitFrame;FramePublishCallback=OnPublishFrame;FrameClearCallback=OnClearFrame;
        Native.FrameCallbacks Callbacks=new(){StructSize=(uint)Marshal.SizeOf<Native.FrameCallbacks>(),AbiVersion=1,
            Submit=FrameSubmitCallback,Publish=FramePublishCallback,Clear=FrameClearCallback};
        if(Native.Lui_SetFrameCallbacks(Runtime,ref Callbacks)!=1)throw new InvalidOperationException("FrameBridgeFailed");
        CompositionTarget.SurfaceContentsLost+=OnFrameSurfaceContentsLost;
    }
    private FrameDeviceTicket FrameDeviceTicketValue() {
        if(FrameDevice is {} Current)return Current;
        // Allocate bookkeeping before starting native work, so failure cannot
        // orphan a successfully created native device in an unowned Task.
        var Ticket=new FrameDeviceTicket{Creation=Task.FromResult(IntPtr.Zero)};
        Ticket.Creation=Task.Run(()=>{
            Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameDeviceCreateV1(0,out IntPtr Value));return Value;
        });
        FrameDevice=Ticket;return Ticket;
    }
    private void RetireFrameDevice(FrameDeviceTicket Ticket) {
        Ticket.Retired=true;
        if(Ticket.Retirement is null) {
            try{Ticket.Retirement=Task.Run(async()=>{try{IntPtr Value=await Ticket.Creation;Native.LuiWinUI_FrameDeviceReleaseV1(Value);}catch{/* Creation failure owns no native handle. */}});}
            catch{++FrameUploadFailures;return;} // Keep the ticket reachable and retry on owner delivery.
        }
        if(ReferenceEquals(FrameDevice,Ticket))FrameDevice=null;
    }
    private void SampleFrameResources() {
        Native.FrameStats Stats=new(){StructSize=(uint)Marshal.SizeOf<Native.FrameStats>(),AbiVersion=1};
        if(Native.Lui_GetFrameStats(Runtime,ref Stats)!=1)return;
        FramePeakPixels=Math.Max(FramePeakPixels,Stats.PixelBytes);FramePeakStaging=Math.Max(FramePeakStaging,Stats.StagingBytes);
        FramePeakAccounted=Math.Max(FramePeakAccounted,Stats.PixelBytes+Stats.StagingBytes);
        FrameMaxPending=Math.Max(FrameMaxPending,Stats.Pending);FrameMaxInFlight=Math.Max(FrameMaxInFlight,Stats.InFlight);
        FrameMaxAllocations=Math.Max(FrameMaxAllocations,Stats.Frames);
    }
    private static void FrameSample(Queue<double> Samples,double Value) {Samples.Enqueue(Value);if(Samples.Count>1024)Samples.Dequeue();}
    private static Stretch FrameStretch(uint Mode)=>Mode switch{0=>Stretch.Uniform,1=>Stretch.UniformToFill,_=>Stretch.Fill};
    private static void AttachFrame(Image Picture,IntPtr Gpu) {
        IntPtr Pointer=((IWinRTObject)Picture).NativeObject.GetRef();
        try{Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameSurfaceAttachV1(Pointer,Gpu));}
        finally{Marshal.Release(Pointer);}
    }
    private int OnSubmitFrame(IntPtr Context,int Id,IntPtr Frame,uint Mode) {
        FrameUpload? Upload=null;bool Retained=false;
        try {
            if(IsDisposed||IsDisposing||Mode>2||!Views.TryGetValue(Id,out var Target)||!Target.IsAlive||Target.ClassName!="FrameSurface")return 0;
            Native.FrameView View=new(){StructSize=(uint)Marshal.SizeOf<Native.FrameView>(),AbiVersion=1};
            if(Native.Lui_FrameView(Frame,ref View)!=1||View.Format!=1||View.Pixels==IntPtr.Zero||View.Epoch!=Target.FrameEpoch||
                View.Width is <1 or >1920||View.Height is <1 or >1080||View.RowStride!=View.Width*4||View.ByteCount!=(ulong)View.RowStride*View.Height||View.ByteCount>MaxFrameStaging)return 0;
            if(Target.PreparedFrame is {} Previous){if(!Previous.Ready)return 0;Target.PreparedFrame=null;ReleaseFrame(Previous);}
            if(FrameQualificationFault==1){FrameQualificationFault=0;throw new OutOfMemoryException();}
            Upload=new FrameUpload{Target=Target,Epoch=View.Epoch,Sequence=View.Sequence,Started=Stopwatch.GetTimestamp()};
            FrameResources.Add(Upload);
            if(Native.Lui_FrameRetain(Frame)!=1){FrameResources.Remove(Upload);return 0;}
            Retained=true;Upload.Lease=Frame;
            if(FrameQualificationFault==2){FrameQualificationFault=0;throw new OutOfMemoryException();}
            if(Native.Lui_FrameReserveStaging(Frame,MaxFrameStaging)!=1){++FrameStagingRefusals;FinishFrameRelease(Upload);return 0;}
            Target.PreparedFrame=Upload;SampleFrameResources();
            _=PrepareFrameAsync(Upload,false,FrameUploadBarrier);return 1;
        } catch {
            ++FrameUploadFailures;
            if(Upload is not null){if(ReferenceEquals(Upload.Target.PreparedFrame,Upload))Upload.Target.PreparedFrame=null;
                if(Retained)FinishFrameRelease(Upload);else FrameResources.Remove(Upload);}
            return 0;
        }
    }
    private async Task PrepareFrameAsync(FrameUpload Upload,bool Recovery,Task? Barrier=null) {
        var Target=Upload.Target;bool Success=false,EnteredGate=false,Counted=false;SemaphoreSlim? Gate=null;
        try {
            if(FrameQualificationFault==4){FrameQualificationFault=0;throw new OutOfMemoryException();}
            Upload.WorkFinished=new(TaskCreationOptions.RunContinuationsAsynchronously);
            Upload.ResourcePrepared=new(TaskCreationOptions.RunContinuationsAsynchronously);
            Gate=Target.FramePrepareGate??=new SemaphoreSlim(1,1);
            Upload.Working=true;++FrameUploadsInProgress;Counted=true;
            await Task.Yield();await Gate.WaitAsync();EnteredGate=true;
            if(Upload.Canceled||IsDisposed||IsDisposing||!Target.IsAlive)return;
            var Ticket=FrameDeviceTicketValue();Upload.Device=Ticket;
            IntPtr Device=await Ticket.Creation;
            if(Upload.Canceled||Ticket.Retired||IsDisposed||IsDisposing)return;
            Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameDeviceBindOwnerV1(Device));
            long Began=Stopwatch.GetTimestamp();
            Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameBeginV1(Device,Upload.Lease,out Upload.Gpu));
            if(FrameCopyEntered is not null&&FrameCopyResume is not null)
                Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameCopyGateForTestV1(Upload.Gpu,FrameCopyEntered.SafeWaitHandle.DangerousGetHandle(),FrameCopyResume.SafeWaitHandle.DangerousGetHandle()));
            IntPtr Copy=Upload.Gpu;
            int CopyResult=await Task.Run(()=>Native.LuiWinUI_FrameCopyV1(Copy));
            if(Upload.Canceled||Ticket.Retired||IsDisposed||IsDisposing)return;
            Marshal.ThrowExceptionForHR(CopyResult);
            if(FrameGpuPauseForTest)Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameGpuGateForTestV1(Upload.Gpu,1));
            Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameSubmitCopyV1(Upload.Gpu));
            Upload.GpuSubmitted.TrySetResult();
            int Status;while((Status=Native.LuiWinUI_FramePollV1(Upload.Gpu))==0)await Task.Delay(1);
            if(Status!=1){LoseFrameDevice(Ticket);return;}
            if(Upload.Canceled||Ticket.Retired||IsDisposed||IsDisposing)return;
            Upload.SurfaceInvalidated=false;
            Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameSurfaceCreateV1(Upload.Gpu));
            Upload.ResourcePrepared.TrySetResult();
            if(!IsDisposed)SampleFrameResources();
            if(!Recovery)FrameSample(FrameUploadSamples,Stopwatch.GetElapsedTime(Began).TotalMilliseconds);
            Upload.Entered.TrySetResult();if(Barrier is not null)await Barrier;
            // Drain authoritative pending submissions/parenting/visibility before
            // restoring a displayed resource. Recovery never outruns a newer
            // accepted C++ pending frame merely because Submit has not run yet.
            if(Recovery&&!IsDisposed&&!IsDisposing)Pump();
            EnsureFrameSurface(Upload);
            Success=!Upload.Canceled&&!Ticket.Retired&&!IsDisposed&&!IsDisposing&&Target.IsAlive&&Target.FrameEpoch==Upload.Epoch&&
                (Recovery?ReferenceEquals(Target.DisplayedFrame,Upload)&&Target.PreparedFrame is null&&CanRecoverFrame(Target):ReferenceEquals(Target.PreparedFrame,Upload));
            if(Recovery&&Success&&Target.Content is Image Picture){AttachFrame(Picture,Upload.Gpu);Upload.Attached=true;Upload.NeedsRecovery=false;++FrameRecoveries;MarkPresentation();}
            else if(!Recovery)Upload.Ready=Success;
        } catch(Exception Error) {
            FrameLastHResult=Error.HResult;
            if(Upload.Device is {} Failed&&Failed.Creation.IsFaulted)RetireFrameDevice(Failed);
            if(Upload.Device is {} Ticket&&Ticket.Creation.IsCompletedSuccessfully&&!Ticket.Retired&&Native.LuiWinUI_FrameDeviceRemovedV1(Ticket.Creation.Result)<0)LoseFrameDevice(Ticket);
            if(!Upload.Canceled&&Upload.Device?.Retired!=true)++FrameUploadFailures;
        } finally {
            Upload.Working=false;if(Counted)--FrameUploadsInProgress;if(EnteredGate)Gate!.Release();
            if(!Recovery){Upload.Completed=true;Native.Lui_FrameComplete(Upload.Lease,Success?1:0);
                if(!Success){if(ReferenceEquals(Target.PreparedFrame,Upload))Target.PreparedFrame=null;ReleaseFrame(Upload);}}
            else if(!Success){if(Upload.Canceled||!ReferenceEquals(Target.DisplayedFrame,Upload))ReleaseFrame(Upload);else RetireFrameGpu(Upload,true);}
            Upload.Entered.TrySetResult();Upload.Finished.TrySetResult();Upload.WorkFinished.TrySetResult();
        }
    }
    private int OnPublishFrame(IntPtr Context,int Id,IntPtr Frame,uint Mode) {
        try {
            if(IsDisposed||IsDisposing||Mode>2||!Views.TryGetValue(Id,out var Target)||!Target.IsAlive||Target.Content is not Image Picture||
                Target.PreparedFrame is not {Ready:true,Canceled:false} Upload||Upload.Lease!=Frame||Upload.Epoch!=Target.FrameEpoch||Upload.Gpu==IntPtr.Zero)return 0;
            if(Upload.Device is not {Retired:false} Ticket||Native.LuiWinUI_FrameDeviceRemovedV1(Ticket.Creation.Result)<0){if(Upload.Device is {} Lost)LoseFrameDevice(Lost);return 0;}
            EnsureFrameSurface(Upload);
            if(FrameQualificationFault==6){FrameQualificationFault=0;throw new OutOfMemoryException();}
            Picture.Stretch=FrameStretch(Mode);var Previous=Target.DisplayedFrame;
            AttachFrame(Picture,Upload.Gpu);Upload.Attached=true;Target.PreparedFrame=null;Target.DisplayedFrame=Upload;++FramePublished;
            MarkPresentation();SampleFrameResources();if(Previous is not null){Previous.Attached=false;ReleaseFrame(Previous);}return 1;
        }catch{
            ++FrameUploadFailures;
            if(Views.TryGetValue(Id,out var FailedTarget)&&FailedTarget.PreparedFrame is {} Failed){
                try{if(FailedTarget.Content is Image Picture)AttachFrame(Picture,FailedTarget.DisplayedFrame?.Gpu??IntPtr.Zero);}
                catch{/* Preserve the charged native token if restoration also fails. */Failed.Attached=true;}
                ReleaseFrame(Failed);
            }
            return 0;
        }
    }
    private void OnClearFrame(IntPtr Context,int Id,ulong Epoch) {
        try {
            if(!Views.TryGetValue(Id,out var Target)||Target.ClassName!="FrameSurface")return;Target.FrameEpoch=Epoch;
            if(Target.PreparedFrame is {} Prepared){Target.PreparedFrame=null;ReleaseFrame(Prepared);}
            if(Target.DisplayedFrame is {} Displayed){Target.DisplayedFrame=null;ReleaseFrame(Displayed);}
        }catch{++FrameUploadFailures;}
    }
    private void ReleaseFrame(FrameUpload Upload) {
        if(ReferenceEquals(Upload.Target.PreparedFrame,Upload))Upload.Target.PreparedFrame=null;
        if(Upload.Released)return;Upload.Canceled=true;if(Upload.Gpu!=IntPtr.Zero)Native.LuiWinUI_FrameCancelV1(Upload.Gpu);
        if(Upload.Working||Upload.Retiring)return;RetireFrameGpu(Upload,false);
    }
    private void RetireFrameGpu(FrameUpload Upload,bool KeepPixels) {
        Upload.RetirementRequested=true;Upload.RetireKeepPixels=KeepPixels;
        if(Upload.Retiring||Upload.Working||Upload.Released)return;
        try {
            if(Upload.Attached){if(FrameQualificationFault==3){FrameQualificationFault=0;throw new InvalidOperationException();}
                if(Upload.Target.Content is Image Picture)AttachFrame(Picture,IntPtr.Zero);Upload.Attached=false;MarkPresentation();}
            if(Upload.Gpu!=IntPtr.Zero)Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameSurfaceCloseV1(Upload.Gpu));
            if(Upload.RetiredGpu==IntPtr.Zero){Upload.RetiredGpu=Upload.Gpu;Upload.Gpu=IntPtr.Zero;}
            Upload.Retiring=true;_=RetireFrameGpuAsync(Upload,KeepPixels);
        }catch(Exception Error){FrameLastHResult=Error.HResult;++FrameUploadFailures;}
    }
    private async Task RetireFrameGpuAsync(FrameUpload Upload,bool KeepPixels) {
        IntPtr Retired=Upload.RetiredGpu;
        try {
        if(FrameQualificationFault==5){FrameQualificationFault=0;throw new OutOfMemoryException();}
        if(Retired!=IntPtr.Zero)await Task.Run(async()=>{
            // No XAML, Runtime, Instance or Luau access on this retirement worker.
            while(Native.LuiWinUI_FramePollV1(Retired)==0)await Task.Delay(2);
            while(Native.LuiWinUI_FrameReleaseV1(Retired)!=1)await Task.Delay(8);
        });
        }catch{++FrameUploadFailures;Upload.Retiring=false;return;}
        Upload.RetiredGpu=IntPtr.Zero;
        Upload.RetirementRequested=false;
        Upload.Retiring=false;
        if(KeepPixels&&!Upload.Canceled&&!IsDisposed&&!IsDisposing&&Upload.Target.IsAlive&&ReferenceEquals(Upload.Target.DisplayedFrame,Upload))Upload.NeedsRecovery=true;
        else FinishFrameRelease(Upload);
    }
    private void FinishFrameRelease(FrameUpload Upload) {
        if(Upload.Device is {Retired:true,Retirement:null} Ticket){RetireFrameDevice(Ticket);if(Ticket.Retirement is null)return;}
        if(Upload.Released)return;Upload.Released=true;FrameResources.Remove(Upload);
        Native.Lui_FrameRelease(Upload.Lease);Upload.Lease=IntPtr.Zero;
        Upload.ReleaseFinished.TrySetResult();
    }
    private bool CanRecoverFrame(View Target) {
        if(!Target.Visible||Target.Element is not Microsoft.UI.Xaml.FrameworkElement Element||Element.XamlRoot is null||Element.ActualWidth<=0||Element.ActualHeight<=0)return false;
        for(var Current=Target;Current.ParentId!=0&&Views.TryGetValue(Current.ParentId,out var Parent);Current=Parent)if(!Parent.Visible)return false;
        return Target.InputVisible;
    }
    private void LoseFrameDevice(FrameDeviceTicket Ticket) {
        if(Ticket.Retired)return;++FrameDeviceLosses;RetireFrameDevice(Ticket);
        foreach(var Upload in FrameResources.ToArray()) {
            if(!ReferenceEquals(Upload.Device,Ticket))continue;
            if(Upload.Gpu!=IntPtr.Zero)Native.LuiWinUI_FrameCancelV1(Upload.Gpu);
            if(ReferenceEquals(Upload.Target.DisplayedFrame,Upload)) {
                if(!Upload.Working)RetireFrameGpu(Upload,true);
            }else ReleaseFrame(Upload);
        }
    }
    private void OnFrameSurfaceContentsLost(object? Sender,object Args) {
        // XAML raises a global notification, possibly for a retired source.
        // Defer it out of native property callbacks and do not invalidate a
        // healthy device or cancel newer uploads for a surface-only loss.
        if(IsDisposed||IsDisposing)return;FrameSurfacesInvalidated=true;
        foreach(var Upload in FrameResources)Upload.SurfaceInvalidated=true;
    }
    private void EnsureFrameSurface(FrameUpload Upload) {
        if(!Upload.SurfaceInvalidated||Upload.Gpu==IntPtr.Zero||Upload.Canceled)return;
        Upload.SurfaceInvalidated=false;
        try{Marshal.ThrowExceptionForHR(Native.LuiWinUI_FrameSurfaceRefreshV1(Upload.Gpu));
            if(Upload.SurfaceInvalidated)throw new InvalidOperationException("FrameSurfaceLostAgain");}
        catch{Upload.SurfaceInvalidated=true;throw;}
    }
    private void DrainFrameRetirements() {
        if(FrameDevice is {Retired:true} Retired)RetireFrameDevice(Retired);
        if(!IsDisposed&&!IsDisposing&&FrameDevice is {Retired:false} Ticket&&Ticket.Creation.IsCompletedSuccessfully&&Native.LuiWinUI_FrameDeviceRemovedV1(Ticket.Creation.Result)<0)LoseFrameDevice(Ticket);
        if(FrameSurfacesInvalidated&&!IsDisposed&&!IsDisposing){FrameSurfacesInvalidated=false;
            foreach(var Target in Views.Values)if(Target.PreparedFrame is null&&Target.DisplayedFrame is {Working:false,Retiring:false,Gpu:var Handle,SurfaceInvalidated:true} Displayed&&Handle!=IntPtr.Zero&&CanRecoverFrame(Target)){
                try{EnsureFrameSurface(Displayed);if(Target.Content is Image Picture)AttachFrame(Picture,Handle);MarkPresentation();}
                catch{FrameSurfacesInvalidated=true;}
            }
        }
        foreach(var Upload in FrameResources.ToArray()) {
            if(Upload.RetirementRequested&&!Upload.Working&&!Upload.Retiring)RetireFrameGpu(Upload,Upload.RetireKeepPixels&&!Upload.Canceled);
            else if(Upload.Canceled&&!Upload.Working&&!Upload.Retiring)ReleaseFrame(Upload);
            else if(!IsDisposed&&!IsDisposing&&Upload.SurfaceInvalidated&&!Upload.Working&&!Upload.Retiring&&Upload.Gpu!=IntPtr.Zero&&ReferenceEquals(Upload.Target.DisplayedFrame,Upload)&&Upload.Target.PreparedFrame is null&&CanRecoverFrame(Upload.Target)){
                try{EnsureFrameSurface(Upload);if(Upload.Target.Content is Image Picture)AttachFrame(Picture,Upload.Gpu);MarkPresentation();}
                catch{FrameSurfacesInvalidated=true;}
            }
            else if(!IsDisposed&&!IsDisposing&&Upload.NeedsRecovery&&!Upload.Working&&!Upload.Retiring&&Upload.Target.PreparedFrame is null&&CanRecoverFrame(Upload.Target)) {
                Upload.NeedsRecovery=false;_=PrepareFrameAsync(Upload,true,FrameUploadBarrier);
            }
        }
    }
    private void StopFrames() {
        CompositionTarget.SurfaceContentsLost-=OnFrameSurfaceContentsLost;
        foreach(var Entry in Views.ToArray())if(Entry.Value.ClassName=="FrameSurface")OnClearFrame(IntPtr.Zero,Entry.Key,Entry.Value.FrameEpoch);
        // Teardown has no future owner Pump. Retry one-shot detach/retirement
        // failures through the resource set, including views already unmapped.
        foreach(var Upload in FrameResources.ToArray())if(Upload.Canceled&&!Upload.Working&&!Upload.Retiring)ReleaseFrame(Upload);
        if(FrameDevice is {} Ticket)RetireFrameDevice(Ticket);
    }
    private void RecordFrameRendering() {
        if(IsDisposed||IsDisposing)return;
        foreach(var Target in Views.Values)if(Target.DisplayedFrame is {Submitted:false} Upload){Upload.Submitted=true;FrameSample(FrameSubmissionSamples,Stopwatch.GetElapsedTime(Upload.Started).TotalMilliseconds);}
    }
}
