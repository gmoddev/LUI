#include "LuiOwnerWake.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
std::atomic<int> Failures{0};
void Check(bool Value,const char* Code){if(!Value){++Failures;std::fprintf(stderr,"[LUI:OwnerWakeTest] %s\n",Code);}}
struct Gate {
    std::mutex Mutex;std::condition_variable Changed;bool Open=false;
    void Set(){std::lock_guard<std::mutex> Lock(Mutex);Open=true;Changed.notify_all();}
    bool Wait(){std::unique_lock<std::mutex> Lock(Mutex);return Changed.wait_for(Lock,std::chrono::seconds(5),[&]{return Open;});}
};
struct ProviderContext {
    LuiNativeHostV2 Host{sizeof(LuiNativeHostV2),2};
    std::vector<LuiNativeIdV2> Requests;
    bool Quiescent=true;int Destroyed=0;
};
void LUI_CALL Start(void* Pointer,LuiNativeIdV2 Request,uint32_t,LuiNativeIdV2,LuiNativeIdV2){static_cast<ProviderContext*>(Pointer)->Requests.push_back(Request);}
void LUI_CALL Cancel(void*,LuiNativeIdV2){}
void LUI_CALL Close(void*,LuiNativeIdV2,uint32_t){}
void LUI_CALL Stop(void*){}
uint32_t LUI_CALL Poll(void* Pointer){return static_cast<ProviderContext*>(Pointer)->Quiescent?1:0;}
void LUI_CALL Destroy(void* Pointer){++static_cast<ProviderContext*>(Pointer)->Destroyed;}
const LuiNativeMethodV2 Method{sizeof(Method),2,"Echo",0,64,0,0,30000,0};
const LuiNativeServiceV2 Service{sizeof(Service),2,"WakeProof",&Method,1,0,LUI_CAPABILITY_HOST_SERVICES,1,0};
const LuiNativeProviderV2 Provider{sizeof(Provider),2,Start,Cancel,Close,Stop,Poll,Destroy};
struct Inbox {
    std::mutex Mutex;std::condition_variable Changed;
    std::atomic<int> Calls{0},Released{0},Fail{0};std::atomic<bool> Stopped{false};
    int Pending=0,HighWater=0;Gate *Entered=nullptr,*Resume=nullptr;bool FailFirst=false;
    static uint32_t LUI_CALL Request(void* Pointer){
        auto& Value=*static_cast<Inbox*>(Pointer);int Call=++Value.Calls;
        if(Call==1&&Value.Entered){Value.Entered->Set();if(!Value.Resume->Wait())return 0;}
        if(Value.Stopped||(Value.FailFirst&&Call==1)||Value.Fail.exchange(0))return 0;
        std::lock_guard<std::mutex> Lock(Value.Mutex);++Value.Pending;
        Value.HighWater=std::max(Value.HighWater,Value.Pending);Value.Changed.notify_all();return 1;
    }
    static void LUI_CALL Release(void* Pointer){++static_cast<Inbox*>(Pointer)->Released;}
    LuiNativeWakeV1 Callbacks(){return {sizeof(LuiNativeWakeV1),1,this,Request,Release};}
    bool TakeUntil(Clock::time_point Until){std::unique_lock<std::mutex> Lock(Mutex);if(!Changed.wait_until(Lock,Until,[&]{return Pending!=0;}))return false;--Pending;return true;}
    bool Take(){return TakeUntil(Clock::now()+std::chrono::seconds(5));}
};
struct Fixture {
    LuiNativeDomainV2* Domain=Lui_CreateNativeDomainV2();LuiRuntime* Runtime=nullptr;
    ProviderContext Context;
    Fixture(){
        Runtime=Lui_Create();Check(Lui_SetNativeDomainV2(Runtime,Domain)==0,"existing explicit domain attachment");
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants),1,LUI_CAPABILITY_HOST_SERVICES};
        Check(Lui_DeclareCapabilities(Runtime,&Grants)==1,"native grant");
        Check(Lui_RegisterNativeServiceV2(Runtime,&Service,&Provider,&Context,&Context.Host)==0,"echo provider registration");
        Run("Delivered=0;Progress=0");
    }
    void Run(const char* Source){Check(Lui_RunScript(Runtime,Source,"OwnerWakeTest")==1,"owner Lua assertions");}
    void Admit(int Count=1){for(int Index=0;Index<Count;++Index)Run("Ticket=app:GetService('WakeProof'):Echo(buffer.create(0),nil,function(_,_,E) assert(not E);Delivered+=1 end,function()Progress+=1 end)");}
    uint64_t Install(Inbox& Value){auto Callbacks=Value.Callbacks();uint64_t Token=0;Check(Lui_SetNativeWakeV1(Domain,&Callbacks,&Token)==0&&Token,"wake registration");return Token;}
    void Complete(size_t Index){Check(Context.Host.Complete(Context.Host.Session,Context.Requests[Index],0,{},{})==0,"owned terminal settlement");}
    void WorkerComplete(size_t Begin,size_t End){
        Check(Context.Host.RetainSession(Context.Host.Session)==0,"worker session lease");
        std::thread Work([&]{for(auto Index=Begin;Index<End;++Index)Complete(Index);Context.Host.ReleaseSession(Context.Host.Session);});Work.join();
    }
    void Turn(Inbox& Value,uint64_t Token){Check(Value.Take(),"enqueued owner turn");Check(Lui_AcknowledgeNativeWakeV1(Domain,Token)==0,"owner acknowledgment");Lui_Pump(Runtime);}
    ~Fixture(){
        Lui_SetNativeWakeV1(Domain,nullptr,nullptr);
        if(Runtime)Lui_Destroy(Runtime);
        Lui_PumpNativeDomainV2(Domain);LuiNativeStatsV2 Stats{sizeof(Stats),2};Lui_GetNativeStatsV2(Domain,&Stats);
        Check(!Stats.Sessions&&!Stats.Requests&&!Stats.Bytes&&!Stats.Leases&&!Stats.Events,"native final quiescence");
        Lui_ReleaseNativeDomainV2(Domain);
    }
};
void Boundaries(){
    Fixture Test;Inbox Value;auto Callbacks=Value.Callbacks();uint64_t Token=0;
    Callbacks.AbiVersion=2;Check(Lui_SetNativeWakeV1(Test.Domain,&Callbacks,&Token)==LUI_NATIVE_INVALID_ARGUMENT&&!Token&&!Value.Released,"version rejection retains caller context");
    Callbacks=Value.Callbacks();--Callbacks.StructSize;Check(Lui_SetNativeWakeV1(Test.Domain,&Callbacks,&Token)==LUI_NATIVE_INVALID_ARGUMENT,"size rejection");
    Callbacks=Value.Callbacks();Callbacks.Request=nullptr;Check(Lui_SetNativeWakeV1(Test.Domain,&Callbacks,&Token)==LUI_NATIVE_INVALID_ARGUMENT,"missing notify rejected");
    Callbacks=Value.Callbacks();Callbacks.Release=nullptr;Check(Lui_SetNativeWakeV1(Test.Domain,&Callbacks,&Token)==LUI_NATIVE_INVALID_ARGUMENT,"missing context release rejected");
    Token=Test.Install(Value);Callbacks=Value.Callbacks();
    std::string Before=Lui_GetLastError(Test.Runtime);uint32_t Set=0,Ack=0;int Ready=1;LuiNativeDomainV2* Copy=nullptr;
    std::thread Worker([&]{uint64_t Other=0;Set=Lui_SetNativeWakeV1(Test.Domain,&Callbacks,&Other);Ack=Lui_AcknowledgeNativeWakeV1(Test.Domain,Token);Ready=Lui_IsOwnerPumpReadyV1(Test.Runtime);Copy=Lui_GetOwnerDomainV1(Test.Runtime);});Worker.join();
    Check(Set==LUI_NATIVE_INVALID_ARGUMENT&&Ack==LUI_NATIVE_INVALID_ARGUMENT&&!Ready&&!Copy,"worker control calls rejected");
    Check(Before==Lui_GetLastError(Test.Runtime),"worker readiness query does not mutate diagnostics");
    Copy=Lui_GetOwnerDomainV1(Test.Runtime);LuiNativeStatsV2 Stats{sizeof(Stats),2};Lui_GetNativeStatsV2(Copy,&Stats);
    Check(Stats.Sessions==1,"wrapper reuses existing quota domain");Lui_ReleaseNativeDomainV2(Copy);
    Check(Lui_AcknowledgeNativeWakeV1(Test.Domain,Token+1)==LUI_NATIVE_CLOSED,"foreign registration rejected");
    Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);
    Check(Value.Released==1,"idempotent unregister releases once");
}
void BurstAndContinuation(){
    Fixture Test;Inbox Value;auto Token=Test.Install(Value);Test.Admit(32);Test.WorkerComplete(0,32);
    Test.Run("assert(Delivered==0)");Check(Value.Calls==1&&Value.HighWater==1,"burst coalesces without inline Lua");
    Test.Turn(Value,Token);Test.Run("assert(Delivered==32)");
    Test.Admit(32);LuiNativeIdV2 Bytes{};Check(Test.Context.Host.CopyBytes(Test.Context.Host.Session,{},nullptr,0,&Bytes)==0,"owned progress allocation");
    for(int Index=0;Index<64;++Index)Check(Test.Context.Host.Emit(Test.Context.Host.Session,Test.Context.Requests[32],Bytes)==0,"bounded progress accepted");
    Test.Context.Host.ReleaseBytes(Test.Context.Host.Session,Bytes);Test.WorkerComplete(32,64);
    Test.Run("OwnerTask=false;task.defer(function()OwnerTask=true end)");
    Test.Turn(Value,Token);Test.Run("assert(Progress==64 and Delivered==32 and OwnerTask)");
    Check(Value.Pending==1&&Value.HighWater==1,"64-record quantum rearms one turn while tasks progress");
    Test.Turn(Value,Token);Test.Run("assert(Delivered==64)");
    Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);Check(Value.Released==1,"burst context release");
}
void FallbackAndAttemptRace(){
    Fixture Test;Inbox Failed;auto Token=Test.Install(Failed);Failed.Fail=1;Test.Admit();Test.WorkerComplete(0,1);
    Check(!Failed.Pending&&Failed.Calls==1,"failed enqueue leaves no host queue");Lui_Pump(Test.Runtime);Test.Run("assert(Delivered==1)");
    Inbox Value;Gate Entered,Resume;Value.Entered=&Entered;Value.Resume=&Resume;Value.FailFirst=true;
    Token=Test.Install(Value);Check(Failed.Released==1,"replacement releases prior registration");Test.Admit(3);
    Check(Test.Context.Host.RetainSession(Test.Context.Host.Session)==0,"blocked notification lease");
    std::thread First([&]{Test.Complete(1);Test.Context.Host.ReleaseSession(Test.Context.Host.Session);});
    Check(Entered.Wait(),"first notification held");
    Check(Lui_AcknowledgeNativeWakeV1(Test.Domain,Token)==0,"fallback overtakes callback return");
    Test.WorkerComplete(2,3);Resume.Set();First.join();Test.WorkerComplete(3,4);
    Check(Value.Calls==2&&Value.Pending==1&&Value.HighWater==1,"late failed attempt cannot clear newer pending wake");
    Test.Turn(Value,Token);Test.Run("assert(Delivered==4)");
    Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);Check(Value.Released==1,"attempt context released");
}
void UnboundSessionDoesNotSpin(){
    Fixture Test;Inbox Value;auto Token=Test.Install(Value);ProviderContext Raw;
    auto RawService=Service;RawService.Name="RawWakeProof";
    Check(Lui_OpenNativeSessionV2(Test.Domain,&RawService,&Provider,&Raw,&Raw.Host)==0,"unbound embedding session");
    LuiNativeIdV2 Input{},Request{};Raw.Host.CopyBytes(Raw.Host.Session,{},nullptr,0,&Input);
    Check(Lui_StartNativeRequestV2(Raw.Host.Session,0,Input,{},&Request)==0,"unbound request admission");Raw.Host.ReleaseBytes(Raw.Host.Session,Input);
    Check(Raw.Host.Complete(Raw.Host.Session,Request,0,{},{})==0,"unbound terminal notification");
    Test.Admit(32);LuiNativeIdV2 Bytes{};Test.Context.Host.CopyBytes(Test.Context.Host.Session,{},nullptr,0,&Bytes);
    for(int Index=0;Index<32;++Index)Test.Context.Host.Emit(Test.Context.Host.Session,Test.Context.Requests[0],Bytes);
    Test.Context.Host.ReleaseBytes(Test.Context.Host.Session,Bytes);Test.WorkerComplete(0,32);
    Test.Turn(Value,Token);Test.Run("assert(Delivered==32 and Progress==32)");
    Check(Value.Pending==0&&Value.Calls==1,"quantum continuation cannot spin on another consumer's records");
    LuiNativeDeliveryV2 Delivery{sizeof(Delivery),2};Check(Lui_NextNativeDeliveryV2(Raw.Host.Session,&Delivery)==0,"embedding consumer takes its own terminal");
    Lui_RetireNativeSessionV2(Raw.Host.Session);Lui_PumpNativeDomainV2(Test.Domain);
    Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);
}
void RetirementAndStaleWake(){
    Fixture Test;Inbox Old;Gate Entered,Resume;Old.Entered=&Entered;Old.Resume=&Resume;
    auto OldToken=Test.Install(Old);Test.Admit();Test.Context.Quiescent=false;
    Check(Test.Context.Host.RetainSession(Test.Context.Host.Session)==0,"retiring worker lease");
    std::thread Worker([&]{Test.Complete(0);Test.Context.Host.ReleaseSession(Test.Context.Host.Session);});
    Check(Entered.Wait(),"notification enters before retirement");Old.Stopped=true;
    Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);Check(Old.Released==0,"context retained through in-flight notify");
    Lui_Destroy(Test.Runtime);Test.Runtime=nullptr;
    Inbox Replacement;auto NewToken=Test.Install(Replacement);
    Check(NewToken!=OldToken&&Lui_AcknowledgeNativeWakeV1(Test.Domain,OldToken)==LUI_NATIVE_CLOSED,"stale generation cannot acknowledge replacement");
    Resume.Set();Worker.join();Test.Context.Quiescent=true;Lui_PumpNativeDomainV2(Test.Domain);
    Check(Old.Released==1&&!Old.Pending&&Test.Context.Destroyed==1,"retired notify completes without old VM or queued publication");
    Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);Check(Replacement.Released==1,"replacement releases exactly once");
}
void CancellationAndBusyOwner(){
    Fixture Test;Inbox Value;auto Token=Test.Install(Value);Test.Admit();
    Test.Run("assert(Ticket:Cancel())"); // Callback deliberately expects success; replace its waiter fixture below.
    // Cancellation's terminal remains deferred, so tear down without invoking
    // that success-only callback, then test a generation with a cancel waiter.
    Lui_Destroy(Test.Runtime);Test.Runtime=nullptr;Lui_PumpNativeDomainV2(Test.Domain);
    Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);Check(Value.Released==1,"queued cancellation context released");
    Fixture CancelTest;Inbox Canceled;Token=CancelTest.Install(Canceled);
    CancelTest.Run("Ticket=app:GetService('WakeProof'):Echo(buffer.create(0),nil,function(_,_,E)assert(E=='Canceled');Delivered+=1 end);assert(Ticket:Cancel());assert(Delivered==0)");
    CancelTest.Turn(Canceled,Token);CancelTest.Run("assert(Delivered==1)");
    Check(CancelTest.Context.Host.Complete(CancelTest.Context.Host.Session,CancelTest.Context.Requests[0],0,{},{})==LUI_NATIVE_CLOSED,"completion after canceled delivery cannot wake old waiter");
    Lui_SetLogCallback(CancelTest.Runtime,CancelTest.Runtime,[](void* Pointer,const char*,const char*){
        Check(Lui_IsOwnerPumpReadyV1(static_cast<LuiRuntime*>(Pointer))==0,"busy owner refuses nested pump without diagnostics");
    });
    CancelTest.Run("print('[LUI:OwnerWakeTest] BusyOwner')");
    Check(Lui_IsOwnerPumpReadyV1(CancelTest.Runtime)==1,"owner ready after VM returns");
    Lui_SetNativeWakeV1(CancelTest.Domain,nullptr,nullptr);
}
double Ms(Clock::duration Value){return std::chrono::duration<double,std::milli>(Value).count();}
struct Distribution {
    std::vector<double> Samples;
    double Rank(double Fraction){auto Values=Samples;std::sort(Values.begin(),Values.end());return Values[static_cast<size_t>(std::ceil(Values.size()*Fraction))-1];}
    void Print(const char* Name){std::printf("\"%s\":{\"Samples\":%zu,\"MedianMs\":%.6f,\"P95Ms\":%.6f,\"P99Ms\":%.6f,\"MaxMs\":%.6f}",Name,Samples.size(),Rank(.5),Rank(.95),Rank(.99),Rank(1));}
};
void Benchmark(bool Wake,int Cadence){
    Fixture Test;Inbox Value;uint64_t Token=Wake?Test.Install(Value):0;
    Distribution Latency,Intervals;
    std::mutex Mutex;std::condition_variable Changed;bool Work=false,Stopping=false;LuiNativeIdV2 Request{};Clock::time_point Completed;
    Check(Test.Context.Host.RetainSession(Test.Context.Host.Session)==0,"benchmark worker lease");
    std::thread Worker([&]{
        for(;;){
            LuiNativeIdV2 Current{};
            {std::unique_lock<std::mutex> Lock(Mutex);Changed.wait(Lock,[&]{return Work||Stopping;});if(Stopping)break;Work=false;Current=Request;}
            std::this_thread::sleep_for(std::chrono::milliseconds(2)); // Explicit fixed workload phase, not a race test.
            const unsigned char Data[64]{};LuiNativeIdV2 Bytes{};
            Check(Test.Context.Host.CopyBytes(Test.Context.Host.Session,Current,Data,sizeof(Data),&Bytes)==0,"benchmark owned echo bytes");
            {std::lock_guard<std::mutex> Lock(Mutex);Completed=Clock::now();}
            Check(Test.Context.Host.Complete(Test.Context.Host.Session,Current,0,Bytes,{})==0,"benchmark completion");
            Test.Context.Host.ReleaseBytes(Test.Context.Host.Session,Bytes);
        }
        Test.Context.Host.ReleaseSession(Test.Context.Host.Session);
    });
    auto Previous=Clock::now();
    for(int Index=0;Index<128;++Index){
        Test.Run("app:GetService('WakeProof'):Echo(buffer.create(0),nil,function(B,_,E)assert(not E and buffer.len(B)==64);Delivered+=1 end)");
        {std::lock_guard<std::mutex> Lock(Mutex);Request=Test.Context.Requests.back();Work=true;Changed.notify_one();}
        const auto Due=Clock::now()+std::chrono::milliseconds(Cadence);
        if(Wake){if(Value.TakeUntil(Due))Lui_AcknowledgeNativeWakeV1(Test.Domain,Token);}
        else std::this_thread::sleep_until(Due);
        auto Before=Clock::now();Intervals.Samples.push_back(Ms(Before-Previous));Lui_Pump(Test.Runtime);Previous=Before;
        // Core statistics synchronize with settlement's domain mutex. Completion
        // entry precedes it; Request removal proves that owner delivery occurred.
        LuiNativeStatsV2 Stats{sizeof(Stats),2};Lui_GetNativeStatsV2(Test.Domain,&Stats);
        Check(!Stats.Requests,"benchmark owner turn delivered echo");
        {std::lock_guard<std::mutex> Lock(Mutex);Latency.Samples.push_back(Ms(Clock::now()-Completed));}
    }
    {std::lock_guard<std::mutex> Lock(Mutex);Stopping=true;Changed.notify_one();}Worker.join();
    Test.Run("assert(Delivered==128)");
    if(Wake){Check(Latency.Rank(.95)<=8&&Latency.Rank(.99)<=16,"published headless wake budget");Lui_SetNativeWakeV1(Test.Domain,nullptr,nullptr);}
    std::printf("[LUI:OwnerWakePerformance] {\"Wake\":%s,\"FallbackMs\":%d,\"QueueHighWater\":%d,\"Notifications\":%d,",Wake?"true":"false",Cadence,Value.HighWater,Value.Calls.load());
    Latency.Print("CompletionEntryToOwnerReturn");std::printf(",");Intervals.Print("OwnerPumpInterval");std::printf("}\n");
}
}
int main(int Count,char** Arguments){
    if(Count>=2&&std::strcmp(Arguments[1],"--benchmark")==0){
        if(Count==3){char* End=nullptr;auto Cadence=std::strtol(Arguments[2],&End,10);if(!End||*End||Cadence<1||Cadence>1000)return 2;Benchmark(false,static_cast<int>(Cadence));Benchmark(true,static_cast<int>(Cadence));}
        else if(Count==2){Benchmark(false,16);Benchmark(false,64);Benchmark(true,64);}else return 2;
    }
    else{Boundaries();BurstAndContinuation();FallbackAndAttemptRace();UnboundSessionDoesNotSpin();RetirementAndStaleWake();CancellationAndBusyOwner();}
    if(!Failures)std::puts("[LUI:OwnerWakeTest] coalescing, owner deferral, ordering, fallback, context retirement and stale tokens passed");
    return Failures?1:0;
}
