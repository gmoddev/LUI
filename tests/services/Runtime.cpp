#include "LuiNativeServices.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace {
int Failures = 0;
void Check(bool Condition, const char* Message) {
    if (!Condition) { ++Failures; std::fprintf(stderr, "[LUI:NativeRuntimeTest] %s\n", Message); }
}
struct Context { LuiNativeHostV2 Host{sizeof(LuiNativeHostV2), 2}; int Starts = 0, Stops = 0, Destroyed = 0; LuiNativeIdV2 LastRequest{}; };
void LUI_CALL Start(void* Pointer, LuiNativeIdV2 Request, uint32_t Method, LuiNativeIdV2 Input, LuiNativeIdV2) {
    auto& Value = *static_cast<Context*>(Pointer); ++Value.Starts; Value.LastRequest = Request;
    if (Method == 1) return;
    Value.Host.Emit(Value.Host.Session, Request, Input);
    Value.Host.Complete(Value.Host.Session, Request, 0, Input, {});
}
void LUI_CALL Cancel(void*, LuiNativeIdV2) {}
void LUI_CALL Close(void*, LuiNativeIdV2, uint32_t) {}
void LUI_CALL Stop(void* Pointer) { ++static_cast<Context*>(Pointer)->Stops; }
uint32_t LUI_CALL Poll(void*) { return 1; }
void LUI_CALL Destroy(void* Pointer) { ++static_cast<Context*>(Pointer)->Destroyed; }
const LuiNativeMethodV2 Methods[] = {
    {sizeof(LuiNativeMethodV2), 2, "Echo", 1024, 1024, 0, 0, 30000, 0},
    {sizeof(LuiNativeMethodV2), 2, "Hold", 1024, 1024, 0, 0, 30000, 0}
};
const LuiNativeServiceV2 Service{sizeof(LuiNativeServiceV2), 2, "OwnerProof", Methods, 2, 0, LUI_CAPABILITY_HOST_SERVICES, 1, 0};
const LuiNativeProviderV2 Provider{sizeof(LuiNativeProviderV2), 2, Start, Cancel, Close, Stop, Poll, Destroy};
LuiRuntime* Create(LuiNativeDomainV2* Domain) {
    LuiDiagnosticPolicyV1 Policy{sizeof(Policy), 1, LUI_DIAGNOSTICS_DEVELOPMENT, 0};
    auto* Runtime = Lui_CreateWithPolicy(&Policy);
    Check(Runtime && Lui_SetNativeDomainV2(Runtime, Domain) == 0, "runtime attaches persistent domain");
    LuiCapabilityDeclarationV1 Grants{sizeof(Grants), 1, LUI_CAPABILITY_HOST_SERVICES | LUI_CAPABILITY_NATIVE_EXTENSIONS};
    Check(Lui_DeclareCapabilities(Runtime, &Grants) == 1, "owner grants native service access"); return Runtime;
}
void Run(LuiRuntime* Runtime, const char* Source) {
    const int Status = Lui_RunScript(Runtime, Source, "NativeServiceTest");
    if (!Status) std::fprintf(stderr, "[LUI:NativeRuntimeTest] %s\n", Lui_GetLastError(Runtime));
    Check(Status == 1, "Luau native service contract");
}
void OwnerProof() {
    auto* Domain = Lui_CreateNativeDomainV2(); auto* Runtime = Create(Domain); Context Value;
    Check(Lui_RegisterNativeServiceV2(Runtime, &Service, &Provider, &Value, &Value.Host) == 0, "host service registration");
    Check(Lui_RegisterNativeServiceV2(Runtime, &Service, &Provider, &Value, &Value.Host) == LUI_NATIVE_INVALID_ARGUMENT, "duplicate registration");
    const std::string Reflection = Lui_GetExtensionSchemaJson(Runtime);
    Check(Reflection.find("OwnerProof") != std::string::npos && Reflection.find("nativeAbiVersion\":2") != std::string::npos &&
        Reflection.find("NativeRequest") != std::string::npos, "typed runtime reflection");
    Run(Runtime,
        "local S=app:GetService('OwnerProof')\n"
        "local B=buffer.fromstring('a\\000b')\n"
        "NativeDeliveries=0; NativeProgress=0\n"
        "NativeTicket=S:Echo(B,nil,function(Result,Handle,Error)\n"
        " assert(Error==nil and Handle==nil and buffer.tostring(Result)=='a\\000b')\n"
        " NativeDeliveries+=1\n"
        "end,function(Result) assert(buffer.tostring(Result)=='a\\000b'); NativeProgress+=1 end)\n"
        "buffer.writeu8(B,0,122)\n"
        "assert(NativeDeliveries==0 and NativeProgress==0)\n");
    Lui_Pump(Runtime);
    Run(Runtime, "assert(NativeDeliveries==1 and NativeProgress==1); local Won,Code=NativeTicket:Cancel(); assert(not Won and Code=='Closed')");
    Run(Runtime,
        "local S=app:GetService('OwnerProof')\n"
        "local R=S:Hold(buffer.create(0),nil,function(Result,Handle,Error) assert(Result==nil and Handle==nil and Error=='Canceled'); NativeDeliveries+=1 end)\n"
        "local Won,Code=R:Cancel(); assert(Won and Code=='Ok')\n"
        "assert(not R:Cancel())\n");
    Lui_Pump(Runtime); Run(Runtime, "assert(NativeDeliveries==2)");
    Run(Runtime,
        "app:GetService('OwnerProof'):Echo(buffer.fromstring('SYNTHETIC_NATIVE_PRIVATE_CANARY'),nil,function() error('SYNTHETIC_NATIVE_PRIVATE_CANARY') end)\n");
    Lui_Pump(Runtime);
    Check(std::string(Lui_GetDiagnosticsJson(Runtime)).find("SYNTHETIC_NATIVE_PRIVATE_CANARY") == std::string::npos,
        "callback error payload absent from diagnostics");
    Run(Runtime,
        "local S=app:GetService('OwnerProof'); for Index=1,32 do S:Hold(buffer.create(0),nil,function() error('retired callback') end) end\n"
        "assert(not pcall(function() S:Hold(buffer.create(0),nil,function() end) end))\n");
    Lui_Destroy(Runtime);
    Lui_PumpNativeDomainV2(Domain);
    LuiNativeStatsV2 Stats{sizeof(Stats), 2}; Lui_GetNativeStatsV2(Domain, &Stats);
    Check(Value.Stops == 1 && Stats.Requests == 0 && Stats.Sessions == 0 && Stats.Bytes == 0, "retired VM cleanup without callbacks");
    // A new runtime reuses exactly the same host-owned domain.
    Runtime = Create(Domain); Context Replacement;
    Check(Lui_RegisterNativeServiceV2(Runtime, &Service, &Provider, &Replacement, &Replacement.Host) == 0, "replacement generation admission");
    Lui_Destroy(Runtime); Lui_ReleaseNativeDomainV2(Domain);
}
void CrossProviderOrdering(bool WorkerSettlement) {
    auto* Domain = Lui_CreateNativeDomainV2(); auto* Runtime = Create(Domain); Context A,B;
    auto Peer = Service; Peer.Name = "PeerProof";
    Check(Lui_RegisterNativeServiceV2(Runtime,&Service,&Provider,&A,&A.Host) == 0 &&
        Lui_RegisterNativeServiceV2(Runtime,&Peer,&Provider,&B,&B.Host) == 0,"two ordered bindings");
    Run(Runtime,
        "DeliveryOrder=''; A=app:GetService('OwnerProof'):Hold(buffer.create(0),nil,function(_,_,Failure) assert(Failure=='Canceled'); DeliveryOrder..='A' end,function() DeliveryOrder..='X' end)\n"
        "B=app:GetService('PeerProof'):Hold(buffer.create(0),nil,function() error('retired waiter') end,function() DeliveryOrder..='P' end)");
    for (auto* Value : {&A,&B}) {
        LuiNativeIdV2 Bytes{};
        Check(Value->Host.CopyBytes(Value->Host.Session,{},nullptr,0,&Bytes) == 0,"ordered progress bytes");
        Check(Value->Host.Emit(Value->Host.Session,Value->LastRequest,Bytes) == 0,"ordered progress admission");
        Value->Host.ReleaseBytes(Value->Host.Session,Bytes);
    }
    if (WorkerSettlement) {
        uint32_t Status = 99;
        Check(A.Host.RetainSession(A.Host.Session) == 0,"ordered worker lease");
        std::thread Worker([&] { Status = A.Host.Complete(A.Host.Session,A.LastRequest,LUI_NATIVE_CANCELED,{},{}); A.Host.ReleaseSession(A.Host.Session); });
        Worker.join(); Check(Status == 0,"worker settles before ordered owner dequeue");
    } else Run(Runtime,"assert(A:Cancel())");
    Lui_Pump(Runtime);
    Run(Runtime,"assert(DeliveryOrder=='PA','suppressed progress inverted cross-provider order')");
    Lui_Destroy(Runtime); Lui_PumpNativeDomainV2(Domain);
    LuiNativeStatsV2 Stats{sizeof(Stats),2}; Lui_GetNativeStatsV2(Domain,&Stats);
    Check(!Stats.Sessions && !Stats.Events && !Stats.Requests && !Stats.Bytes,"ordered bindings reclaimed");
    Lui_ReleaseNativeDomainV2(Domain);
}
void HostRetirement() {
    auto* Domain = Lui_CreateNativeDomainV2(); auto* Runtime = Create(Domain); Context Value;
    Check(Lui_RegisterNativeServiceV2(Runtime,&Service,&Provider,&Value,&Value.Host) == 0,"retirement fixture registration");
    Run(Runtime,"RetiredTicket=app:GetService('OwnerProof'):Hold(buffer.create(0),nil,function() error('retired callback') end)");
    Check(Lui_RetireNativeSessionV2(Value.Host.Session) == 0,"host retires a live runtime binding");
    Lui_PumpNativeDomainV2(Domain);
    // Fail without dereferencing the old pointer if the regression reproduces.
    Check(Value.Destroyed == 0,"session reclaimed before binding/VM teardown");
    if (Value.Destroyed) return;
    LuiNativeStatsV2 Stats{sizeof(Stats),2}; Lui_GetNativeStatsV2(Domain,&Stats);
    Check(Stats.Sessions == 1 && Stats.RetiringSessions == 1 && Stats.Leases == 1,"binding lease charges retired session");
    Lui_Pump(Runtime);
    Run(Runtime,"local Won,Code=RetiredTicket:Cancel(); assert(not Won and Code=='Closed'); local Ok,Failure=pcall(function() app:GetService('OwnerProof'):Hold(buffer.create(0),nil,function() end) end); assert(not Ok and string.find(Failure,'Closed',1,true))");
    Check(std::string(Lui_GetExtensionSchemaJson(Runtime)).find("OwnerProof") != std::string::npos,"retired binding metadata remains valid");
    Lui_Destroy(Runtime); Lui_PumpNativeDomainV2(Domain); Lui_GetNativeStatsV2(Domain,&Stats);
    Check(Value.Destroyed == 1 && Stats.Sessions == 0 && Stats.Leases == 0 && Stats.Requests == 0,"VM teardown releases binding lease");
    Lui_ReleaseNativeDomainV2(Domain);
}
void RustProof(const char* Path) {
    auto* Domain = Lui_CreateNativeDomainV2(); auto* Runtime = Create(Domain);
    Check(Lui_LoadNativeServiceV2(Runtime, Path) == 0, "independent Rust library loads through ABI v2");
    Run(Runtime,
        "local S=app:GetService('RustProof'); RustDone=0; RustCanceled=0; RustProgress=0; FrameDone=0; RetiredFrameDone=0\n"
        "Surface=Instance.new('FrameSurface')\n"
        "S:Frame(buffer.create(0),nil,function(Result,Handle,Error)\n"
        " assert(not Error and not Handle and buffer.len(Result)==16 and buffer.readu8(Result,0)==11)\n"
        " assert(Surface:SubmitFrame(Result,2,2,8,'BGRA8Premultiplied')); buffer.writeu8(Result,0,200); FrameDone+=1\n"
        "end)\n"
        "local Retired=Instance.new('FrameSurface')\n"
        "S:Frame(buffer.create(0),nil,function(Result,Handle,Error)\n"
        " assert(not Error and not Handle); local Ok,Code=Retired:SubmitFrame(Result,2,2,8,'BGRA8Premultiplied')\n"
        " assert(not Ok and Code=='Closed'); RetiredFrameDone+=1\n"
        "end); Retired:Destroy()\n"
        "local B=buffer.fromstring('rust\\000bytes')\n"
        "local R=S:Hold(B,nil,function(Result,Handle,Error) assert(Error=='Canceled'); RustCanceled+=1 end)\n"
        "assert(R:Cancel())\n"
        "S:Progress(B,nil,function(Result,Handle,Error) assert(not Error and buffer.tostring(Result)=='rust\\000bytes'); RustDone+=1 end,\n"
        " function(Result) assert(buffer.tostring(Result)=='rust\\000bytes'); RustProgress+=1 end)\n"
        "S:Counter(buffer.create(0),nil,function(Result,Handle,Error)\n"
        " assert(not Error and Handle and buffer.readu32(Result,0)==0)\n"
        " S:Increment(buffer.create(0),Handle,function(Next,NoHandle,Failure)\n"
        "  assert(not Failure and not NoHandle and buffer.readu32(Next,0)==1)\n"
        "  assert(Handle:Close() and Handle:Close()); RustDone+=1\n"
        " end)\n"
        "end)\n"
        "S:Fail(buffer.create(0),nil,function(Result,Handle,Error) assert(Error=='ProviderFailure'); RustDone+=1 end)\n"
        "buffer.writeu8(B,0,122)\n");
    const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool Done = false;
    while (std::chrono::steady_clock::now() < Until) {
        Lui_Pump(Runtime);
        LuiNativeStatsV2 Pending{sizeof(Pending), 2}; Lui_GetNativeStatsV2(Domain, &Pending);
        if (!Pending.Requests) { Done = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(Done, "Rust bytes/handle/progress/cancel/failure interoperability");
    Run(Runtime, "assert(RustDone==3 and RustCanceled==1 and RustProgress==1 and FrameDone==1 and RetiredFrameDone==1 and Surface.FrameState=='Ready'); Surface:Destroy()");
    Run(Runtime, "app:GetService('RustProof'):Hold(buffer.create(0),nil,function() error('late callback') end)");
    Lui_Destroy(Runtime);
    LuiNativeStatsV2 Stats{sizeof(Stats), 2};
    while (std::chrono::steady_clock::now() < Until) {
        Lui_PumpNativeDomainV2(Domain); Lui_GetNativeStatsV2(Domain, &Stats);
        if (!Stats.Sessions) break;
        std::this_thread::yield();
    }
    Check(Stats.Sessions == 0 && Stats.Bytes == 0 && Stats.Leases == 0, "Rust workers join and unload after retirement");
    Lui_ReleaseNativeDomainV2(Domain);
}
void RustQuarantine(const char* Path) {
    auto* Domain = Lui_CreateNativeDomainV2();
    for (int Generation = 0; Generation < 4; ++Generation) {
        auto* Runtime = Create(Domain);
        Check(Lui_LoadNativeServiceV2(Runtime, Path) == 0, "quarantine fixture load");
        Run(Runtime, "app:GetService('RustProof'):StallShutdown(buffer.create(0),nil,function() error('retired delivery') end)");
        Lui_Destroy(Runtime);
    }
    auto* Runtime = Create(Domain);
    Check(Lui_LoadNativeServiceV2(Runtime, Path) == LUI_NATIVE_LIMIT_EXCEEDED, "recreating runtime escaped retirement admission cap");
    Lui_Destroy(Runtime);
    LuiNativeStatsV2 Stats{sizeof(Stats), 2}; Lui_GetNativeStatsV2(Domain, &Stats);
    Check(Stats.Sessions == 4 && Stats.RetiringSessions == 4 && Stats.Requests == 0 && Stats.Leases == 0 && Stats.Bytes == 0,
        "stuck provider charges remain bounded without late delivery");
    // This isolated process intentionally retains exactly four nonquiescent
    // images. OS process exit reclaims them; no worker is left running.
    Lui_ReleaseNativeDomainV2(Domain);
}
}
int main(int Count, char** Arguments) {
    if (Count == 3 && std::string(Arguments[1]) == "--quarantine") RustQuarantine(Arguments[2]);
    else { OwnerProof(); HostRetirement(); CrossProviderOrdering(false); CrossProviderOrdering(true); if (Count == 2) RustProof(Arguments[1]); }
    if (!Failures) std::puts("[LUI:NativeRuntimeTest] owner delivery, managed handles, redaction and generation cleanup passed");
    return Failures ? 1 : 0;
}
