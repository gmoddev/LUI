#include "../internal/Frames.h"
#include "../internal/Dispatch.h"
#include "../../ui/layout/Layout.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace {
constexpr uint64_t MaxFrameBytes = 8 * 1024 * 1024;
constexpr uint64_t MaxDomainBytes = 64 * 1024 * 1024;
constexpr uint64_t MaxSurfaceBytes = 48 * 1024 * 1024;
}

struct FrameDomain {
    std::mutex Mutex;
    uint64_t Surfaces = 0, Frames = 0, PixelBytes = 0, StagingBytes = 0, PeakBytes = 0;
};
struct FrameSurfaceSlot {
    std::shared_ptr<FrameDomain> Domain;
    uint64_t Frames = 0, PixelBytes = 0, StagingBytes = 0;
    explicit FrameSurfaceSlot(std::shared_ptr<FrameDomain> Value): Domain(std::move(Value)) {}
    ~FrameSurfaceSlot() {
        std::lock_guard<std::mutex> Lock(Domain->Mutex);
        --Domain->Surfaces;
    }
};
struct LuiFrame {
    std::atomic<uint32_t> References{1};
    std::atomic<int> Completion{0};
    std::shared_ptr<FrameSurfaceSlot> Slot;
    void* Pixels = nullptr;
    uint64_t Capacity = 0, ByteCount = 0, StagingBytes = 0, Epoch = 0, Sequence = 0;
    uint32_t Width = 0, Height = 0, RowStride = 0;
    ~LuiFrame() {
        std::free(Pixels);
        if (!Slot) return;
        auto& Domain = *Slot->Domain;
        std::lock_guard<std::mutex> Lock(Domain.Mutex);
        Domain.PixelBytes -= Capacity; Domain.StagingBytes -= StagingBytes; --Domain.Frames;
        Slot->PixelBytes -= Capacity; Slot->StagingBytes -= StagingBytes; --Slot->Frames;
    }
};

bool FrameBackendAvailable(const LuiRuntime* Runtime) {
    const auto& Backend = Runtime->Backend;
    return Runtime->Frames.Submit || !(Backend.Create || Backend.Property || Backend.Parent || Backend.Arrange || Backend.Destroy);
}

bool RegisterFrameSurface(LuiRuntime* Runtime, Node* Value) {
    // Match NativeDomain's owner-scoped persistence across replacement runtimes.
    // Leases keep this allocation budget alive, never an Instance or a VM.
    static thread_local std::weak_ptr<FrameDomain> OwnerDomain;
    try {
        if (!Runtime->FrameResources) {
            Runtime->FrameResources = OwnerDomain.lock();
            if (!Runtime->FrameResources) {
                Runtime->FrameResources = std::make_shared<FrameDomain>();
                OwnerDomain = Runtime->FrameResources;
            }
        }
        {
            std::lock_guard<std::mutex> Lock(Runtime->FrameResources->Mutex);
            if (Runtime->FrameResources->Surfaces >= 4) return false;
            auto Slot = std::make_shared<FrameSurfaceSlot>(Runtime->FrameResources);
            ++Runtime->FrameResources->Surfaces;
            Value->FrameSlot = std::move(Slot);
        }
        return true;
    } catch (const std::bad_alloc&) { return false; }
}

