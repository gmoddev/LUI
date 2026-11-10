#include "LuiRuntime.h"
#include "../../runtime/internal/State.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
int Failures = 0;
void Check(bool Condition, const char* Message) {
    if (!Condition) { ++Failures; std::fprintf(stderr, "[LUI:FrameTest] %s\n", Message); }
}
void Run(LuiRuntime* Runtime, const char* Source) {
    const int Status = Lui_RunScript(Runtime, Source, "FrameTest");
    Check(Status == 1, Lui_GetLastError(Runtime));
}
LuiFrameStatsV1 Stats(LuiRuntime* Runtime) {
    LuiFrameStatsV1 Result{sizeof(Result), 1};
    Check(Lui_GetFrameStats(Runtime, &Result) == 1, "frame stats boundary"); return Result;
}
struct Adapter {
    std::vector<LuiFrame*> Leases;
    std::vector<unsigned char> FirstBytes;
    std::vector<unsigned char> PublishedBytes;
    std::vector<uint64_t> Sequences;
    int Clears = 0;
    bool Synchronous = false, Fail = false, FailPublish = false, Staging = false;
    uint32_t ScaleMode = 0;
    ~Adapter() { for (auto* Frame : Leases) Lui_FrameRelease(Frame); }
    void Release() { for (auto* Frame : Leases) Lui_FrameRelease(Frame); Leases.clear(); }
};
int LUI_CALL Submit(void* Pointer, int, LuiFrame* Frame, uint32_t ScaleMode) {
    auto& Value = *static_cast<Adapter*>(Pointer);
    LuiFrameViewV1 View{sizeof(View), 1}; Check(Lui_FrameView(Frame, &View) == 1, "immutable frame view");
    Check(View.Format == 1 && View.RowStride == View.Width * 4 && View.ByteCount == uint64_t(View.RowStride) * View.Height, "packed metadata");
    Value.FirstBytes.push_back(*static_cast<const unsigned char*>(View.Pixels));
    Value.Sequences.push_back(View.Sequence); Value.ScaleMode = ScaleMode;
    if (Value.Staging) Check(Lui_FrameReserveStaging(Frame, View.ByteCount) == 1, "native staging reservation");
    if (Value.Fail) return 0;
    if (Value.Synchronous) Check(Lui_FrameComplete(Frame, 1) == 1, "synchronous completion");
    else { Check(Lui_FrameRetain(Frame) == 1, "async lease"); Value.Leases.push_back(Frame); }
    return 1;
}
void LUI_CALL Clear(void* Pointer, int, uint64_t) { ++static_cast<Adapter*>(Pointer)->Clears; }
int LUI_CALL Publish(void* Pointer, int, LuiFrame* Frame, uint32_t ScaleMode) {
    auto& Value = *static_cast<Adapter*>(Pointer);
    if (Value.FailPublish) return 0;
    LuiFrameViewV1 View{sizeof(View), 1}; Check(Lui_FrameView(Frame, &View) == 1, "publish immutable lease");
    Value.PublishedBytes.push_back(*static_cast<const unsigned char*>(View.Pixels)); Value.ScaleMode = ScaleMode;
    return 1;
}
void Install(LuiRuntime* Runtime, Adapter& Value) {
    LuiFrameCallbacksV1 Callbacks{sizeof(Callbacks), 1, &Value, Submit, Publish, Clear, 0};
    auto Invalid = Callbacks; Invalid.StructSize--; Check(!Lui_SetFrameCallbacks(Runtime, &Invalid), "callback size validation");
    Invalid = Callbacks; Invalid.AbiVersion++; Check(!Lui_SetFrameCallbacks(Runtime, &Invalid), "callback version validation");
    Invalid = Callbacks; Invalid.Reserved = 1; Check(!Lui_SetFrameCallbacks(Runtime, &Invalid), "callback reserved validation");
    Invalid = Callbacks; Invalid.Clear = nullptr; Check(!Lui_SetFrameCallbacks(Runtime, &Invalid), "callback completeness validation");
    Invalid = Callbacks; Invalid.Publish = nullptr; Check(!Lui_SetFrameCallbacks(Runtime, &Invalid), "owner publication required");
    Check(Lui_SetFrameCallbacks(Runtime, &Callbacks) && !Lui_SetFrameCallbacks(Runtime, &Callbacks), "one-time bridge registration");
}
constexpr const char* Setup = R"(
    W=Instance.new('Window',{Visible=true,Size=UDim2.fromOffset(640,480)})
    Root=Instance.new('Frame',{Parent=W,ClipsDescendants=true})
    S=Instance.new('FrameSurface',{Parent=Root})
    B=buffer.create(4); buffer.writeu8(B,0,11)
    function Put(N) buffer.writeu8(B,0,N); assert(S:SubmitFrame(B,1,1,4,'BGRA8Premultiplied')) end
)";
void ApiAndValidation() {
    auto* Runtime = Lui_Create();
    const std::string Schema = Lui_GetSchemaJson();
    Check(Schema.find("FrameSurface") != std::string::npos && Schema.find("SubmitFrame") != std::string::npos, "reflected API");
    Run(Runtime, Setup);
    Run(Runtime, R"(
        assert(S:IsA('FrameSurface') and S:IsA('GuiObject') and S:IsA('Instance'))
        assert(S.ScaleMode=='Fit' and S.FrameState=='Empty')
        assert(not app:GetService('PlatformService'):Supports('FrameSurface'))
        assert(S.AbsoluteSize.X==640 and S.AbsoluteSize.Y==480)
        S.ScaleMode='Fill'; local C=S:Clone(); assert(C.ScaleMode=='Fill' and C.FrameState=='Empty'); C:Destroy()
        assert(not pcall(function() S.FrameState='Ready' end))
        assert(not pcall(function() S.ScaleMode='Bad' end))
        assert(not pcall(function() S:ClearFrame(true) end))
        assert(not pcall(function() local Submit=S.SubmitFrame; Submit(W,B,1,1,4,'BGRA8Premultiplied') end))
        assert(not pcall(function() Instance.new('TextLabel',{Parent=S}) end))
        function Reject(P,X,Y,R,F)
            local Before=S.FrameState
            local Ok,Code=S:SubmitFrame(P,X,Y,R,F)
            assert(not Ok and (Code=='InvalidArgument' or Code=='LimitExceeded' or Code=='Unsupported'))
            assert(S.FrameState==Before)
        end
        Reject(B,0,1,4,'BGRA8Premultiplied'); Reject(B,-1,1,4,'BGRA8Premultiplied')
        Reject(B,1,0,4,'BGRA8Premultiplied'); Reject(B,1,-1,4,'BGRA8Premultiplied')
        Reject(B,1921,1,7684,'BGRA8Premultiplied'); Reject(B,1,1081,4,'BGRA8Premultiplied')
        Reject(B,0/0,1,4,'BGRA8Premultiplied'); Reject(B,math.huge,1,4,'BGRA8Premultiplied')
        Reject(B,1,1,1e300,'BGRA8Premultiplied'); Reject(B,1,1,-4,'BGRA8Premultiplied')
        Reject(B,1,1,3,'BGRA8Premultiplied'); Reject(B,2,1,4,'BGRA8Premultiplied')
        Reject(B,1.5,1,4,'BGRA8Premultiplied'); Reject(B,1,1,4.5,'BGRA8Premultiplied')
        Reject(B,1,1,4,'RGBA8'); Reject(B,1,1,4,'BGRA8Premultiplied\000private')
        Reject(buffer.create(3),1,1,4,'BGRA8Premultiplied'); Reject(buffer.create(5),1,1,4,'BGRA8Premultiplied')
        Reject({},1,1,4,'BGRA8Premultiplied'); Reject(B,'1',1,4,'BGRA8Premultiplied')
        assert(not S:SubmitFrame()); assert(not S:SubmitFrame(B,1,1,4,'BGRA8Premultiplied',true))
        for I=1,1000 do Reject(B,I+2,I%1080+1,I*7919,'BGRA8Premultiplied') end
        local Large=buffer.create(1920*1080*4)
        assert(S:SubmitFrame(Large,1920,1080,1920*4,'BGRA8Premultiplied'))
        assert(S.FrameState=='Pending'); S:ClearFrame(); S:ClearFrame(); assert(S.FrameState=='Empty')
        Reject(B,1920,1080,8192,'BGRA8Premultiplied')
        Large=nil
        for I=1,100 do Put(I) end
        assert(S.FrameState=='Pending')
    )");
    auto Before = Stats(Runtime);
    Check(Before.Pending == 1 && Before.Frames == 1 && Before.PixelBytes == 4 && Before.Replaced == 99, "bounded pending replacement");
    Lui_Pump(Runtime); Run(Runtime, "assert(S.FrameState=='Ready'); C=S:Clone(); assert(C.FrameState=='Empty'); C:Destroy()");
    Run(Runtime, "S:Destroy(); S:Destroy(); S:ClearFrame(); local Ok,Code=S:SubmitFrame(B,1,1,4,'BGRA8Premultiplied'); assert(not Ok and Code=='Closed')");
    Check(Stats(Runtime).PixelBytes == 0 && Stats(Runtime).Surfaces == 0, "clear/destroy release");
    Run(Runtime, "T={}; for I=1,4 do T[I]=Instance.new('FrameSurface') end; assert(not pcall(function() Instance.new('FrameSurface') end)); assert(not pcall(function() T[1]:Clone() end)); for _,V in T do V:Destroy() end");
    Check(Stats(Runtime).Surfaces == 0, "quota rejection rollback");
    Run(Runtime, "Group=Instance.new('Frame'); for I=1,3 do Instance.new('FrameSurface',{Parent=Group}) end; assert(not pcall(function() Group:Clone() end))");
    Check(Stats(Runtime).Surfaces == 3, "failed recursive clone retires every partial surface");
    Run(Runtime, "Group:Destroy()");
    Lui_Destroy(Runtime);
}
void* FailAllocation(size_t) { return nullptr; }
void OwnershipAndRaces() {
    Adapter Value; auto* Runtime = Lui_Create(); Install(Runtime, Value); Run(Runtime, Setup);
    Run(Runtime, "assert(app:GetService('PlatformService'):Supports('FrameSurface')); Put(21); buffer.writeu8(B,0,99)");
    Check(Value.Leases.empty(), "script/layout does not upload before pump");
    Lui_Pump(Runtime); Check(Value.FirstBytes.back() == 21, "caller mutation cannot alter snapshot");
    Run(Runtime, "Put(22); Put(23); Put(24)");
    auto Current = Stats(Runtime);
    Check(Current.Pending == 1 && Current.InFlight == 1 && Current.Frames == 2, "one pending one upload");
    std::thread Worker([&] { Check(Lui_FrameComplete(Value.Leases[0], 1) && !Lui_FrameComplete(Value.Leases[0], 1), "worker exactly-once settlement"); }); Worker.join();
    Lui_Pump(Runtime);
    Check(Value.Leases.size() == 2 && Value.FirstBytes.back() == 24 && Stats(Runtime).Stale == 1, "stale completion cannot publish; latest wins");
    Check(Value.PublishedBytes.empty(), "stale upload never reaches native publication callback");
    Check(Lui_FrameComplete(Value.Leases[1], 1), "latest completion"); Lui_Pump(Runtime);
    Check(Value.PublishedBytes.size() == 1 && Value.PublishedBytes.back() == 24, "only latest content publishes");
    Run(Runtime, "assert(S.FrameState=='Ready'); S.ScaleMode='Stretch'");
    Value.Release();
    Runtime->FrameAllocator = FailAllocation;
    auto Before = Stats(Runtime);
    Run(Runtime, "local Ok,Code=S:SubmitFrame(buffer.create(16),2,2,8,'BGRA8Premultiplied'); assert(not Ok and Code=='LimitExceeded' and S.FrameState=='Ready')");
    auto After = Stats(Runtime);
    Check(After.Frames == Before.Frames && After.PixelBytes == Before.PixelBytes && After.Accepted == Before.Accepted, "allocation failure is atomic");
    Runtime->FrameAllocator = nullptr;
    Run(Runtime, "local P=buffer.create(16); buffer.writeu8(P,0,31); buffer.writeu8(P,8,32); assert(S:SubmitFrame(P,1,2,8,'BGRA8Premultiplied'))");
    Lui_Pump(Runtime); Check(Value.ScaleMode == 2, "scale passed to adapter");
    LuiFrameViewV1 View{sizeof(View), 1}; Lui_FrameView(Value.Leases.back(), &View);
    Check(View.ByteCount == 8 && static_cast<const unsigned char*>(View.Pixels)[4] == 32, "padded rows normalized");
    Run(Runtime, "S:ClearFrame(); Put(33)");
    const auto Uploads = Value.Leases.size(); Lui_Pump(Runtime);
    Check(Value.Leases.size() == Uploads, "clear keeps canceled upload bounded until settlement");
    Lui_FrameComplete(Value.Leases.back(), 1); Lui_Pump(Runtime);
    Check(Value.FirstBytes.back() == 33, "clear invalidates old publication");
    Run(Runtime, "S:Destroy(); S:Destroy()");
    Check(Stats(Runtime).PixelBytes > 0, "destroyed retained frames remain charged");
    auto* Old = Value.Leases.back(); Lui_Destroy(Runtime);
    // A new runtime starts object IDs from one. Completing old leases has no VM pointer.
    Runtime = Lui_Create(); Run(Runtime, "S=Instance.new('FrameSurface'); assert(S.FrameState=='Empty')");
    Check(Stats(Runtime).PixelBytes > 0, "retired generation shares persistent accounting");
    std::thread Late([&] { Lui_FrameComplete(Old, 1); Value.Release(); }); Late.join();
    Lui_Pump(Runtime); Run(Runtime, "assert(S.FrameState=='Empty'); S:Destroy()");
    Check(Stats(Runtime).PixelBytes == 0 && Stats(Runtime).StagingBytes == 0 && Stats(Runtime).Surfaces == 0, "last async release frees retired ownership");
    Lui_Destroy(Runtime);
}
void BackpressureAndVisibility() {
    Adapter Value; Value.Staging = true;
    auto* Runtime = Lui_Create(); Install(Runtime, Value); Run(Runtime, Setup);
    Run(Runtime, "W.Visible=false; Put(1)"); Lui_Pump(Runtime); Check(Value.Leases.empty(), "hidden native upload suppressed");
    Run(Runtime, "W.Visible=true; S.Position=UDim2.fromOffset(1000,0)");
    Lui_Pump(Runtime); Check(Value.Leases.empty(), "clipped upload suppressed");
    Run(Runtime, "S.Position=UDim2.fromOffset(0,0); Put(2)"); Lui_Pump(Runtime);
    Check(Value.Leases.size() == 1 && Stats(Runtime).StagingBytes == 4, "staging charged");
    Check(!Lui_FrameReserveStaging(Value.Leases.back(), 4) && !Lui_FrameReserveStaging(Value.Leases.back(), uint64_t(9)*1024*1024), "staging boundary validation");
    Run(Runtime, "S.Parent=nil; Put(3)"); Lui_FrameComplete(Value.Leases.back(), 1); Lui_Pump(Runtime);
    Check(Value.Leases.size() == 1, "detached upload suppressed");
    Run(Runtime, "S.Parent=W"); Lui_Pump(Runtime); Check(Value.FirstBytes.back() == 3, "reparent retains newest content");
    Lui_FrameComplete(Value.Leases.back(), 0); Lui_Pump(Runtime); Run(Runtime, "assert(S.FrameState=='Failed'); S:ClearFrame()");
    Value.Release(); Value.Staging = false; Value.Synchronous = true;
    Run(Runtime, R"(
        Surfaces={S}; for I=2,4 do Surfaces[I]=Instance.new('FrameSurface',{Parent=W}) end
        for I=1,100 do for J,V in Surfaces do buffer.writeu8(B,0,I); assert(V:SubmitFrame(B,1,1,4,'BGRA8Premultiplied')) end end
        Responded=false; task.defer(function() Responded=true end)
    )");
    const auto Before = Value.Sequences.size(); Lui_Pump(Runtime);
    Check(Value.Sequences.size() == Before + 4 && Stats(Runtime).Pending == 0, "four-surface fairness");
    Run(Runtime, "assert(Responded); for _,V in Surfaces do assert(V.FrameState=='Ready'); V:Destroy() end");
    Check(Stats(Runtime).PixelBytes == 0, "multi-surface quiescence"); Lui_Destroy(Runtime);
}
void SaturationAndPrivacy() {
    Adapter Value;
    LuiDiagnosticPolicyV1 Policy{sizeof(Policy), 1, LUI_DIAGNOSTICS_DEVELOPMENT, 0};
    auto* Runtime = Lui_CreateWithPolicy(&Policy); Install(Runtime, Value); Run(Runtime, Setup);
    Value.Synchronous = true;
    Run(Runtime, "P=buffer.create(8*1024*1024); T={S}; for I=2,4 do T[I]=Instance.new('FrameSurface',{Parent=W}) end; function Big(V) return V:SubmitFrame(P,1,1024,8192,'BGRA8Premultiplied') end; for _,V in T do assert(Big(V)) end");
    // Padding is removed: use a true near-8MiB payload for global saturation.
    Run(Runtime, "P=buffer.create(1920*1080*4); function Big(V) return V:SubmitFrame(P,1920,1080,7680,'BGRA8Premultiplied') end; for _,V in T do assert(Big(V)) end");
    Lui_Pump(Runtime);
    Value.Synchronous = false;
    Run(Runtime, "for _,V in T do assert(Big(V)) end"); Lui_Pump(Runtime);
    auto Bound = Stats(Runtime);
    Check(Bound.Frames == 8 && Bound.PixelBytes == uint64_t(8)*1920*1080*4, "domain budget includes displayed plus upload");
    Run(Runtime, "for _,V in T do local Before=V.FrameState; local Ok,Code=Big(V); assert(not Ok and Code=='WouldBlock' and V.FrameState==Before) end");
    Check(Stats(Runtime).PixelBytes == Bound.PixelBytes && Stats(Runtime).Pending == 0, "saturation has no partial state");
    Run(Runtime, "for _,V in T do V:Destroy() end; assert(not pcall(function() Instance.new('FrameSurface') end))");
    Check(Stats(Runtime).Surfaces == 4, "retired slots bounded until leases release");
    Value.Release(); Run(Runtime, "S=Instance.new('FrameSurface'); Private=buffer.fromstring('SYNTHETIC_FRAME_PRIVATE_CANARY__'); assert(S:SubmitFrame(Private,8,1,32,'BGRA8Premultiplied'))");
    const std::string Canary = "SYNTHETIC_FRAME_PRIVATE_CANARY";
    Check(std::string(Lui_GetPreviewTreeJson(Runtime)).find(Canary) == std::string::npos &&
        std::string(Lui_GetDiagnosticsJson(Runtime)).find(Canary) == std::string::npos &&
        std::string(Lui_GetPerformanceJson(Runtime)).find(Canary) == std::string::npos, "pixel privacy projections");
    Check(Lui_BeginInspection(Runtime) && std::string(Lui_GetInspectionTreeJson(Runtime)).find(Canary) == std::string::npos, "owner inspection has no pixels");
    Run(Runtime, "S:Destroy()"); Lui_Destroy(Runtime);
}
int LUI_CALL DummyCreate(void*, int, const char*) { return 1; }
void Unsupported() {
    auto* Runtime = Lui_Create(); LuiBackendCallbacks Backend{}; Backend.Create = DummyCreate;
    Lui_SetBackend(Runtime, Backend);
    Run(Runtime, "assert(not app:GetService('PlatformService'):Supports('FrameSurface')); local Ok,Failure=pcall(function() Instance.new('FrameSurface') end); assert(not Ok and string.find(Failure,'Unsupported'))");
    Check(Stats(Runtime).Surfaces == 0, "unsupported host has no partial allocation"); Lui_Destroy(Runtime);
    Runtime = Lui_Create(); Run(Runtime, "S=Instance.new('FrameSurface')"); Lui_SetBackend(Runtime, Backend);
    Run(Runtime, "assert(S:SubmitFrame(buffer.create(4),1,1,4,'BGRA8Premultiplied')); S:Destroy()"); Lui_Destroy(Runtime);
}
void Overproduction() {
    Adapter Value; Value.Synchronous = true;
    auto* Runtime = Lui_Create(); Install(Runtime, Value); Run(Runtime, Setup);
    Run(Runtime, "Counter=0; function Burst() for I=1,4 do Counter+=1; Put(Counter%256) end end");
    for (int Tick = 1; Tick <= 250; ++Tick) {
        Run(Runtime, "Burst()"); Lui_Pump(Runtime);
        Check(Value.FirstBytes.back() == (Tick*4)%256, "virtual 120Hz producer / 30Hz pump selects newest");
        Check(Stats(Runtime).Frames == 1 && Stats(Runtime).Pending == 0, "overproduction has no growing history");
    }
    const auto Result = Stats(Runtime);
    Check(Result.Accepted == 1000 && Result.Replaced == 750 && Result.Consumed == 250 && Result.PeakBytes == 8, "overproduction drop and resource accounting");
    Run(Runtime, "S:Destroy()"); Lui_Destroy(Runtime);
}
void ShutdownWithPending() {
    Adapter Value;
    auto* Runtime = Lui_Create(); Install(Runtime, Value); Run(Runtime, Setup);
    int RetiredErrors = 0;
    Lui_SetLogCallback(Runtime, &RetiredErrors, [](void* Context, const char* Level, const char*) {
        if (std::strcmp(Level, "Error") == 0) ++*static_cast<int*>(Context);
    });
    Run(Runtime, "Put(1)"); Lui_Pump(Runtime);
    Run(Runtime, "Put(2); S.Destroying:Connect(function() error('retired callback') end); W.Destroying:Connect(function() assert(not pcall(function() Instance.new('FrameSurface') end)); assert(not pcall(function() S:Clone() end)) end)");
    Check(Stats(Runtime).Pending == 1 && Stats(Runtime).InFlight == 1, "shutdown starts with pending and active resources");
    Lui_Destroy(Runtime);
    Check(RetiredErrors == 0, "surface shutdown never enters retired Luau or admits replacement content");
    Runtime = Lui_Create(); Run(Runtime, Setup); // Reuses the same surface ID and initial epoch.
    auto Retired = Stats(Runtime);
    Check(Retired.Pending == 0 && Retired.Frames == 1 && Retired.PixelBytes == 4 && Retired.Surfaces == 2, "shutdown drops pending while charging late native ownership");
    std::thread Worker([&] { Lui_FrameComplete(Value.Leases[0], 1); Value.Release(); }); Worker.join();
    Lui_Pump(Runtime); Run(Runtime, "assert(S.FrameState=='Empty'); S:Destroy()");
    Check(Stats(Runtime).Frames == 0 && Stats(Runtime).Surfaces == 0, "shutdown late release cannot affect replacement VM");
    Lui_Destroy(Runtime);
}
void AllocationAndStagingBounds() {
    Adapter Value; Value.Synchronous = true;
    auto* Runtime = Lui_Create(); Install(Runtime, Value); Run(Runtime, Setup);
    Run(Runtime, "Put(1)"); Lui_Pump(Runtime);
    Value.Synchronous = false;
    Run(Runtime, "Put(2)"); Lui_Pump(Runtime);
    Run(Runtime, "Put(3); Put(4)");
    auto Before = Stats(Runtime);
    Check(Before.Frames == 3 && Before.Pending == 1 && Before.InFlight == 1, "three-allocation surface ceiling");
    Run(Runtime, "local Ok,Code=S:SubmitFrame(buffer.create(8),2,1,8,'BGRA8Premultiplied'); assert(not Ok and Code=='WouldBlock' and S.FrameState=='Pending')");
    Check(Stats(Runtime).Frames == 3 && Stats(Runtime).Accepted == Before.Accepted, "growing replacement remains atomic at ceiling");
    Run(Runtime, "local Closed=false; S.Destroying:Connect(function() local Ok,Code=S:SubmitFrame(B,1,1,4,'BGRA8Premultiplied'); Closed=not Ok and Code=='Closed' end); S:Destroy(); assert(Closed)");
    Value.Release(); Check(Stats(Runtime).PixelBytes == 0, "surface ceiling cleanup"); Lui_Destroy(Runtime);
    Value.Synchronous = true; Value.Staging = true;
    Runtime = Lui_Create(); Install(Runtime, Value); Run(Runtime, Setup);
    Run(Runtime, "T={S}; P=buffer.create(1920*1080*4); for I=2,4 do T[I]=Instance.new('FrameSurface',{Parent=W}) end; for _,V in T do assert(V:SubmitFrame(P,1920,1080,7680,'BGRA8Premultiplied')) end");
    Lui_Pump(Runtime); Before = Stats(Runtime);
    Check(Before.PixelBytes == Before.StagingBytes && Before.PixelBytes + Before.StagingBytes <= 64*1024*1024, "native staging participates in domain budget");
    Run(Runtime, "local Ok,Code=S:SubmitFrame(P,1920,1080,7680,'BGRA8Premultiplied'); assert(not Ok and Code=='WouldBlock'); for _,V in T do V:Destroy() end");
    Check(Stats(Runtime).StagingBytes == 0, "staging released after final lease"); Lui_Destroy(Runtime);
    Value.Staging = false; Value.FailPublish = true;
    Runtime = Lui_Create(); Install(Runtime, Value); Run(Runtime, Setup);
    Run(Runtime, "Put(1)"); Lui_Pump(Runtime); Run(Runtime, "assert(S.FrameState=='Failed')");
    Value.FailPublish = false; Value.Fail = true;
    Run(Runtime, "Put(2)"); Lui_Pump(Runtime); Run(Runtime, "assert(S.FrameState=='Failed')");
    Value.Fail = false;
    Run(Runtime, "Put(3)"); Lui_Pump(Runtime); Run(Runtime, "assert(S.FrameState=='Ready'); S:Destroy()");
    Lui_Destroy(Runtime);
    Runtime = Lui_Create(); Run(Runtime, Setup);
    for (auto& Pair : Runtime->Nodes) if (Pair.second->ClassName == "FrameSurface") Pair.second->FrameSequence = std::numeric_limits<uint64_t>::max();
    Run(Runtime, "local Ok,Code=S:SubmitFrame(B,1,1,4,'BGRA8Premultiplied'); assert(not Ok and Code=='LimitExceeded' and S.FrameState=='Empty')");
    for (auto& Pair : Runtime->Nodes) if (Pair.second->ClassName == "FrameSurface") {
        Pair.second->FrameSequence = 0; Pair.second->FrameEpoch = std::numeric_limits<uint64_t>::max();
    }
    Run(Runtime, "S:ClearFrame(); local Ok,Code=S:SubmitFrame(B,1,1,4,'BGRA8Premultiplied'); assert(not Ok and Code=='LimitExceeded'); S:Destroy()");
    Lui_Destroy(Runtime);
}
uint64_t ProcessResidentBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS Value{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &Value, sizeof(Value)) ? Value.WorkingSetSize : 0;
#else
    uint64_t Total = 0, Resident = 0;
    std::ifstream File("/proc/self/statm"); File >> Total >> Resident;
    return Resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
