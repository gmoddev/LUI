using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    [ThreadStatic] private static OwnerWakeGroup? ThreadWakeGroup;
    [ThreadStatic] private static OwnerDispatcher? ThreadOwnerDispatcher;
    private OwnerWakeGroup? OwnerWake;
    private bool Pumping;
    private Action? OwnerPumpObservation;
    private sealed class OwnerDispatcher
    {
        private readonly DispatcherQueue Queue;
        private readonly DispatcherQueueHandler Handler;
        internal OwnerWakeGroup? Active;
        internal int Queued,NeedsRetry;
        internal long Posts,HighWater;
        internal OwnerDispatcher(){Queue=DispatcherQueue.GetForCurrentThread()??throw new InvalidOperationException("OwnerDispatcherUnavailable");Handler=Dispatch;}
        internal uint Enqueue(OwnerWakeGroup Source)
        {
            if(!ReferenceEquals(Volatile.Read(ref Active),Source))return 0;
            if(Interlocked.CompareExchange(ref Queued,1,0)!=0)return 1;
            Interlocked.Exchange(ref HighWater,1);
            try{if(Queue.TryEnqueue(DispatcherQueuePriority.Normal,Handler)){Interlocked.Increment(ref Posts);return 1;}}catch{}
            Interlocked.Exchange(ref Queued,0);Interlocked.Exchange(ref NeedsRetry,1);return 0;
        }
        private void Dispatch(){Interlocked.Exchange(ref Queued,0);Volatile.Read(ref Active)?.TryOwners();}
    }
    private sealed class OwnerWakeGroup
    {
        private readonly OwnerDispatcher Dispatcher;
        private readonly List<WinUIBackend> Targets=new();
        private IntPtr Domain;
        private ulong Registration;
        private int Stopped;
        private bool Running,Deferred;
        internal bool SuppressForQualification;
        internal long Notifications,Turns,Rejected,Suppressed,BusyDeferrals;
        internal long DispatchPosts=>Dispatcher.Posts;
        internal long DispatchHighWater=>Dispatcher.HighWater;
        private static readonly Native.WakeRequest RequestCallback=Request;
        private static readonly Native.WakeRelease ReleaseCallback=Release;
        internal OwnerWakeGroup(WinUIBackend Backend,OwnerDispatcher Dispatcher)
        {
            this.Dispatcher=Dispatcher;Targets.Add(Backend);
            Domain=Native.Lui_GetOwnerDomainV1(Backend.Runtime);
            if(Domain==IntPtr.Zero)throw new InvalidOperationException("OwnerDomainUnavailable");
            GCHandle Context=default;bool Allocated=false,Transferred=false;
            try{
                Context=GCHandle.Alloc(this);Allocated=true;
                Volatile.Write(ref Dispatcher.Active,this);
                Native.NativeWake Callbacks=new(){StructSize=(uint)Marshal.SizeOf<Native.NativeWake>(),AbiVersion=1,
                    Context=GCHandle.ToIntPtr(Context),Request=RequestCallback,Release=ReleaseCallback};
                if(Native.Lui_SetNativeWakeV1(Domain,ref Callbacks,out Registration)!=0)throw new InvalidOperationException("OwnerWakeRegistrationFailed");
                Transferred=true;
            }finally{if(!Transferred){Volatile.Write(ref Dispatcher.Active,null);if(Allocated)Context.Free();Native.Lui_ReleaseNativeDomainV2(Domain);Domain=IntPtr.Zero;}}
        }
        private static uint Request(IntPtr Context)
        {
            try{return ((OwnerWakeGroup)GCHandle.FromIntPtr(Context).Target!).Enqueue();}catch{return 0;}
        }
        private static void Release(IntPtr Context)
        {
            // Native owns this root until the last in-flight Request returns.
            // This callback can run on a provider worker: no UI/runtime access.
            try{GCHandle.FromIntPtr(Context).Free();}catch{}
        }
        private uint Enqueue()
        {
            Interlocked.Increment(ref Notifications);
            if(Volatile.Read(ref Stopped)!=0)return 0;
            if(Volatile.Read(ref SuppressForQualification)){Interlocked.Increment(ref Suppressed);return 0;}
            uint Status=Dispatcher.Enqueue(this);if(Status==0)Interlocked.Increment(ref Rejected);return Status;
        }
        internal bool TryOwners()
        {
            if(Volatile.Read(ref Stopped)!=0)return false;
            if(Running||DeferIfBusy()){
                Deferred=true;++BusyDeferrals;return false;
            }
            Deferred=false;
            if(Native.Lui_AcknowledgeNativeWakeV1(Domain,Registration)!=0)return false;
            Interlocked.Exchange(ref Dispatcher.NeedsRetry,0);
            Running=true;++Turns;
            try{
                foreach(var Target in Targets.ToArray())if(!Target.IsDisposed&&!Target.IsDisposing)Target.Pump();
            }catch{LuiDiagnostics.Error("OwnerWake","[LUI:OwnerWake] OwnerTurnFailed");}
            finally{Running=false;}
            return true;
        }
        internal bool DeferIfBusy()
        {
            if(!Targets.Any(Value=>Value.Pumping||Native.Lui_IsOwnerPumpReadyV1(Value.Runtime)==0))return false;
            Deferred=true;return true;
        }
        internal bool Retry()
        {
            if(Running||Volatile.Read(ref Dispatcher.Queued)!=0)return false;
            if(Deferred||Volatile.Read(ref Dispatcher.NeedsRetry)!=0)return TryOwners();
            // A periodic owner turn also clears residual native pending state
            // after an overtaken enqueue failure. Records are still drained by
            // the normal pump; later completions may request a fresh wake.
            Native.Lui_AcknowledgeNativeWakeV1(Domain,Registration);return false;
        }
        internal void Add(WinUIBackend Backend)=>Targets.Add(Backend);
        internal void Remove(WinUIBackend Backend)
        {
            Targets.Remove(Backend);
            if(Targets.Count!=0)return;
            Volatile.Write(ref Stopped,1);
            Volatile.Write(ref Dispatcher.Active,null);
            // Queued closures hold only this stopped group. They never dereference
            // the released domain or a destroyed backend/runtime.
            Native.Lui_ClearNativeWakeV1(Domain,IntPtr.Zero,out _);
            Native.Lui_ReleaseNativeDomainV2(Domain);Domain=IntPtr.Zero;
            if(ReferenceEquals(ThreadWakeGroup,this))ThreadWakeGroup=null;
        }
    }
    private void StartOwnerWake()
    {
        try{
            var Dispatcher=ThreadOwnerDispatcher??=new OwnerDispatcher();
            OwnerWakeGroup Group;
            if(ThreadWakeGroup is {} Existing){Group=Existing;Group.Add(this);}
            else Group=new OwnerWakeGroup(this,Dispatcher);
            ThreadWakeGroup=Group;OwnerWake=Group;
        }catch(EntryPointNotFoundException){/* Older runtime: retain polling. */}
        catch{LuiDiagnostics.Error("OwnerWake","[LUI:OwnerWake] PollingFallback");}
    }
    private void StopOwnerWake(){var Group=OwnerWake;OwnerWake=null;Group?.Remove(this);}
    private static partial class Native
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]internal delegate uint WakeRequest(IntPtr Context);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]internal delegate void WakeRelease(IntPtr Context);
        [StructLayout(LayoutKind.Sequential)]internal struct NativeWake {
            internal uint StructSize,AbiVersion;internal IntPtr Context;internal WakeRequest Request;internal WakeRelease Release;
        }
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)]internal static extern IntPtr Lui_GetOwnerDomainV1(IntPtr Runtime);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)]internal static extern uint Lui_SetNativeWakeV1(IntPtr Domain,ref NativeWake Callbacks,out ulong Registration);
        [DllImport("LuiRuntime.dll",EntryPoint="Lui_SetNativeWakeV1",CallingConvention=CallingConvention.Cdecl)]internal static extern uint Lui_ClearNativeWakeV1(IntPtr Domain,IntPtr Callbacks,out ulong Registration);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)]internal static extern uint Lui_AcknowledgeNativeWakeV1(IntPtr Domain,ulong Registration);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)]internal static extern int Lui_IsOwnerPumpReadyV1(IntPtr Runtime);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)]internal static extern void Lui_ReleaseNativeDomainV2(IntPtr Domain);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)]internal static extern uint Lui_PumpNativeDomainV2(IntPtr Domain);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)]internal static extern uint Lui_LoadNativeServiceV2(IntPtr Runtime,[MarshalAs(UnmanagedType.LPUTF8Str)]string Path);
        [StructLayout(LayoutKind.Sequential)]internal struct OwnerNativeStats {
            internal uint StructSize,AbiVersion;internal ulong Bytes,ReservedBytes,Requests,Handles,Sessions,RetiringSessions,Leases,Events;
        }
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)]internal static extern uint Lui_GetNativeStatsV2(IntPtr Domain,ref OwnerNativeStats Stats);
    }
}
