using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.UI.Xaml;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    public async Task<bool> RunOwnerWakeQualificationAsync(string ProviderPath,string ReportPath)
    {
        var Passed=new List<string>();var Modes=new List<object>();
        var Owner=Environment.CurrentManagedThreadId;
        TaskCompletionSource? Delivered=null;long Admission=0;double CallbackMs=0;int WrongOwner=0;
        bool BusyDeferred=false;
        long PreviousNotifications=0,PreviousTurns=0,PreviousSuppressed=0,PreviousRejected=0,PreviousDeferrals=0;
        string Stage="Setup";int PumpTicks=0;
        Native.LogCallback Observer=(Context,Level,Message)=>{
            if(Level=="Print"&&Message=="[LUI:WakeEcho] Complete"){
                if(Environment.CurrentManagedThreadId!=Owner||Native.Lui_IsOwnerPumpReadyV1(Runtime)!=0)++WrongOwner;
                CallbackMs=Stopwatch.GetElapsedTime(Admission).TotalMilliseconds;Delivered?.TrySetResult();
            }else if(Level=="Print"&&Message=="[LUI:WakeEcho] Busy"){
                BusyDeferred=OwnerWake is not null&&!OwnerWake.TryOwners();
            }else OnNativeLog(Context,Level,Message);
        };
        var Timer=new DispatcherTimer();
        Timer.Tick+=(_,_)=>Pump();
        string DirectoryPath=Path.GetDirectoryName(Path.GetFullPath(ReportPath))!;Directory.CreateDirectory(DirectoryPath);
        try{
            FrameRequire(OwnerWake is not null&&Marshal.SizeOf<Native.NativeWake>()==32,"OwnerWakeInstalled");
            Native.CapabilityDeclaration Grants=new(){StructSize=(uint)Marshal.SizeOf<Native.CapabilityDeclaration>(),AbiVersion=1,GrantedCapabilities=3};
            FrameRequire(Native.Lui_DeclareCapabilities(Runtime,ref Grants)==1&&Native.Lui_LoadNativeServiceV2(Runtime,Path.GetFullPath(ProviderPath))==0,"RustEchoProvider");
            Native.Lui_SetLogCallback(Runtime,IntPtr.Zero,Observer);
            FrameRun("W=Instance.new('Window',{Title='LUI Engine-free owner wake',Visible=true,Size=UDim2.fromOffset(320,200)});Root=Instance.new('Frame',{Parent=W,Size=UDim2.fromScale(1,1)})");
            foreach(var Mode in new[]{(Name:"Polling16",Interval:16,Wake:false),(Name:"Polling64",Interval:64,Wake:false),(Name:"Wake64",Interval:64,Wake:true)}){
                Stage=Mode.Name;
                long InitialNotifications=OwnerWake!.Notifications,InitialTurns=OwnerWake.Turns,InitialSuppressed=OwnerWake.Suppressed;
                Volatile.Write(ref OwnerWake!.SuppressForQualification,!Mode.Wake);
                var Latencies=new List<double>();var Intervals=new List<double>();long Previous=0;
                OwnerPumpObservation=()=>{++PumpTicks;long Now=Stopwatch.GetTimestamp();if(Previous!=0)Intervals.Add(Stopwatch.GetElapsedTime(Previous,Now).TotalMilliseconds);Previous=Now;};
                Timer.Interval=TimeSpan.FromMilliseconds(Mode.Interval);Timer.Start();
                for(int Index=0;Index<128;++Index){
                    Delivered=new(TaskCreationOptions.RunContinuationsAsynchronously);Admission=Stopwatch.GetTimestamp();
                    FrameRun("app:GetService('RustProof'):Echo(buffer.create(64),nil,function(B,H,E)assert(not E and not H and buffer.len(B)==64);print('[LUI:WakeEcho] Complete')end)");
                    FrameRequire(!Delivered.Task.IsCompleted,"DeferredNativeCallback");
                    await Delivered.Task.WaitAsync(TimeSpan.FromSeconds(5));Latencies.Add(CallbackMs);
                }
                Timer.Stop();OwnerPumpObservation=null;
                if(Mode.Wake){var Values=Latencies.Order().ToArray();FrameRequire(Values[121]<=16&&Values[126]<=32,"WinUIWakeBudget");}
                Modes.Add(new{Mode.Name,RequestedFallbackMs=Mode.Interval,Wake=Mode.Wake,AdmissionToCallback=Aggregate(Latencies),OwnerPumpInterval=Aggregate(Intervals),Notifications=OwnerWake.Notifications-InitialNotifications,OwnerTurns=OwnerWake.Turns-InitialTurns,Suppressed=OwnerWake.Suppressed-InitialSuppressed});
            }
            FrameRequire(WrongOwner==0,"CallbackOwner");Passed.Add("Rust ABI v2 echo/owner-only/deferred delivery and configurable polling comparison");
            Stage="BusyOwner";
            Volatile.Write(ref OwnerWake!.SuppressForQualification,false);
            Timer.Interval=TimeSpan.FromMilliseconds(16);Timer.Start();
            Delivered=new(TaskCreationOptions.RunContinuationsAsynchronously);Admission=Stopwatch.GetTimestamp();
            FrameRun("app:GetService('RustProof'):Wake(buffer.create(0),nil,function()print('[LUI:WakeEcho] Complete')end);print('[LUI:WakeEcho] Busy')");
            await Delivered.Task.WaitAsync(TimeSpan.FromSeconds(5));FrameRequire(BusyDeferred&&OwnerWake.BusyDeferrals>0,"NestedOwnerDeferral");
            Passed.Add("Nested owner wake during VM dispatch defers without reentrant pumping");
            Stage="SiblingBackends";
            using(var Healthy=new WinUIBackend()){
                FrameRequire(Native.Lui_DeclareCapabilities(Healthy.Runtime,ref Grants)==1&&Native.Lui_LoadNativeServiceV2(Healthy.Runtime,Path.GetFullPath(ProviderPath))==0,"SiblingProvider");
                var SiblingDone=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                Native.LogCallback SiblingObserver=(_,Level,Message)=>{if(Level=="Print"&&Message=="[LUI:WakeEcho] Sibling")SiblingDone.TrySetResult();};
                Native.Lui_SetLogCallback(Healthy.Runtime,IntPtr.Zero,SiblingObserver);
                Healthy.FrameRun("app:GetService('RustProof'):Wake(buffer.create(0),nil,function()print('[LUI:WakeEcho] Sibling')end)");
                bool SiblingDeferred=false;
                Native.LogCallback BusyObserver=(_,Level,Message)=>{
                    if(Level=="Print"&&Message=="[LUI:WakeEcho] SiblingBusy") {Healthy.Pump();SiblingDeferred=!SiblingDone.Task.IsCompleted;}
                };
                Native.Lui_SetLogCallback(Runtime,IntPtr.Zero,BusyObserver);
                try{FrameRun("print('[LUI:WakeEcho] SiblingBusy')");FrameRequire(SiblingDeferred,"BusySiblingPumpedReentrantly");}
                finally{Native.Lui_SetLogCallback(Runtime,IntPtr.Zero,Observer);GC.KeepAlive(BusyObserver);}
                await SiblingDone.Task.WaitAsync(TimeSpan.FromSeconds(5));
                var Terminal=new WinUIBackend();Native.PropertyCallback FailProperty=(_,_,_,_)=>0;
                try{
                    Terminal.FrameRun("W=Instance.new('Window',{Visible=true,Size=UDim2.fromOffset(120,100)});Root=Instance.new('Frame',{Parent=W});S=Instance.new('FrameSurface',{Parent=Root,Size=UDim2.fromOffset(80,40)});assert(S:SubmitFrame(buffer.create(4),1,1,4,'BGRA8Premultiplied'))");
                    await Terminal.FlushFrameAsync();var Retiring=Terminal.SurfaceView().DisplayedFrame!;
                    Terminal.FrameQualificationFault=3;Terminal.FrameRun("S:ClearFrame()");
                    FrameRequire(Retiring.RetirementRequested&&Terminal.GetFrameStats().StagingBytes>0,"TerminalRetrySetup");
                    Native.Lui_SetBackend(Terminal.Runtime,new Native.BackendCallbacks{Create=Terminal.CreateCallback,Property=FailProperty,Parent=Terminal.ParentCallback,Arrange=Terminal.ArrangeCallback,Destroy=Terminal.DestroyCallback});
                    FrameRequire(Native.Lui_RunScript(Terminal.Runtime,"Root.Opacity=0.5","TerminalBackendProbe")==0&&Native.Lui_IsOwnerPumpReadyV1(Terminal.Runtime)==-1,"TerminalBackendFailure");
                    SiblingDone=new(TaskCreationOptions.RunContinuationsAsynchronously);
                    Healthy.FrameRun("app:GetService('RustProof'):Echo(buffer.create(0),nil,function()print('[LUI:WakeEcho] Sibling')end)");
                    await SiblingDone.Task.WaitAsync(TimeSpan.FromSeconds(5));Terminal.Pump();
                    await Retiring.ReleaseFinished.Task.WaitAsync(TimeSpan.FromSeconds(10));
                    FrameRequire(Terminal.FrameResources.Count==0&&Terminal.GetFrameStats().StagingBytes==0,"TerminalSkippedAdapterRetirement");
                }finally{Terminal.Dispose();GC.KeepAlive(FailProperty);}
                GC.KeepAlive(SiblingObserver);
            }
            Passed.Add("Simultaneous backends/busy sibling deferral/healthy sibling with terminal VM/charged adapter retirement");
            // Remove the last group target while its inline native completion
            // already has an owner dispatch queued, then replace that group.
            PreviousNotifications=OwnerWake.Notifications;PreviousTurns=OwnerWake.Turns;PreviousSuppressed=OwnerWake.Suppressed;PreviousRejected=OwnerWake.Rejected;PreviousDeferrals=OwnerWake.BusyDeferrals;
            StopOwnerWake();
            Stage="Retirement";
            try{
                long BeforePosts=ThreadOwnerDispatcher!.Posts;
                for(int Index=0;Index<16;++Index)using(var Retired=new WinUIBackend()){
                    FrameRequire(Native.Lui_DeclareCapabilities(Retired.Runtime,ref Grants)==1&&Native.Lui_LoadNativeServiceV2(Retired.Runtime,Path.GetFullPath(ProviderPath))==0,"RetiredProvider");
                    Retired.FrameRun("app:GetService('RustProof'):Wake(buffer.create(0),nil,function()error('retired wake callback')end)");
                }
                FrameRequire(ThreadOwnerDispatcher.Posts<=BeforePosts+1,"ReloadDispatchQueueBound");
                using(var Replacement=new WinUIBackend()){
                    FrameRequire(Native.Lui_DeclareCapabilities(Replacement.Runtime,ref Grants)==1&&Native.Lui_LoadNativeServiceV2(Replacement.Runtime,Path.GetFullPath(ProviderPath))==0,"ReplacementProvider");
                    var Ready=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                    Native.LogCallback ReplacementObserver=(_,Level,Message)=>{if(Level=="Print"&&Message=="[LUI:WakeEcho] Replacement")Ready.TrySetResult();};
                    Native.Lui_SetLogCallback(Replacement.Runtime,IntPtr.Zero,ReplacementObserver);
                    Replacement.FrameRun("ReplacementDone=false;app:GetService('RustProof'):Echo(buffer.create(0),nil,function()ReplacementDone=true;print('[LUI:WakeEcho] Replacement')end)");
                    await Ready.Task.WaitAsync(TimeSpan.FromSeconds(5));Replacement.FrameRun("assert(ReplacementDone)");GC.KeepAlive(ReplacementObserver);
                }
                await Task.Yield();
            }finally{StartOwnerWake();}
            Passed.Add("Queued dispatch/last-backend close/replacement group suppresses retired VM");
            Stage="FrameSurface";
            Delivered=new(TaskCreationOptions.RunContinuationsAsynchronously);Admission=Stopwatch.GetTimestamp();
            FrameRun("S=Instance.new('FrameSurface',{Parent=Root,Position=UDim2.fromOffset(20,20),Size=UDim2.fromOffset(200,100),ScaleMode='Stretch'});app:GetService('RustProof'):Frame(buffer.create(0),nil,function(B,H,E)assert(not E and not H);assert(S:SubmitFrame(B,2,2,8,'BGRA8Premultiplied'));buffer.fill(B,0,0);print('[LUI:WakeEcho] Complete')end)");
            await Delivered.Task.WaitAsync(TimeSpan.FromSeconds(5));await FlushFrameAsync();
            var Root=(FrameworkElement)Views.Values.Single(Value=>Value.Window is not null).Window!.Content;
            var Pixels=await CaptureFrameAsync(Path.Combine(DirectoryPath,"Rust-owned-frame.png"),Root);CheckPixel(Pixels,Root,50,40,33,22,11);
            FrameRun("assert(S.FrameState=='Ready');S:ClearFrame();S:Destroy()");await WaitRetiredFramesAsync();
            FrameRequire(GetFrameStats().PixelBytes==0&&GetFrameStats().StagingBytes==0,"WakeFrameQuiescence");
            Passed.Add("Rust owned frame reaches existing FrameSurface/immutable snapshot/native pixels and zero frame charges");
            var Group=OwnerWake!;
            long Notifications=PreviousNotifications+Group.Notifications,OwnerTurns=PreviousTurns+Group.Turns,Rejected=PreviousRejected+Group.Rejected,
                Suppressed=PreviousSuppressed+Group.Suppressed,BusyDeferrals=PreviousDeferrals+Group.BusyDeferrals;
            var Domain=Native.Lui_GetOwnerDomainV1(Runtime);var Device=FrameDevice;Timer.Stop();
            Native.OwnerNativeStats Remaining=new(){StructSize=(uint)Marshal.SizeOf<Native.OwnerNativeStats>(),AbiVersion=2};
            try{
                Stage="Shutdown";Dispose();
                if(Device?.Retirement is {} Retirement)await Retirement.WaitAsync(TimeSpan.FromSeconds(10));
                var Clock=Stopwatch.StartNew();
                do{
                    FrameRequire(Clock.Elapsed.TotalSeconds<10,"WakeNativeRetirementTimeout");
                    Native.Lui_PumpNativeDomainV2(Domain);FrameRequire(Native.Lui_GetNativeStatsV2(Domain,ref Remaining)==0,"WakeNativeStats");
                    if(Remaining.Sessions!=0)await Task.Delay(1);
                }while(Remaining.Sessions!=0);
                FrameRequire(Remaining.Bytes==0&&Remaining.ReservedBytes==0&&Remaining.Requests==0&&Remaining.Handles==0&&Remaining.Leases==0&&Remaining.Events==0&&GetFrameGpuStats().Devices==0,"WakeShutdownQuiescence");
            }finally{Native.Lui_ReleaseNativeDomainV2(Domain);}
            Passed.Add("Actual backend shutdown/native provider retirement/zero session leases, records, bytes and graphics devices");
            File.WriteAllText(ReportPath,JsonSerializer.Serialize(new{Success=true,Scope="WindowsWinUIOwnerWake",Platform=RuntimeInformation.OSDescription,Modes,Passed,Notifications,OwnerTurns,Rejected,Suppressed,BusyDeferrals,WrongOwner,QueueBound=1,Group.DispatchHighWater,Group.DispatchPosts,RemainingSessions=Remaining.Sessions,RemainingLeases=Remaining.Leases,RemainingBytes=Remaining.Bytes,RemainingRequests=Remaining.Requests}));
            return true;
        }catch(Exception Error){
            File.WriteAllText(ReportPath,JsonSerializer.Serialize(new{Success=false,Code=Error is FrameQualificationFailure Failure?Failure.Check:"OwnerWakeQualificationFailed",Error.HResult,ExceptionType=Error.GetType().Name,Passed,Modes,WrongOwner,Stage,PumpTicks,Step=FrameQualificationStep,Notifications=OwnerWake?.Notifications,Suppressed=OwnerWake?.Suppressed}));return false;
        }finally{
            Timer.Stop();OwnerPumpObservation=null;
            if(OwnerWake is not null)Volatile.Write(ref OwnerWake.SuppressForQualification,false);
            if(!IsDisposed)Native.Lui_SetLogCallback(Runtime,IntPtr.Zero,LogCallback);GC.KeepAlive(Observer);
        }
    }
}