extern "C" LUI_API int LUI_CALL Lui_FrameRetain(LuiFrame* Frame) {
    if (!Frame) return 0;
    auto Count = Frame->References.load(std::memory_order_relaxed);
    while (Count && Count != std::numeric_limits<uint32_t>::max())
        if (Frame->References.compare_exchange_weak(Count, Count + 1, std::memory_order_relaxed)) return 1;
    return 0;
}
extern "C" LUI_API void LUI_CALL Lui_FrameRelease(LuiFrame* Frame) {
    if (Frame && Frame->References.fetch_sub(1, std::memory_order_acq_rel) == 1) delete Frame;
}
extern "C" LUI_API int LUI_CALL Lui_FrameView(LuiFrame* Frame, LuiFrameViewV1* View) {
    if (!Frame || !View || View->StructSize != sizeof(*View) || View->AbiVersion != 1) return 0;
    *View = {sizeof(*View), 1, Frame->Pixels, Frame->ByteCount, Frame->Epoch, Frame->Sequence,
        Frame->Width, Frame->Height, Frame->RowStride, 1};
    return 1;
}
extern "C" LUI_API int LUI_CALL Lui_FrameComplete(LuiFrame* Frame, int Success) {
    if (!Frame || (Success != 0 && Success != 1)) return 0;
    int Expected = 0;
    return Frame->Completion.compare_exchange_strong(Expected, Success ? 1 : 2, std::memory_order_release) ? 1 : 0;
}
extern "C" LUI_API int LUI_CALL Lui_FrameReserveStaging(LuiFrame* Frame, uint64_t Bytes) {
    if (!Frame || !Bytes || Bytes > MaxFrameBytes) return 0;
    auto& Slot = *Frame->Slot; auto& Domain = *Slot.Domain;
    std::lock_guard<std::mutex> Lock(Domain.Mutex);
    if (Frame->StagingBytes || Bytes > MaxDomainBytes - Domain.PixelBytes - Domain.StagingBytes ||
        Bytes > MaxSurfaceBytes - Slot.PixelBytes - Slot.StagingBytes) return 0;
    Frame->StagingBytes = Bytes; Slot.StagingBytes += Bytes; Domain.StagingBytes += Bytes;
    Domain.PeakBytes = std::max(Domain.PeakBytes, Domain.PixelBytes + Domain.StagingBytes);
    return 1;
}
extern "C" LUI_API int LUI_CALL Lui_SetFrameCallbacks(LuiRuntime* Runtime, const LuiFrameCallbacksV1* Callbacks) {
    if (!CheckOwner(Runtime) || !Callbacks || Callbacks->StructSize != sizeof(*Callbacks) ||
        Callbacks->AbiVersion != 1 || Callbacks->Reserved || !Callbacks->Submit || !Callbacks->Publish || !Callbacks->Clear ||
        Runtime->Frames.Submit || Runtime->ApplicationStarted || Runtime->NextNodeId != 1 ||
        Runtime->VmDepth || Runtime->BackendDepth || Runtime->ShuttingDown) return 0;
    Runtime->Frames = *Callbacks; return 1;
}
extern "C" LUI_API int LUI_CALL Lui_GetFrameStats(LuiRuntime* Runtime, LuiFrameStatsV1* Stats) {
    if (!CheckOwner(Runtime) || !Stats || Stats->StructSize != sizeof(*Stats) || Stats->AbiVersion != 1) return 0;
    *Stats = {sizeof(*Stats), 1};
    if (Runtime->FrameResources) {
        auto& Domain = *Runtime->FrameResources;
        std::lock_guard<std::mutex> Lock(Domain.Mutex);
        Stats->Surfaces = Domain.Surfaces; Stats->Frames = Domain.Frames;
        Stats->PixelBytes = Domain.PixelBytes; Stats->StagingBytes = Domain.StagingBytes; Stats->PeakBytes = Domain.PeakBytes;
    }
    for (const auto& Pair : Runtime->Nodes) {
        Stats->Pending += Pair.second->PendingFrame ? 1 : 0;
        Stats->InFlight += Pair.second->InFlightFrame ? 1 : 0;
    }
    Stats->Accepted = Runtime->FramesAccepted; Stats->Replaced = Runtime->FramesReplaced;
    Stats->Rejected = Runtime->FramesRejected; Stats->Consumed = Runtime->FramesConsumed; Stats->Stale = Runtime->FramesStale;
    return 1;
}

void ClearFrameSurface(LuiRuntime* Runtime, Node* Value, bool Destroy) {
    if (!Value->FrameSlot) return;
    if (Value->FrameEpoch != std::numeric_limits<uint64_t>::max()) ++Value->FrameEpoch;
    Lui_FrameRelease(Value->PendingFrame); Value->PendingFrame = nullptr;
    Lui_FrameRelease(Value->DisplayedFrame); Value->DisplayedFrame = nullptr;
    Value->FrameState = "Empty";
    if (Runtime->Frames.Clear) {
        ++Runtime->BackendDepth;
        Runtime->Frames.Clear(Runtime->Frames.Context, Value->Id, Value->FrameEpoch);
        --Runtime->BackendDepth;
    }
    // Keep a canceled upload in its slot until it settles. Destroy may release
    // the owner reference: an async adapter holds its own independent lease.
    if (Destroy) {
        Lui_FrameRelease(Value->InFlightFrame); Value->InFlightFrame = nullptr;
        Value->FrameSlot.reset();
    }
}

