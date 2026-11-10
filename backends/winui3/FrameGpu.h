#pragma once
#include "LuiRuntime.h"
#include <windows.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <atomic>
#include <memory>

namespace LuiWinUI {
using Microsoft::WRL::ComPtr;
constexpr uint64_t MaxStaging = 8*1024*1024;
// Prepared before submission so removed-device retirement cannot require an
// allocation while a driver worker might still reference the GPU resources.
struct RemovedFrameResources {
    ComPtr<ID3D12Resource> Upload, Texture;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> Commands;
    ComPtr<ID3D11Resource> Wrapped;
    ComPtr<ID3D12Fence> TestGate;
    LuiFrame* Lease = nullptr;
    uint64_t Capacity = 0;
    bool Counted = false;
    RemovedFrameResources* Next = nullptr;
    ~RemovedFrameResources();
};
struct Device {
    ComPtr<ID3D12Device> Native;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12Fence> Fence;
    ComPtr<ID3D11Device> Bridge;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11On12Device> Interop;
    uint64_t NextFence = 0;
    bool Counted = false;
    std::atomic<DWORD> OwnerThread{0};
    std::atomic<RemovedFrameResources*> RemovedHead{nullptr};
    ~Device();
    HRESULT Signal(uint64_t& Value);
    void QuarantineRemoved(RemovedFrameResources* Value) noexcept;
};
using DeviceHandle = std::shared_ptr<Device>;
struct Frame {
    std::shared_ptr<Device> Owner;
    LuiFrame* Lease = nullptr;
    LuiFrameViewV1 View{sizeof(View),1};
    ComPtr<ID3D12Resource> Upload, Texture;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> Commands;
    ComPtr<ID3D11Resource> Wrapped;
    ComPtr<ID3D12Fence> TestGate;
    ComPtr<IUnknown> Source; // Created, detached and released only on XAML owner.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint{};
    uint64_t Capacity = 0, FenceValue = 0;
    unsigned char* Pixels = nullptr;
    HANDLE Entered = nullptr, Resume = nullptr;
    std::atomic<bool> Canceled{false};
    std::atomic<int> CopyState{0};
    bool Submitted = false, Counted = false;
    bool RemovedAtRetirement = false;
    std::unique_ptr<RemovedFrameResources> RemovedReserve;
    ~Frame();
    HRESULT OwnerCall() const;
    int Status(); // 0 pending; 1 complete; -1 removed; -2 unfenced/quarantined.
    void ReleaseUpload();
};
struct Stats {
    uint32_t StructSize, Version;
    uint64_t Uploads, UploadBytes, PeakUploadBytes, Devices, Sources, Frames, Copies, Draws, Removed;
};
extern std::atomic<uint64_t> Uploads, UploadBytes, PeakUploadBytes, Devices, Sources, Frames, Copies, Draws, Removed;
HRESULT SurfaceCreate(Frame* Value);
HRESULT SurfaceAttach(IUnknown* Image, Frame* Value);
HRESULT SurfaceClose(Frame* Value);
HRESULT SurfaceRefresh(Frame* Value);
}