#endif
}
uint64_t ProcessPeakBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS Value{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &Value, sizeof(Value)) ? Value.PeakWorkingSetSize : 0;
#else
    rusage Value{}; return getrusage(RUSAGE_SELF, &Value) == 0 ? uint64_t(Value.ru_maxrss) * 1024 : 0;
#endif
}
void Performance(bool Paced = false) {
    auto* Runtime = Lui_Create(); Run(Runtime, "S=Instance.new('FrameSurface'); B=buffer.create(1280*720*4)");
    const auto Baseline = ProcessResidentBytes();
    const auto WorkloadStart = std::chrono::steady_clock::now();
    std::vector<double> Copies, Ready;
    for (int Index = 0; Index < 1000; ++Index) {
        if (Paced) std::this_thread::sleep_until(WorkloadStart + std::chrono::microseconds(int64_t(Index) * 1000000 / 30));
        const auto Start = std::chrono::steady_clock::now();
        Run(Runtime, "assert(S:SubmitFrame(B,1280,720,5120,'BGRA8Premultiplied'))");
        const auto Copied = std::chrono::steady_clock::now(); Lui_Pump(Runtime);
        Copies.push_back(std::chrono::duration<double,std::milli>(Copied-Start).count());
        Ready.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Start).count());
    }
    std::sort(Copies.begin(),Copies.end()); std::sort(Ready.begin(),Ready.end());
    const auto Value = Stats(Runtime);
    std::printf("[LUI:FramePerformance] samples=1000 copies=1000 paced=%d admission_ms median=%.4f p95=%.4f p99=%.4f max=%.4f shared_ready_ms median=%.4f p95=%.4f p99=%.4f max=%.4f baseline_process_bytes=%llu peak_process_bytes=%llu frame_peak_bytes=%llu replaced=%llu\n",
        Paced ? 1 : 0, Copies[499],Copies[949],Copies[989],Copies[999],Ready[499],Ready[949],Ready[989],Ready[999],
        static_cast<unsigned long long>(Baseline),static_cast<unsigned long long>(ProcessPeakBytes()),
        static_cast<unsigned long long>(Value.PeakBytes),static_cast<unsigned long long>(Value.Replaced));
    Check(Copies[949] <= 8 && Copies[989] <= 16 && Ready[949] <= 50 && Ready[989] <= 100, "published shared performance budgets");
    Check(Value.Accepted == 1000 && Value.Consumed == 1000 && Value.Frames == 1 && Value.Pending == 0, "performance resource bounds");
    Run(Runtime, "S:Destroy()"); Check(Stats(Runtime).PixelBytes == 0, "performance quiescence"); Lui_Destroy(Runtime);
}
}
int main(int Count, char** Arguments) {
    if (Count == 2 && std::string(Arguments[1]) == "--paced") Performance(true);
    else { ApiAndValidation(); OwnershipAndRaces(); BackpressureAndVisibility(); SaturationAndPrivacy(); Unsupported(); AllocationAndStagingBounds(); Overproduction(); ShutdownWithPending(); Performance(); }
    if (!Failures) std::puts("[LUI:FrameTest] API, validation, ownership, scheduling, retirement, quotas and privacy passed");
    return Failures ? 1 : 0;
}