const char* SubmitFrameSurface(LuiRuntime* Runtime, Node* Value, const void* Pixels, size_t Length,
    double Width, double Height, double RowStride, const char* Format, size_t FormatLength) {
    if (Value->Destroyed || Value->DestroyingInProgress || Runtime->ShuttingDown || !Value->FrameSlot) return "Closed";
    if (!FrameBackendAvailable(Runtime)) return "Unsupported";
    if (!Pixels || !Format || !std::isfinite(Width) || !std::isfinite(Height) || !std::isfinite(RowStride) ||
        Width < 1 || Height < 1 || RowStride < 1 || std::floor(Width) != Width || std::floor(Height) != Height ||
        std::floor(RowStride) != RowStride) return "InvalidArgument";
    if (FormatLength != 18 || std::memcmp(Format, "BGRA8Premultiplied", 18) != 0) return "Unsupported";
    if (Width > 1920 || Height > 1080 || RowStride > MaxFrameBytes) return "LimitExceeded";
    const auto W = static_cast<uint32_t>(Width), H = static_cast<uint32_t>(Height), Stride = static_cast<uint32_t>(RowStride);
    if (Stride % 4 || Stride < W * 4) return "InvalidArgument";
    if (Stride > MaxFrameBytes / H) return "LimitExceeded";
    if (Length != static_cast<uint64_t>(Stride) * H) return "InvalidArgument";
    if (Value->FrameEpoch == std::numeric_limits<uint64_t>::max() || Value->FrameSequence == std::numeric_limits<uint64_t>::max()) return "LimitExceeded";
    const uint64_t Bytes = static_cast<uint64_t>(W) * 4 * H;
    LuiFrame* Frame = Value->PendingFrame;
    if (!Frame || Frame->Capacity < Bytes) {
        auto& Slot = *Value->FrameSlot; auto& Domain = *Slot.Domain;
        {
            std::lock_guard<std::mutex> Lock(Domain.Mutex);
            if (Slot.Frames >= 3 || Bytes > MaxDomainBytes - Domain.PixelBytes - Domain.StagingBytes ||
                Bytes > MaxSurfaceBytes - Slot.PixelBytes - Slot.StagingBytes) return "WouldBlock";
            ++Slot.Frames; ++Domain.Frames; Slot.PixelBytes += Bytes; Domain.PixelBytes += Bytes;
            Domain.PeakBytes = std::max(Domain.PeakBytes, Domain.PixelBytes + Domain.StagingBytes);
        }
        Frame = new (std::nothrow) LuiFrame;
        void* Storage = Frame ? (Runtime->FrameAllocator ? Runtime->FrameAllocator(static_cast<size_t>(Bytes)) : std::malloc(static_cast<size_t>(Bytes))) : nullptr;
        if (!Storage) {
            delete Frame; // Slot is assigned only after successful allocation.
            std::lock_guard<std::mutex> Lock(Domain.Mutex);
            --Slot.Frames; --Domain.Frames; Slot.PixelBytes -= Bytes; Domain.PixelBytes -= Bytes;
            return "LimitExceeded";
        }
        Frame->Slot = Value->FrameSlot; Frame->Capacity = Bytes; Frame->Pixels = Storage;
    }
    for (uint32_t Row = 0; Row < H; ++Row)
        std::memcpy(static_cast<unsigned char*>(Frame->Pixels) + static_cast<size_t>(Row) * W * 4,
            static_cast<const unsigned char*>(Pixels) + static_cast<size_t>(Row) * Stride, W * 4);
    Frame->Width = W; Frame->Height = H; Frame->RowStride = W * 4; Frame->ByteCount = Bytes;
    Frame->Epoch = Value->FrameEpoch; Frame->Sequence = ++Value->FrameSequence;
    if (Value->PendingFrame) {
        ++Runtime->FramesReplaced;
        if (Value->PendingFrame != Frame) Lui_FrameRelease(Value->PendingFrame);
    }
    Value->PendingFrame = Frame; Value->FrameState = "Pending"; ++Runtime->FramesAccepted;
    return nullptr;
}

