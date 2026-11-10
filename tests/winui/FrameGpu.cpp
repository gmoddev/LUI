#include "../../backends/winui3/FrameGpu.h"
#include <cstdio>
#include <future>
#include <thread>
#include <chrono>

using namespace LuiWinUI;
extern "C" {
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameDeviceCreateV1(uint32_t,DeviceHandle**);
__declspec(dllimport) void __cdecl LuiWinUI_FrameDeviceReleaseV1(DeviceHandle*);
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameDeviceBindOwnerV1(DeviceHandle*);
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameDeviceRemoveForTestV1(DeviceHandle*);
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameBeginV1(DeviceHandle*,LuiFrame*,Frame**);
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameCopyGateForTestV1(Frame*,HANDLE,HANDLE);
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameCopyV1(Frame*);
__declspec(dllimport) void __cdecl LuiWinUI_FrameCancelV1(Frame*);
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameSubmitCopyV1(Frame*);
__declspec(dllimport) int __cdecl LuiWinUI_FramePollV1(Frame*);
__declspec(dllimport) int __cdecl LuiWinUI_FrameReleaseV1(Frame*);
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameGpuStatsV1(Stats*);
__declspec(dllimport) void __cdecl LuiWinUI_FrameAllocationFaultForTestV1(int);
#ifdef LUI_WINUI_PRESENTER
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameSurfaceCreateV1(Frame*);
__declspec(dllimport) HRESULT __cdecl LuiWinUI_FrameSurfaceCloseV1(Frame*);
#endif
}
namespace {
int Failures=0;LuiFrame* Captured=nullptr;
bool Check(bool Result,const char* Label){if(!Result){++Failures;std::fprintf(stderr,"[LUI:FrameGpuTest] %s\n",Label);}return Result;}
int LUI_CALL Submit(void*,int,LuiFrame* Value,uint32_t){Lui_FrameRetain(Value);Captured=Value;return Lui_FrameComplete(Value,1);}
int LUI_CALL Publish(void*,int,LuiFrame*,uint32_t){return 1;}
void LUI_CALL Clear(void*,int,uint64_t){}
Stats GpuStats(){Stats Value{sizeof(Value),1};Check(SUCCEEDED(LuiWinUI_FrameGpuStatsV1(&Value)),"private statistics");return Value;}
LuiFrameStatsV1 CoreStats(LuiRuntime* Runtime){LuiFrameStatsV1 Value{sizeof(Value),1};Check(Lui_GetFrameStats(Runtime,&Value)==1,"core statistics");return Value;}
LuiRuntime* MakeRuntime(uint32_t Width=4,uint32_t Height=2){
    auto* Runtime=Lui_Create();LuiFrameCallbacksV1 Callbacks{sizeof(Callbacks),1,nullptr,Submit,Publish,Clear,0};
    Check(Lui_SetFrameCallbacks(Runtime,&Callbacks)==1,"core callbacks");
    char Source[768];std::snprintf(Source,sizeof(Source),
        "W=Instance.new('Window',{Visible=true});S=Instance.new('FrameSurface',{Parent=W});B=buffer.create(%u);buffer.fill(B,0,7);assert(S:SubmitFrame(B,%u,%u,%u,'BGRA8Premultiplied'));buffer.fill(B,0,99)",
        Width*Height*4,Width,Height,Width*4);
    Check(Lui_RunScript(Runtime,Source,"GpuOwnership")==1,"public snapshot admission");Lui_Pump(Runtime);
    Check(Captured!=nullptr,"captured lease");Check(Lui_FrameReserveStaging(Captured,MaxStaging)==1,"reservation before allocation");return Runtime;
}
Frame* Begin(DeviceHandle* DeviceValue){
    Frame* Value=nullptr;HRESULT Status=LuiWinUI_FrameBeginV1(DeviceValue,Captured,&Value);
    Lui_FrameRelease(Captured);Captured=nullptr;
    if(!Check(SUCCEEDED(Status)&&Value,"native upload allocation"))return nullptr;
    Check(Value->Capacity<=MaxStaging,"queried allocation within cap");return Value;
}
int Release(Frame* Value){return std::async(std::launch::async,[Value]{return LuiWinUI_FrameReleaseV1(Value);}).get();}
void ReleaseDevice(DeviceHandle* Value){std::thread Worker([Value]{LuiWinUI_FrameDeviceReleaseV1(Value);});Worker.join();}
void WaitCopy(Frame* Value){Check(SUCCEEDED(std::async(std::launch::async,[Value]{return LuiWinUI_FrameCopyV1(Value);}).get()),"worker mapped-memory copy");}
void ActiveCopy(DeviceHandle* DeviceValue,int Action){
    auto* Runtime=MakeRuntime();Frame* Value=Begin(DeviceValue);if(!Value){Lui_Destroy(Runtime);return;}
#ifdef LUI_WINUI_PRESENTER
    Check(FAILED(LuiWinUI_FrameSurfaceCreateV1(Value)),"reject XAML draw before copy/submission");
#endif
    HANDLE Entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),Resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(SUCCEEDED(LuiWinUI_FrameCopyGateForTestV1(Value,Entered,Resume)),"duplicate copy barrier handles");
    auto Worker=std::async(std::launch::async,[Value]{return LuiWinUI_FrameCopyV1(Value);});
    Check(WaitForSingleObject(Entered,5000)==WAIT_OBJECT_0,"actual half-copy entered");
    Check(Value->CopyState.load()==1&&GpuStats().UploadBytes==Value->Capacity,"mapped allocation stays charged during copy");
    if(Action==0)Check(Lui_RunScript(Runtime,"S:ClearFrame()","ClearDuringCopy")==1,"clear during copy");
    if(Action==1)Check(Lui_RunScript(Runtime,"S:Destroy();S:Destroy()","DestroyDuringCopy")==1,"destroy during copy");
    if(Action==2){Lui_Destroy(Runtime);Runtime=Lui_Create();Check(Lui_RunScript(Runtime,"S=Instance.new('FrameSurface')","Replacement")==1,"reload with retained upload");}
    LuiWinUI_FrameCancelV1(Value);
    Check(LuiWinUI_FrameReleaseV1(Value)==0&&CoreStats(Runtime).StagingBytes==MaxStaging,"cannot release active copy or its accounting");
    SetEvent(Resume);Check(FAILED(Worker.get()),"canceled copy returns without GPU submission");
    Check(Release(Value)==1&&GpuStats().UploadBytes==0,"off-owner release after worker settlement");
    Check(CoreStats(Runtime).PixelBytes==0&&CoreStats(Runtime).StagingBytes==0,"retired original snapshot fully released");
    CloseHandle(Entered);CloseHandle(Resume);Lui_Destroy(Runtime);
}
LuiRuntime* FenceRetention(DeviceHandle* DeviceValue,bool Remove,bool Wrap=false){
    auto* Runtime=MakeRuntime(1920,1080);Frame* Value=Begin(DeviceValue);if(!Value){Lui_Destroy(Runtime);return nullptr;}
    Check(Value->Capacity==8323072&&Value->Footprint.Footprint.RowPitch==7680,"1080p exact queried heap allocation and pitch");
    WaitCopy(Value);
    Check(Value->Pixels[Value->Footprint.Offset]==7,"caller mutation cannot change owned source");
    ComPtr<ID3D12Fence> Barrier;Check(SUCCEEDED((*DeviceValue)->Native->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&Barrier))),"private queue barrier");
    Check(SUCCEEDED((*DeviceValue)->Queue->Wait(Barrier.Get(),1)),"queue waits without physical GPU reset");
    Check(SUCCEEDED(LuiWinUI_FrameSubmitCopyV1(Value)),"submit explicit copy and completion fence");
    HANDLE RemovedEvent=nullptr;std::future<HRESULT> DrainSignal;
    if(Wrap&&Remove){
        RemovedEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Check(RemovedEvent!=nullptr,"create device-removal notification event");
        Check(RemovedEvent&&SUCCEEDED((*DeviceValue)->Fence->SetEventOnCompletion(UINT64_MAX,RemovedEvent)),
            "register real removal notification before forced removal");
        // The monitored fence signals MAX only after removal. A separate worker
        // then makes the shared 11on12 queue drainable even if RemoveDevice blocks.
        DrainSignal=std::async(std::launch::async,[Barrier,RemovedEvent]{
            const DWORD Wait=WaitForSingleObject(RemovedEvent,5000);
            const HRESULT Signal=Barrier->Signal(1);
            return Wait==WAIT_OBJECT_0?Signal:E_FAIL;
        });
    }
    if(Wrap){
        D3D11_RESOURCE_FLAGS Flags{};Flags.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Check(SUCCEEDED((*DeviceValue)->Interop->CreateWrappedResource(Value->Texture.Get(),&Flags,
            D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON,IID_PPV_ARGS(&Value->Wrapped)))&&
            Value->Wrapped&&!Value->Source,"real 11on12 wrapper retained without XAML source");
    }
    Check(Barrier->GetCompletedValue()==0&&LuiWinUI_FramePollV1(Value)==0&&
        LuiWinUI_FrameReleaseV1(Value)==0,"blocked in-flight GPU read retains upload before removal");
    Lui_Destroy(Runtime);Runtime=Lui_Create();Check(Lui_RunScript(Runtime,"S=Instance.new('FrameSurface')","RetiredOwner")==1,"replacement owner during GPU copy");
    Check(CoreStats(Runtime).StagingBytes==MaxStaging&&GpuStats().UploadBytes==8323072,"accounting survives retired runtime");
    if(Remove){
        Check(FAILED(LuiWinUI_FrameDeviceRemoveForTestV1(DeviceValue)),"real task-private D3D12 removal");
        Check(LuiWinUI_FramePollV1(Value)==-1,"removed fence is failure, not ready");
        Check(GpuStats().UploadBytes==8323072&&CoreStats(Runtime).StagingBytes==MaxStaging,
            "removed fence retains GPU allocation and P1 reservation");
    }else{
        Check(SUCCEEDED(Barrier->Signal(1)),"release GPU barrier");
        auto End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(LuiWinUI_FramePollV1(Value)==0&&std::chrono::steady_clock::now()<End)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Check(LuiWinUI_FramePollV1(Value)==1,"GPU fence completion observed");
    }
    if(!Remove)Check(GpuStats().UploadBytes==0,"CPU allocation released at confirmed completion fence");
    Check(Release(Value)==1,"native resource retirement off owner");
    if(Remove)Check(GpuStats().UploadBytes==8323072&&GpuStats().Frames==1&&
        CoreStats(Runtime).StagingBytes==MaxStaging,"removed frame remains charged after public token retirement");
    else Check(CoreStats(Runtime).PixelBytes==0&&CoreStats(Runtime).StagingBytes==0,
        "completed frame release clears owner ledger");
    if(Wrap&&Remove){
        auto* Retained=(*DeviceValue)->RemovedHead.load(std::memory_order_acquire);
        Check(Retained&&Retained->Wrapped&&Retained->Texture&&Retained->Upload&&Retained->Lease,
            "real 11on12 wrapper and GPU copy resources reached old-device quarantine");
        Check(SUCCEEDED(DrainSignal.get()),"11on12 queue barrier receives an eventual signal after removal");
        if(RemovedEvent)CloseHandle(RemovedEvent);
    }
    return Runtime;
}
void Faults(DeviceHandle* DeviceValue){
    for(int Fault=1;Fault<=2;++Fault){
        auto* Runtime=MakeRuntime();LuiWinUI_FrameAllocationFaultForTestV1(Fault);Frame* Value=nullptr;
        Check(LuiWinUI_FrameBeginV1(DeviceValue,Captured,&Value)==E_OUTOFMEMORY&&!Value,"allocation failure rolls back native ownership");
        Check(GpuStats().Uploads==0&&GpuStats().Frames==0,"no partial native allocation retained");
        Lui_FrameRelease(Captured);Captured=nullptr;Lui_Destroy(Runtime);
    }
}
#ifdef LUI_WINUI_PRESENTER
void FailedLaterFence(){
    DeviceHandle* DeviceValue=nullptr;
    if(!Check(SUCCEEDED(LuiWinUI_FrameDeviceCreateV1(1,&DeviceValue)),"failed-fence device"))return;
    Check(SUCCEEDED(LuiWinUI_FrameDeviceBindOwnerV1(DeviceValue)),"failed-fence owner");
    auto* Runtime=MakeRuntime();Frame* Value=Begin(DeviceValue);
    if(Value){
        WaitCopy(Value);Check(SUCCEEDED(LuiWinUI_FrameSubmitCopyV1(Value))&&Value->FenceValue!=0,"initial fence proof");
        (*DeviceValue)->NextFence=UINT64_MAX-1;
        Check(FAILED(LuiWinUI_FrameSurfaceCloseV1(Value))&&Value->FenceValue==0,"failed later fence discards old proof");
        Check(LuiWinUI_FramePollV1(Value)==-2&&LuiWinUI_FrameReleaseV1(Value)==0&&GpuStats().UploadBytes>0,"unfenced work remains quarantined and charged");
        Check(FAILED(LuiWinUI_FrameDeviceRemoveForTestV1(DeviceValue))&&LuiWinUI_FramePollV1(Value)==-1,
            "permanent removal reports failure without proving copy completion");
        Check(Release(Value)==1,"quarantined native lease retires after removal");
        Check(GpuStats().UploadBytes>0&&CoreStats(Runtime).StagingBytes==MaxStaging,
            "failed later fence remains charged through removed-device retirement");
    }
    Lui_Destroy(Runtime);ReleaseDevice(DeviceValue);
    Check(GpuStats().Devices==0&&GpuStats().Frames==0&&GpuStats().UploadBytes==0,"failed-fence final quiescence");
}
#endif
}
int main(){
    static_assert(sizeof(Stats)==80);
    Stats Wrong{};Check(FAILED(LuiWinUI_FrameGpuStatsV1(&Wrong)),"reject wrong private ABI size/version");
    DeviceHandle* DeviceValue=nullptr;
    if(!Check(SUCCEEDED(LuiWinUI_FrameDeviceCreateV1(1,&DeviceValue)),"WARP D3D12/11on12 device"))return 1;
    Check(SUCCEEDED(LuiWinUI_FrameDeviceBindOwnerV1(DeviceValue)),"bind authoritative owner once");
    Check(std::async(std::launch::async,[DeviceValue]{return LuiWinUI_FrameDeviceBindOwnerV1(DeviceValue);}).get()==RPC_E_WRONG_THREAD,"reject owner rebinding from worker");
    for(int Action=0;Action<3;++Action)ActiveCopy(DeviceValue,Action);
    Faults(DeviceValue);
    if(auto* Runtime=FenceRetention(DeviceValue,false))Lui_Destroy(Runtime);
    auto* RemovedRuntime=FenceRetention(DeviceValue,true);
    ReleaseDevice(DeviceValue);
    if(RemovedRuntime){
        Check(CoreStats(RemovedRuntime).PixelBytes==0&&CoreStats(RemovedRuntime).StagingBytes==0,
            "old-device teardown releases retained P1 reservation");
        Lui_Destroy(RemovedRuntime);
    }
    Check(GpuStats().Devices==0&&GpuStats().Frames==0&&GpuStats().Sources==0&&GpuStats().UploadBytes==0,"all native ownership retired");
    DeviceValue=nullptr;Check(SUCCEEDED(LuiWinUI_FrameDeviceCreateV1(1,&DeviceValue)),"device recreation after real removal");
    if(DeviceValue){
        Check(SUCCEEDED(LuiWinUI_FrameDeviceBindOwnerV1(DeviceValue)),"bind recreated device owner");
        auto* WrappedRuntime=FenceRetention(DeviceValue,true,true);
        ReleaseDevice(DeviceValue);
        if(WrappedRuntime){
            Check(CoreStats(WrappedRuntime).PixelBytes==0&&CoreStats(WrappedRuntime).StagingBytes==0,
                "wrapped removed-device teardown releases retained P1 reservation");
            Lui_Destroy(WrappedRuntime);
        }
        Check(GpuStats().Devices==0&&GpuStats().Frames==0&&GpuStats().UploadBytes==0,
            "wrapped removal retires every native allocation");
    }
#ifdef LUI_WINUI_PRESENTER
    FailedLaterFence();
#endif
    if(!Failures)std::puts("[LUI:FrameGpuTest] active-copy clear/destroy/reload, exact heap cap, fence retention, allocation faults and real device removal/recreation passed");
    return Failures?1:0;
}
