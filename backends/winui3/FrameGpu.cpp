#include "FrameGpu.h"
#include <cstring>
#include <new>
#include <limits>

namespace LuiWinUI {
std::atomic<uint64_t> Uploads{}, UploadBytes{}, PeakUploadBytes{}, Devices{}, Sources{}, Frames{}, Copies{}, Draws{}, Removed{};
namespace {
std::atomic<int> AllocationFault{};
HRESULT CreateDevice(uint32_t Mode, std::shared_ptr<Device>& Result) {
    if (Mode > 1) return E_INVALIDARG;
    auto Value = std::make_shared<Device>();
    ComPtr<IDXGIFactory4> Factory;
    HRESULT Status = CreateDXGIFactory2(0,IID_PPV_ARGS(&Factory)); if (FAILED(Status)) return Status;
    ComPtr<IDXGIAdapter> Adapter;
    if (Mode == 1) {Status=Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter));if(FAILED(Status))return Status;}
    Status=D3D12CreateDevice(Adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&Value->Native));
    if ((Status==DXGI_ERROR_UNSUPPORTED||Status==E_NOINTERFACE) && Mode == 0) {
        Status=Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter));if(FAILED(Status))return Status;
        Status=D3D12CreateDevice(Adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&Value->Native));
    }
    if(FAILED(Status))return Status;
    D3D12_COMMAND_QUEUE_DESC Description{}; Description.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    Status=Value->Native->CreateCommandQueue(&Description,IID_PPV_ARGS(&Value->Queue));if(FAILED(Status))return Status;
    Status=Value->Native->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&Value->Fence));if(FAILED(Status))return Status;
    IUnknown* Queues[]={Value->Queue.Get()};
    Status=D3D11On12CreateDevice(Value->Native.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,
        Queues,1,0,&Value->Bridge,&Value->Context,nullptr);if(FAILED(Status))return Status;
    Status=Value->Bridge.As(&Value->Interop);if(FAILED(Status))return Status;
    ++Devices;Value->Counted=true;Result=std::move(Value);return S_OK;
}
D3D12_RESOURCE_DESC TextureDescription(const LuiFrameViewV1& View) {
    D3D12_RESOURCE_DESC Value{};Value.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Value.Width=View.Width;Value.Height=View.Height;Value.DepthOrArraySize=1;Value.MipLevels=1;
    Value.Format=DXGI_FORMAT_B8G8R8A8_UNORM;Value.SampleDesc.Count=1;return Value;
}
HRESULT CreateFrame(DeviceHandle* DeviceValue,LuiFrame* Lease,Frame** Result) {
    if(!DeviceValue || !*DeviceValue || !Lease || !Result)return E_INVALIDARG;*Result=nullptr;
    auto Value=std::make_unique<Frame>();Value->Owner=*DeviceValue;
    if(Value->OwnerCall()!=S_OK)return RPC_E_WRONG_THREAD;
    if(!Lui_FrameView(Lease,&Value->View)||Value->View.Format!=1||!Value->View.Pixels||
        !Value->View.Width||Value->View.Width>1920||!Value->View.Height||Value->View.Height>1080||
        Value->View.RowStride!=Value->View.Width*4||Value->View.ByteCount!=uint64_t(Value->View.RowStride)*Value->View.Height||
        Value->View.ByteCount>MaxStaging)return E_INVALIDARG;
    if(!Lui_FrameRetain(Lease))return E_INVALIDARG;
    Value->Lease=Lease;
    // The owner adapter reserves MaxStaging exactly once before this call.
    // Recovery reuses that same live reservation; the P1 API rejects a second
    // reservation. This trusted private helper verifies capacity before alloc.
    auto Description=TextureDescription(Value->View);
    uint64_t Total=0,RowBytes=0;UINT Rows=0;
    Value->Owner->Native->GetCopyableFootprints(&Description,0,1,0,&Value->Footprint,&Rows,&RowBytes,&Total);
    if(Rows!=Value->View.Height||RowBytes!=Value->View.RowStride||!Total||Total>MaxStaging||
        Value->Footprint.Footprint.RowPitch<Value->View.RowStride)return E_INVALIDARG;
    D3D12_RESOURCE_DESC Buffer{};Buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;Buffer.Width=Total;
    Buffer.Height=1;Buffer.DepthOrArraySize=1;Buffer.MipLevels=1;Buffer.SampleDesc.Count=1;Buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto Allocation=Value->Owner->Native->GetResourceAllocationInfo(0,1,&Buffer);
    if(!Allocation.SizeInBytes||Allocation.SizeInBytes==UINT64_MAX||Allocation.SizeInBytes>MaxStaging)return E_OUTOFMEMORY;
    D3D12_HEAP_PROPERTIES Heap{};Heap.Type=D3D12_HEAP_TYPE_UPLOAD;Heap.CreationNodeMask=1;Heap.VisibleNodeMask=1;
    if(AllocationFault.load()==1){AllocationFault=0;return E_OUTOFMEMORY;}
    HRESULT Status=Value->Owner->Native->CreateCommittedResource(&Heap,D3D12_HEAP_FLAG_NONE,&Buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&Value->Upload));if(FAILED(Status))return Status;
    Value->Capacity=Allocation.SizeInBytes;++Uploads;
    const uint64_t Current=UploadBytes.fetch_add(Value->Capacity)+Value->Capacity;
    uint64_t Peak=PeakUploadBytes.load();while(Current>Peak&&!PeakUploadBytes.compare_exchange_weak(Peak,Current)){}
    D3D12_RANGE ReadRange{};
    Status=Value->Upload->Map(0,&ReadRange,reinterpret_cast<void**>(&Value->Pixels));if(FAILED(Status))return Status;
    if(AllocationFault.load()==2){AllocationFault=0;return E_OUTOFMEMORY;}
    Heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Status=Value->Owner->Native->CreateCommittedResource(&Heap,D3D12_HEAP_FLAG_NONE,&Description,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&Value->Texture));if(FAILED(Status))return Status;
    Status=Value->Owner->Native->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&Value->Allocator));if(FAILED(Status))return Status;
    Status=Value->Owner->Native->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,Value->Allocator.Get(),nullptr,
        IID_PPV_ARGS(&Value->Commands));if(FAILED(Status))return Status;
    Status=Value->Commands->Close();if(FAILED(Status))return Status;
    Value->RemovedReserve=std::make_unique<RemovedFrameResources>();
    ++Frames;Value->Counted=true;*Result=Value.release();return S_OK;
}
}
RemovedFrameResources::~RemovedFrameResources() {
    TestGate.Reset();Wrapped.Reset();Commands.Reset();Allocator.Reset();Texture.Reset();
    if(Upload){Upload.Reset();--Uploads;UploadBytes.fetch_sub(Capacity);}
    if(Counted)--Frames;
    Lui_FrameRelease(Lease);
}
void Device::QuarantineRemoved(RemovedFrameResources* Value) noexcept {
    auto* Head=RemovedHead.load(std::memory_order_relaxed);
    do {Value->Next=Head;}
    while(!RemovedHead.compare_exchange_weak(Head,Value,std::memory_order_release,std::memory_order_relaxed));
    OutputDebugStringA("[LUI:FrameGpu] Removed frame resources retained for old-device teardown.\n");
}
Device::~Device() {
    // Only the managed retirement worker releases the last bridge reference.
    auto* Removed=RemovedHead.exchange(nullptr,std::memory_order_acquire);
    const bool HadRemoved=Removed!=nullptr;
    if(Removed) {
        OutputDebugStringA("[LUI:FrameGpu] Old-device teardown releasing wrapped references.\n");
        // 11on12 deferred destruction must process while its immediate context
        // is live; keep our D3D12 texture reference through this Flush.
        for(auto* Value=Removed;Value;Value=Value->Next)Value->Wrapped.Reset();
        if(Context)Context->Flush();
        OutputDebugStringA("[LUI:FrameGpu] Last wrapped release and 11on12 Flush returned.\n");
    }
    Interop.Reset();Context.Reset();Bridge.Reset();Fence.Reset();Queue.Reset();Native.Reset();
    if(Removed)OutputDebugStringA("[LUI:FrameGpu] Old-device queue release returned; releasing retained resources (experimental).\n");
    while(Removed){auto* Next=Removed->Next;delete Removed;Removed=Next;}
    if(HadRemoved)OutputDebugStringA("[LUI:FrameGpu] Removed GPU resources and P1 leases released.\n");
    if(Counted) --Devices;
}
HRESULT Device::Signal(uint64_t& Value) {
    // A failed later signal must never leave an older completed fence usable
    // as proof that newly queued work retired. Zero quarantines that resource
    // until a subsequent successful signal or permanent device removal.
    Value=0;
    if(NextFence==UINT64_MAX-1)return E_FAIL;
    const uint64_t Candidate=++NextFence;
    const HRESULT Status=Queue->Signal(Fence.Get(),Candidate);
    if(SUCCEEDED(Status))Value=Candidate;
    return Status;
}
HRESULT Frame::OwnerCall() const {return Owner&&Owner->OwnerThread.load(std::memory_order_acquire)==GetCurrentThreadId()?S_OK:RPC_E_WRONG_THREAD;}
void Frame::ReleaseUpload() {
    if(Upload) {
        if(Pixels){Upload->Unmap(0,nullptr);Pixels=nullptr;}
        Upload.Reset();--Uploads;UploadBytes.fetch_sub(Capacity);Capacity=0;
    }
}
int Frame::Status() {
    if(!Submitted)return CopyState.load()==1?0:1;
    const uint64_t Completed=Owner->Fence->GetCompletedValue();
    if(Completed==UINT64_MAX||FAILED(Owner->Native->GetDeviceRemovedReason()))return -1;
    if(!FenceValue)return -2;
    return Completed>=FenceValue?1:0;
}
Frame::~Frame() {
    // Source must already have been released on the XAML owner. Submitted work
    // with a removed device transfers all GPU references and the P1 reservation
    // to that device's retirement worker instead of treating removal as a fence.
    if(RemovedAtRetirement) {
        auto* Retained=RemovedReserve.release();
        Retained->Upload=std::move(Upload);Retained->Texture=std::move(Texture);
        Retained->Allocator=std::move(Allocator);Retained->Commands=std::move(Commands);
        Retained->Wrapped=std::move(Wrapped);Retained->TestGate=std::move(TestGate);
        Retained->Lease=Lease;Lease=nullptr;Retained->Capacity=Capacity;Capacity=0;
        Retained->Counted=Counted;Counted=false;
        Owner->QuarantineRemoved(Retained);
    } else ReleaseUpload();
    if(Entered)CloseHandle(Entered);if(Resume)CloseHandle(Resume);
    TestGate.Reset();Wrapped.Reset();Commands.Reset();Allocator.Reset();Texture.Reset();Owner.reset();
    if(Counted)--Frames;
    Lui_FrameRelease(Lease);
}
}
using namespace LuiWinUI;
#define FRAME_EXPORT extern "C" __declspec(dllexport)
FRAME_EXPORT int __cdecl LuiWinUI_FramePresenterVersionV1() noexcept {
#ifdef LUI_WINUI_PRESENTER
    return 1;
#else
    return 0;
#endif
}
FRAME_EXPORT void __cdecl LuiWinUI_FrameAllocationFaultForTestV1(int Value) noexcept {AllocationFault.store(Value);}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameDeviceCreateV1(uint32_t Mode,DeviceHandle** Result) noexcept {
    if(!Result)return E_POINTER;*Result=nullptr;
    try {std::shared_ptr<Device> Value;HRESULT Status=CreateDevice(Mode,Value);if(FAILED(Status))return Status;
        *Result=new DeviceHandle(std::move(Value));return S_OK;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
FRAME_EXPORT void __cdecl LuiWinUI_FrameDeviceReleaseV1(DeviceHandle* Value) noexcept {delete Value;}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameDeviceBindOwnerV1(DeviceHandle* Value) noexcept {
    if(!Value||!*Value)return E_INVALIDARG;
    DWORD Expected=0;const DWORD Current=GetCurrentThreadId();
    if((*Value)->OwnerThread.compare_exchange_strong(Expected,Current,std::memory_order_release)||Expected==Current)return S_OK;
    return RPC_E_WRONG_THREAD;
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameDeviceRemovedV1(DeviceHandle* Value) noexcept {
    return Value&&*Value?(*Value)->Native->GetDeviceRemovedReason():E_INVALIDARG;
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameDeviceRemoveForTestV1(DeviceHandle* Value) noexcept {
    if(!Value||!*Value)return E_INVALIDARG;ComPtr<ID3D12Device5> Test;
    HRESULT Status=(*Value)->Native.As(&Test);if(FAILED(Status))return Status;
    Test->RemoveDevice();++Removed;return (*Value)->Native->GetDeviceRemovedReason();
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameBeginV1(DeviceHandle* Value,LuiFrame* Lease,Frame** Result) noexcept {
    try {return CreateFrame(Value,Lease,Result);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameCopyGateForTestV1(Frame* Value,HANDLE Entered,HANDLE Resume) noexcept {
    if(!Value||Value->OwnerCall()!=S_OK||Value->CopyState.load()!=0||!Entered||!Resume)return E_INVALIDARG;
    HANDLE Process=GetCurrentProcess();
    if(!DuplicateHandle(Process,Entered,Process,&Value->Entered,0,FALSE,DUPLICATE_SAME_ACCESS))return HRESULT_FROM_WIN32(GetLastError());
    if(!DuplicateHandle(Process,Resume,Process,&Value->Resume,0,FALSE,DUPLICATE_SAME_ACCESS))return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameCopyV1(Frame* Value) noexcept {
    if(!Value||Value->OwnerCall()==S_OK||!Value->Pixels)return E_INVALIDARG;
    int Expected=0;if(!Value->CopyState.compare_exchange_strong(Expected,1))return E_UNEXPECTED;
    HRESULT Status=S_OK;
    for(uint32_t Row=0;Row<Value->View.Height;++Row) {
        if(Value->Canceled.load()){Status=E_ABORT;break;}
        std::memcpy(Value->Pixels+Value->Footprint.Offset+size_t(Row)*Value->Footprint.Footprint.RowPitch,
            static_cast<const unsigned char*>(Value->View.Pixels)+size_t(Row)*Value->View.RowStride,Value->View.RowStride);
        if(Value->Entered && Row+1==(Value->View.Height+1)/2) {
            SetEvent(Value->Entered);
            if(WaitForSingleObject(Value->Resume,10000)!=WAIT_OBJECT_0){Status=E_ABORT;break;}
        }
    }
    ++Copies;Value->CopyState.store(SUCCEEDED(Status)?2:3);return Status;
}
FRAME_EXPORT void __cdecl LuiWinUI_FrameCancelV1(Frame* Value) noexcept {if(Value)Value->Canceled.store(true);}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameGpuGateForTestV1(Frame* Value,int Enable) noexcept {
    if(!Value||Value->OwnerCall()!=S_OK)return E_INVALIDARG;
    if(!Enable)return Value->TestGate?Value->TestGate->Signal(1):S_OK;
    if(Value->TestGate||Value->Submitted)return E_INVALIDARG;
    HRESULT Status=Value->Owner->Native->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&Value->TestGate));
    return FAILED(Status)?Status:Value->Owner->Queue->Wait(Value->TestGate.Get(),1);
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameSubmitCopyV1(Frame* Value) noexcept {
    if(!Value||Value->OwnerCall()!=S_OK||Value->CopyState.load()!=2||Value->Submitted||Value->Canceled.load())return E_INVALIDARG;
    if(Value->Pixels){Value->Upload->Unmap(0,nullptr);Value->Pixels=nullptr;}
    HRESULT Status=Value->Allocator->Reset();if(FAILED(Status))return Status;
    Status=Value->Commands->Reset(Value->Allocator.Get(),nullptr);if(FAILED(Status))return Status;
    D3D12_TEXTURE_COPY_LOCATION Source{};Source.pResource=Value->Upload.Get();Source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;Source.PlacedFootprint=Value->Footprint;
    D3D12_TEXTURE_COPY_LOCATION Target{};Target.pResource=Value->Texture.Get();Target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    Value->Commands->CopyTextureRegion(&Target,0,0,0,&Source,nullptr);
    D3D12_RESOURCE_BARRIER Barrier{};Barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource=Value->Texture.Get();Barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
    Barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON;Barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Value->Commands->ResourceBarrier(1,&Barrier);Status=Value->Commands->Close();if(FAILED(Status))return Status;
    ID3D12CommandList* Lists[]={Value->Commands.Get()};Value->Submitted=true;
    Value->Owner->Queue->ExecuteCommandLists(1,Lists);
    return Value->Owner->Signal(Value->FenceValue);
}
FRAME_EXPORT int __cdecl LuiWinUI_FramePollV1(Frame* Value) noexcept {
    if(!Value)return -2;const int Status=Value->Status();
    if(Status==1)Value->ReleaseUpload();return Status;
}
FRAME_EXPORT int __cdecl LuiWinUI_FrameReleaseV1(Frame* Value) noexcept {
    if(!Value)return 1;
    if(Value->Source||Value->CopyState.load()==1)return 0;
    const int Status=Value->Status();
    if(Status==0||Status==-2||(Status==-1&&Value->Submitted&&!Value->RemovedReserve))return 0;
    Value->RemovedAtRetirement=Status==-1&&Value->Submitted;
    delete Value;return 1;
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameGpuStatsV1(Stats* Value) noexcept {
    if(!Value||Value->StructSize!=sizeof(Stats)||Value->Version!=1)return E_INVALIDARG;
    *Value={sizeof(Stats),1,Uploads.load(),UploadBytes.load(),PeakUploadBytes.load(),Devices.load(),Sources.load(),Frames.load(),Copies.load(),Draws.load(),Removed.load()};return S_OK;
}