namespace {
bool CanUpload(const LuiRuntime* Runtime, const Node* Value) {
    if (!Runtime->Frames.Submit) return true; // headless semantic consumption
    if (Value->Bounds.Width <= 0 || Value->Bounds.Height <= 0 || !LuiLayout::HasViewportIntersection(Runtime, Value)) return false;
    for (const Node* Current = Value; Current;) {
        if (!Current->Visible) return false;
        if (Current->ClassName == "Window") {
            // Windows always clip native presentation; the input helper above
            // deliberately preserves legacy explicit-ID input without a clip.
            return Value->Bounds.X < Current->Bounds.X + Current->Bounds.Width &&
                Value->Bounds.Y < Current->Bounds.Y + Current->Bounds.Height &&
                Value->Bounds.X + Value->Bounds.Width > Current->Bounds.X &&
                Value->Bounds.Y + Value->Bounds.Height > Current->Bounds.Y;
        }
        const auto Found = Runtime->Nodes.find(Current->ParentId);
        Current = Found == Runtime->Nodes.end() ? nullptr : Found->second.get();
    }
    return false;
}
void Settle(LuiRuntime* Runtime, Node* Value) {
    LuiFrame* Frame = Value->InFlightFrame;
    const int Result = Frame ? Frame->Completion.load(std::memory_order_acquire) : 0;
    if (!Result) return;
    // A completed native upload stays bounded in its slot while hidden. Stale
    // results can be retired immediately, including after ClearFrame.
    const bool Current = Frame->Epoch == Value->FrameEpoch && Frame->Sequence == Value->FrameSequence;
    if (Current && Result == 1 && !CanUpload(Runtime, Value)) return;
    Value->InFlightFrame = nullptr;
    if (!Current) ++Runtime->FramesStale;
    else if (Result == 1) {
        if (Runtime->Frames.Publish) {
            ++Runtime->BackendDepth;
            const int Published = Runtime->Frames.Publish(Runtime->Frames.Context, Value->Id, Frame,
                Value->ScaleMode == "Fit" ? 0 : Value->ScaleMode == "Fill" ? 1 : 2);
            --Runtime->BackendDepth;
            if (Published != 1) {
                Value->FrameState = "Failed"; Lui_FrameRelease(Frame); return;
            }
        }
        Lui_FrameRelease(Value->DisplayedFrame); Value->DisplayedFrame = Frame;
        Value->FrameState = "Ready"; ++Runtime->FramesConsumed; return;
    } else Value->FrameState = "Failed";
    Lui_FrameRelease(Frame);
}
}

int PumpFrameSurfaces(LuiRuntime* Runtime) {
    if (Runtime->BackendFailed || Runtime->ShuttingDown || Runtime->ProjectBootstrap || Runtime->ModuleDepth) return 0;
    // No historical frame queue, no callbacks into Luau, <=4 entries per pump.
    int Ids[4]; size_t Count = 0;
    for (const auto& Pair : Runtime->Nodes) if (Pair.second->FrameSlot && !Pair.second->Destroyed) Ids[Count++] = Pair.first;
    std::sort(Ids, Ids + Count);
    int Submitted = 0;
    for (size_t Index = 0; Index < Count; ++Index) {
        Node* Value = Runtime->Nodes.at(Ids[Index]).get(); Settle(Runtime, Value);
        if (Value->InFlightFrame || !Value->PendingFrame || !CanUpload(Runtime, Value)) continue;
        Value->InFlightFrame = Value->PendingFrame; Value->PendingFrame = nullptr;
        LuiFrame* Frame = Value->InFlightFrame;
        if (Runtime->Frames.Submit) {
            ++Runtime->BackendDepth;
            const int Accepted = Runtime->Frames.Submit(Runtime->Frames.Context, Value->Id, Frame,
                Value->ScaleMode == "Fit" ? 0 : Value->ScaleMode == "Fill" ? 1 : 2);
            --Runtime->BackendDepth;
            if (Accepted != 1) Frame->Completion.store(2, std::memory_order_release);
        } else Lui_FrameComplete(Frame, 1);
        ++Submitted; Settle(Runtime, Value);
    }
    return Submitted;
}
