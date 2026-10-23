#include "LuiNativeServices.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {
int Failures = 0;
void Check(bool Value, const char* Message) {
    if (!Value) { ++Failures; std::fprintf(stderr, "[LUI:NativeServiceTest] %s\n", Message); }
}
constexpr LuiNativeIdV2 None{};
struct Fixture {
    LuiNativeHostV2 Host{sizeof(LuiNativeHostV2), 2};
    int Started = 0, Canceled = 0, Closed = 0, Shutdown = 0, Destroyed = 0;
    bool Stuck = false;
    std::thread::id Owner = std::this_thread::get_id();
};
void LUI_CALL Start(void* Context, LuiNativeIdV2 Request, uint32_t Method, LuiNativeIdV2 Input, LuiNativeIdV2) {
    auto& Value = *static_cast<Fixture*>(Context); ++Value.Started;
    Check(std::this_thread::get_id() == Value.Owner, "start left owner scheduler");
    if (Method == 1) {
        const void* Bytes = nullptr; uint64_t Length = 0;
        Check(Value.Host.ViewBytes(Value.Host.Session, Input, &Bytes, &Length) == 0, "start input view");
        LuiNativeIdV2 Result{};
        Check(Value.Host.CopyBytes(Value.Host.Session, Request, Bytes, Length, &Result) == 0, "reserved result copy");
        Check(Value.Host.Complete(Value.Host.Session, Request, 0, Result, None) == 0, "inline settlement");
        Check(Value.Host.ReleaseBytes(Value.Host.Session, Result) == 0, "producer release after complete");
    }
}
void LUI_CALL Cancel(void* Context, LuiNativeIdV2) {
    auto& Value = *static_cast<Fixture*>(Context); ++Value.Canceled;
    Check(std::this_thread::get_id() == Value.Owner, "cancel left owner scheduler");
}
void LUI_CALL Close(void* Context, LuiNativeIdV2, uint32_t Kind) {
    auto& Value = *static_cast<Fixture*>(Context); ++Value.Closed;
    Check(Kind == 1 && std::this_thread::get_id() == Value.Owner, "typed owner close");
}
void LUI_CALL Shutdown(void* Context) { ++static_cast<Fixture*>(Context)->Shutdown; }
uint32_t LUI_CALL Poll(void* Context) { return !static_cast<Fixture*>(Context)->Stuck; }
void LUI_CALL Destroy(void* Context) { ++static_cast<Fixture*>(Context)->Destroyed; }
const LuiNativeMethodV2 Methods[] = {
    {sizeof(LuiNativeMethodV2), 2, "Hold", 8*1024*1024, 64, 0, 0, 300000, 0},
    {sizeof(LuiNativeMethodV2), 2, "Echo", 8*1024*1024, 8*1024*1024, 0, 0, 300000, 0},
    {sizeof(LuiNativeMethodV2), 2, "Handle", 0, 0, 0, 1, 300000, 0},
    {sizeof(LuiNativeMethodV2), 2, "Use", 0, 0, 1, 0, 300000, 0},
    {sizeof(LuiNativeMethodV2), 2, "Expiry", 0, 0, 0, 0, 1, 0}
};
const LuiNativeServiceV2 Service{sizeof(LuiNativeServiceV2), 2, "Synthetic", Methods, 5, 1, 0, 1, 0};
const LuiNativeProviderV2 Provider{sizeof(LuiNativeProviderV2), 2, Start, Cancel, Close, Shutdown, Poll, Destroy};
void Open(LuiNativeDomainV2* Domain, Fixture& Value) {
    Check(Lui_OpenNativeSessionV2(Domain, &Service, &Provider, &Value, &Value.Host) == 0, "open session");
}
LuiNativeStatsV2 Stats(LuiNativeDomainV2* Domain) {
    LuiNativeStatsV2 Value{sizeof(Value), 2};
    Check(Lui_GetNativeStatsV2(Domain, &Value) == 0, "stats query"); return Value;
}
LuiNativeIdV2 Bytes(Fixture& Value, const void* Data = nullptr, uint64_t Length = 0) {
    LuiNativeIdV2 Result{};
    Check(Value.Host.CopyBytes(Value.Host.Session, None, Data, Length, &Result) == 0, "copy input bytes"); return Result;
}
LuiNativeIdV2 Begin(Fixture& Value, LuiNativeIdV2 Input, uint32_t Method = 0, LuiNativeIdV2 Handle = {}) {
    LuiNativeIdV2 Result{};
    Check(Lui_StartNativeRequestV2(Value.Host.Session, Method, Input, Handle, &Result) == 0, "admit request"); return Result;
}
LuiNativeDeliveryV2 Take(Fixture& Value) {
    LuiNativeDeliveryV2 Result{sizeof(Result), 2};
    Check(Lui_NextNativeDeliveryV2(Value.Host.Session, &Result) == 0, "take delivery"); return Result;
}
void Retire(LuiNativeDomainV2* Domain, Fixture& Value) {
    Check(Lui_RetireNativeSessionV2(Value.Host.Session) == 0, "retire session");
    Check(Lui_PumpNativeDomainV2(Domain) == 0, "pump retirement");
}
void Ownership() {
    auto* Domain = Lui_CreateNativeDomainV2(); Fixture A, B; Open(Domain, A); Open(Domain, B);
    char Input[] = {'x', 0, 'y'}; const auto Buffer = Bytes(A, Input, sizeof(Input)); Input[0] = 'z';
    const void* View = nullptr; uint64_t Length = 0;
    Check(A.Host.ViewBytes(A.Host.Session, Buffer, &View, &Length) == 0 && Length == 3 && std::memcmp(View, "x\0y", 3) == 0, "owned binary copy");
    Check(B.Host.ReleaseBytes(B.Host.Session, Buffer) == LUI_NATIVE_INVALID_ARGUMENT, "foreign buffer release");
    Check(A.Host.RetainBytes(A.Host.Session, Buffer) == 0, "retain buffer");
    Check(A.Host.ReleaseBytes(A.Host.Session, Buffer) == 0 && Stats(Domain).Bytes == 3, "retained charge survives release");
    const auto Request = Begin(A, Buffer, 1);
    Check(Stats(Domain).Requests == 1 && Stats(Domain).ReservedBytes == 0, "inline completion deferred and reservation freed");
    Check(Lui_CancelNativeRequestV2(A.Host.Session, Request) == LUI_NATIVE_ALREADY_SETTLED, "cancel rewrote completion");
    const auto Result = Take(A);
    Check(Result.Kind == LUI_NATIVE_TERMINAL && Result.Status == 0 && Stats(Domain).Requests == 0, "one terminal delivery");
    Check(A.Host.ViewBytes(A.Host.Session, Result.Buffer, &View, &Length) == 0 && std::memcmp(View, "x\0y", 3) == 0, "result lifetime");
    A.Host.ReleaseBytes(A.Host.Session, Result.Buffer); A.Host.ReleaseBytes(A.Host.Session, Buffer);
    Check(A.Host.ReleaseBytes(A.Host.Session, Buffer) == LUI_NATIVE_INVALID_ARGUMENT, "stale buffer release");
    LuiNativeIdV2 Handle{}; Check(A.Host.CreateHandle(A.Host.Session, 1, &Handle) == 0, "create handle");
    Check(B.Host.RetainHandle(B.Host.Session, Handle) == LUI_NATIVE_INVALID_ARGUMENT, "foreign handle");
    Check(Lui_CloseNativeHandleV2(A.Host.Session, Handle) == 0 && Lui_CloseNativeHandleV2(A.Host.Session, Handle) == 0, "idempotent handle close");
    Check(A.Host.RetainHandle(A.Host.Session, Handle) == LUI_NATIVE_CLOSED, "closed handle retain");
    Lui_PumpNativeDomainV2(Domain); Lui_PumpNativeDomainV2(Domain); Check(A.Closed == 1, "exactly one close callback");
    A.Host.ReleaseHandle(A.Host.Session, Handle); Lui_PumpNativeDomainV2(Domain);
    Check(Stats(Domain).Bytes == 0 && Stats(Domain).Handles == 0, "owned records reclaimed");
    Retire(Domain, A); Retire(Domain, B);
    Check(Stats(Domain).Sessions == 0 && A.Destroyed == 1 && B.Destroyed == 1, "session final cleanup");
    Lui_ReleaseNativeDomainV2(Domain);
}
void Saturation() {
    auto* Domain = Lui_CreateNativeDomainV2(); Fixture Value; Open(Domain, Value);
    const auto Input = Bytes(Value); std::vector<LuiNativeIdV2> Requests;
    for (int Index = 0; Index < 32; ++Index) Requests.push_back(Begin(Value, Input));
    LuiNativeIdV2 Refused{};
    Check(Lui_StartNativeRequestV2(Value.Host.Session, 0, Input, None, &Refused) == LUI_NATIVE_LIMIT_EXCEEDED && Value.Started == 32, "request refusal starts no operation");
    const auto Event = Bytes(Value, "event", 5);
    for (int Index = 0; Index < 64; ++Index) Check(Value.Host.Emit(Value.Host.Session, Requests[0], Event) == 0, "event admission");
    Check(Value.Host.Emit(Value.Host.Session, Requests[0], Event) == LUI_NATIVE_WOULD_BLOCK, "event backpressure");
    Check(Lui_CancelNativeRequestV2(Value.Host.Session, Requests[0]) == 0, "cancel while progress full");
    Check(Value.Host.Complete(Value.Host.Session, Requests[0], 0, None, None) == LUI_NATIVE_ALREADY_SETTLED, "late completion after cancel");
    Lui_PumpNativeDomainV2(Domain);
    const auto Result = Take(Value);
    Check(Result.Status == LUI_NATIVE_CANCELED && Value.Canceled == 1 && Stats(Domain).Events == 0, "cancel suppresses progress without terminal starvation");
    for (size_t Index = 1; Index < Requests.size(); ++Index) Value.Host.Complete(Value.Host.Session, Requests[Index], 0, None, None);
    for (int Index = 1; Index < 32; ++Index) Take(Value);
    Value.Host.ReleaseBytes(Value.Host.Session, Event); Value.Host.ReleaseBytes(Value.Host.Session, Input);
    std::vector<unsigned char> Maximum(8*1024*1024, 0xA5);
    const auto Large = Bytes(Value, Maximum.data(), Maximum.size());
    const auto Other = Bytes(Value, Maximum.data(), Maximum.size());
    Check(Value.Host.CopyBytes(Value.Host.Session, None, "x", 1, &Refused) == LUI_NATIVE_LIMIT_EXCEEDED, "provider byte ceiling");
    Check(Value.Host.CopyBytes(Value.Host.Session, None, Maximum.data(), Maximum.size()+1, &Refused) == LUI_NATIVE_LIMIT_EXCEEDED, "oversized value");
    Check(Lui_StartNativeRequestV2(Value.Host.Session, 1, Large, None, &Refused) == LUI_NATIVE_LIMIT_EXCEEDED, "result reservation before start");
    Value.Host.ReleaseBytes(Value.Host.Session, Other);
    Begin(Value, Large, 1); const auto Echo = Take(Value);
    Check(Stats(Domain).Bytes == 16*1024*1024 && Stats(Domain).ReservedBytes == 0, "reservation converts without double charge");
    Value.Host.ReleaseBytes(Value.Host.Session, Echo.Buffer); Value.Host.ReleaseBytes(Value.Host.Session, Large);
    // Even empty values have a bounded registry footprint.
    std::vector<LuiNativeIdV2> EmptyValues;
    for (int Index = 0; Index < 256; ++Index) EmptyValues.push_back(Bytes(Value));
    Check(Value.Host.CopyBytes(Value.Host.Session, None, nullptr, 0, &Refused) == LUI_NATIVE_LIMIT_EXCEEDED, "zero-byte registry bound");
    for (const auto Id : EmptyValues) Value.Host.ReleaseBytes(Value.Host.Session, Id);
    Retire(Domain, Value); Check(Stats(Domain).Sessions == 0, "saturation cleanup"); Lui_ReleaseNativeDomainV2(Domain);
}
void RacesAndRetirement() {
    auto* Domain = Lui_CreateNativeDomainV2(); Fixture Value; Open(Domain, Value);
    const auto Input = Bytes(Value); const auto Request = Begin(Value, Input);
    const auto Failed = Begin(Value, Input);
    const auto Event = Bytes(Value,"progress",8);
    Check(Value.Host.Emit(Value.Host.Session,Failed,Event) == 0,"progress before failure");
    Value.Host.ReleaseBytes(Value.Host.Session,Event);
    Check(Value.Host.Complete(Value.Host.Session,Failed,LUI_NATIVE_PROVIDER_FAILURE,None,None) == 0,"provider failure settlement");
    const auto Progress = Take(Value); const auto Failure = Take(Value);
    Check(Progress.Kind == LUI_NATIVE_PROGRESS && Failure.Status == LUI_NATIVE_PROVIDER_FAILURE && Progress.Sequence < Failure.Sequence,"accepted progress ordered before provider failure");
    Value.Host.ReleaseBytes(Value.Host.Session,Progress.Buffer);
    Check(Value.Host.RetainSession(Value.Host.Session) == 0 && Value.Host.RetainBytes(Value.Host.Session, Input) == 0, "worker retains before start returns");
    std::mutex Mutex; std::condition_variable Ready; bool MayComplete = false;
    uint32_t Outcome = 99;
    std::thread Worker([&] {
        { std::unique_lock<std::mutex> Lock(Mutex); Ready.wait(Lock, [&] { return MayComplete; }); }
        Outcome = Value.Host.Complete(Value.Host.Session, Request, 0, None, None);
        Value.Host.ReleaseBytes(Value.Host.Session, Input); Value.Host.ReleaseSession(Value.Host.Session);
    });
    Check(Lui_CancelNativeRequestV2(Value.Host.Session, Request) == 0, "cancel wins forced race");
    { std::lock_guard<std::mutex> Lock(Mutex); MayComplete = true; } Ready.notify_one(); Worker.join();
    Check(Outcome == LUI_NATIVE_ALREADY_SETTLED, "worker observes first settlement");
    Lui_PumpNativeDomainV2(Domain); Check(Take(Value).Status == LUI_NATIVE_CANCELED, "cancel outcome delivered once");
    const auto Expiring = Begin(Value, Input, 4);
    const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    LuiNativeDeliveryV2 Expiry{sizeof(Expiry), 2}; uint32_t Status = LUI_NATIVE_WOULD_BLOCK;
    while (Status == LUI_NATIVE_WOULD_BLOCK && std::chrono::steady_clock::now() < Until) {
        Lui_PumpNativeDomainV2(Domain); Status = Lui_NextNativeDeliveryV2(Value.Host.Session, &Expiry); std::this_thread::yield();
    }
    Check(Status == 0 && Expiry.Request.Value == Expiring.Value && Expiry.Status == LUI_NATIVE_DEADLINE_EXCEEDED, "bounded deadline settlement");
    const auto Late = Begin(Value, Input);
    Value.Host.RetainSession(Value.Host.Session); Value.Host.ReleaseBytes(Value.Host.Session, Input);
    Retire(Domain, Value);
    Check(Value.Host.Complete(Value.Host.Session, Late, 0, None, None) == LUI_NATIVE_CLOSED && Stats(Domain).RetiringSessions == 1, "retirement suppresses dead waiter");
    Check(Value.Destroyed == 0, "session image cleanup before worker lease drained");
    Value.Host.ReleaseSession(Value.Host.Session); Lui_PumpNativeDomainV2(Domain);
    Check(Stats(Domain).Sessions == 0 && Value.Destroyed == 1, "owner final cleanup after lease drain");
    Lui_ReleaseNativeDomainV2(Domain);
}
void Quarantine() {
    auto* Domain = Lui_CreateNativeDomainV2(); std::array<Fixture, 5> Values;
    for (int Index = 0; Index < 4; ++Index) { Values[Index].Stuck = true; Open(Domain, Values[Index]); Retire(Domain, Values[Index]); }
    Check(Lui_OpenNativeSessionV2(Domain, &Service, &Provider, &Values[4], &Values[4].Host) == LUI_NATIVE_LIMIT_EXCEEDED, "retired sessions cannot reset quota");
    Check(Stats(Domain).Sessions == 4 && Stats(Domain).RetiringSessions == 4, "quarantine charged");
    for (int Index = 0; Index < 4; ++Index) Values[Index].Stuck = false;
    Lui_PumpNativeDomainV2(Domain); Check(Stats(Domain).Sessions == 0, "cooperative quarantine recovery");
    Open(Domain, Values[4]); Retire(Domain, Values[4]); Lui_ReleaseNativeDomainV2(Domain);
}
}
int main() {
    Ownership(); Saturation(); RacesAndRetirement(); Quarantine();
    if (!Failures) std::puts("[LUI:NativeServiceTest] domain ownership, quotas, ordering, races and retirement passed");
    return Failures ? 1 : 0;
}
