#include "LuiHttp.h"
#include "LuiOwnerWake.h"
#include "Parser.h"
#include "../../runtime/internal/State.h"
#include "../../runtime/internal/Http.h"
#include <asio.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using Tcp = asio::ip::tcp;
int64_t Micros() { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count(); }
int Failures = 0;
void Check(bool Value, const char* Name) { if (!Value) { ++Failures; std::fprintf(stderr, "[LUI:HttpPolicyTest] %s\n", Name); } }
struct Peer {
    asio::io_context Io;
    Tcp::acceptor Acceptor{Io, {asio::ip::make_address("127.0.0.1"), 0}};
    unsigned short Port = Acceptor.local_endpoint().port();
    std::string Reply;
    bool Stall;
    std::mutex Mutex; std::condition_variable Changed;
    unsigned Requests = 0;
    std::vector<Lui::Http::Message> Received;
    struct Exchange {
        Tcp::socket Socket;
        Lui::Http::Parser Parser{Lui::Http::MessageKind::Request};
        std::array<char, 4096> Bytes{};
        explicit Exchange(asio::io_context& Io) : Socket(Io) {}
    };
    std::vector<std::shared_ptr<Exchange>> Held;
    std::thread Worker;
    Peer(std::string Reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK", bool Stall = false)
        : Reply(std::move(Reply)), Stall(Stall) {
        Accept(); Worker = std::thread([this] { Io.run(); });
    }
    std::string Origin() const { return "http://127.0.0.1:" + std::to_string(Port); }
    void Accept() {
        auto Value = std::make_shared<Exchange>(Io);
        Acceptor.async_accept(Value->Socket, [this, Value](const asio::error_code& Error) {
            if (Error) return;
            Accept(); Read(Value);
        });
    }
    void Read(const std::shared_ptr<Exchange>& Value) {
        Value->Socket.async_read_some(asio::buffer(Value->Bytes), [this, Value](const asio::error_code& Error, size_t Count) {
            if (Error) return;
            auto Parsed = Value->Parser.Feed({Value->Bytes.data(), Count});
            if (Parsed.Status == Lui::Http::ParseStatus::NeedMore) { Read(Value); return; }
            if (Parsed.Status == Lui::Http::ParseStatus::Failed) return;
            {
                std::lock_guard<std::mutex> Lock(Mutex);
                Received.push_back(*Value->Parser.GetResult()); ++Requests; Changed.notify_all();
            }
            if (Stall) { Held.push_back(Value); return; }
            asio::async_write(Value->Socket, asio::buffer(Reply), [Value](const asio::error_code&, size_t) {
                asio::error_code Ignored; Value->Socket.close(Ignored);
            });
        });
    }
    bool Wait(unsigned Count = 1) {
        std::unique_lock<std::mutex> Lock(Mutex);
        return Changed.wait_for(Lock, std::chrono::seconds(5), [&] { return Requests >= Count; });
    }
    ~Peer() { Io.stop(); if (Worker.joinable()) Worker.join(); }
};
struct Inbox {
    std::mutex Mutex; std::condition_variable Changed;
    std::atomic<uint64_t> Calls{0}, Releases{0};
    std::atomic<int64_t> FirstNotificationMicros{0};
    static uint32_t LUI_CALL Request(void* Context) {
        auto* Value = static_cast<Inbox*>(Context);
        int64_t Empty=0; Value->FirstNotificationMicros.compare_exchange_strong(Empty,Micros());
        { std::lock_guard<std::mutex> Lock(Value->Mutex); ++Value->Calls; }
        Value->Changed.notify_all(); return 1;
    }
    static void LUI_CALL Release(void* Context) { ++static_cast<Inbox*>(Context)->Releases; }
    bool Wait(uint64_t Previous) {
        std::unique_lock<std::mutex> Lock(Mutex);
        return Changed.wait_for(Lock, std::chrono::seconds(5), [&] { return Calls > Previous; });
    }
};
struct Environment {
    Inbox Wake;
    LuiRuntime* Runtime = Lui_Create();
    LuiNativeDomainV2* Domain = nullptr;
    uint64_t Registration = 0;
    bool Done = false;
    bool Measure = false;
    uint64_t MaxOwnerObservedVmBytes = 0;
    std::vector<double> PumpMilliseconds;
    std::string Error;
    static void LUI_CALL Log(void* Context, const char* Level, const char* Text) {
        auto* Value = static_cast<Environment*>(Context);
        if (std::strcmp(Level, "Error") == 0) Value->Error = Text;
        if (std::strcmp(Text, "[LUI:HttpPolicyTest] Done") == 0) Value->Done = true;
    }
    Environment() {
        LuiCapabilityDeclarationV1 Grant{sizeof(Grant), 1, LUI_CAPABILITY_NETWORK_CLIENT};
        Check(Lui_DeclareCapabilities(Runtime, &Grant) == 1, "grant");
        Lui_SetLogCallback(Runtime, this, Log);
        Domain = Lui_GetOwnerDomainV1(Runtime);
        LuiNativeWakeV1 Callback{sizeof(Callback), 1, &Wake, Inbox::Request, Inbox::Release};
        Check(Domain && Lui_SetNativeWakeV1(Domain, &Callback, &Registration) == LUI_NATIVE_OK, "wake registration");
    }
    void Script(const std::string& Source) {
        Check(Lui_RunScript(Runtime, Source.c_str(), "HttpPolicyFixture") == 1, "script compilation/execution");
    }
    void Start(const std::string& Body) {
        Done = false;
        Script("local Http=app:GetService('HttpService') task.spawn(function() " + Body +
            " print('[LUI:HttpPolicyTest] Done') end)");
    }
    void Turn() {
        Lui_AcknowledgeNativeWakeV1(Domain, Registration);
        const auto Start=Clock::now(); Lui_Pump(Runtime);
        if(Measure) {
            PumpMilliseconds.push_back(std::chrono::duration<double,std::milli>(Clock::now()-Start).count());
            MaxOwnerObservedVmBytes=std::max(MaxOwnerObservedVmBytes,Runtime->VmBytes);
        }
    }
    bool Drain() {
        auto End = Clock::now() + std::chrono::seconds(5);
        while (!Done && Error.empty() && Clock::now() < End) {
            auto Previous = Wake.Calls.load(); Turn();
            if (!Done && Error.empty() && Wake.Calls == Previous) Wake.Wait(Previous);
        }
        if (!Error.empty()) std::fprintf(stderr, "[LUI:HttpPolicyTest] %s\n", Error.c_str());
        Check(Done && Error.empty(), "owner delivery"); return Done && Error.empty();
    }
    LuiHttpStatsV1 Stats() {
        LuiHttpStatsV1 Result{sizeof(Result), 1};
        Check(Lui_GetHttpStatsV1(Runtime, &Result) == 1, "statistics"); return Result;
    }
    void Empty() {
        auto End = Clock::now() + std::chrono::seconds(5);
        while (Stats().LiveRequests && Clock::now() < End) { Turn(); std::this_thread::yield(); }
        auto Value = Stats(); Check(!Value.LiveRequests && !Value.ReservedBytes, "actual reservation release");
    }
    void Destroy() {
        if (!Runtime) return;
        Lui_SetNativeWakeV1(Domain, nullptr, &Registration);
        Lui_Destroy(Runtime); Lui_ReleaseNativeDomainV2(Domain);
        Runtime=nullptr; Domain=nullptr;
        Check(Wake.Releases == 1, "wake context retirement");
    }
    ~Environment() { Destroy(); }
};
LuiHttpOptionsV1 Options() { return {sizeof(LuiHttpOptionsV1), 1, 0, 1024 * 1024, 8 * 1024 * 1024, 32, 608ull * 1024 * 1024, nullptr, 0, 0}; }
bool Configure(Environment& Owner, LuiHttpOptionsV1 Value, const std::vector<std::string>& Origins = {}) {
    std::vector<LuiHttpOriginV1> Entries;
    for (const auto& Origin : Origins) Entries.push_back({Origin.data(), static_cast<uint32_t>(Origin.size()), 0});
    Value.Origins = Entries.data(); Value.OriginCount = static_cast<uint32_t>(Entries.size());
    return Lui_SetHttpOptionsV1(Owner.Runtime, &Value) == 1;
}
void Configuration() {
    Environment Owner;
    auto Value = Options();
    Check(!Configure(Owner, Value, {"http://localhost/path"}), "origin path accepted");
    Check(!Configure(Owner, Value, {"http://user:pass@localhost"}), "origin credentials accepted");
    Check(!Configure(Owner, Value, {"http://LOCALHOST", "http://localhost:80/"}), "duplicate canonical origin");
    Value.Flags = 2; Check(!Configure(Owner, Value), "unknown flags");
    Value = Options(); Value.MaxConcurrent = 33; Check(!Configure(Owner, Value), "concurrency ceiling");
    Value = Options(); Value.MaxRequestBytes++; Check(!Configure(Owner, Value), "request ceiling");
    Value = Options(); Value.MaxResponseBytes++; Check(!Configure(Owner, Value), "response ceiling");
    Value = Options(); Value.MaxReservedBytes++; Check(!Configure(Owner, Value), "budget ceiling");
    Value = Options(); Value.StructSize--; Check(!Configure(Owner, Value), "exact structure size");
    Value = Options(); Value.AbiVersion++; Check(!Configure(Owner, Value), "version");
    Value = Options();
    bool Wrong = true;
    std::thread Thread([&] { Wrong = Configure(Owner, Value); LuiHttpStatsV1 Stats{sizeof(Stats),1}; Check(!Lui_GetHttpStatsV1(Owner.Runtime,&Stats), "wrong owner statistics"); });
    Thread.join(); Check(!Wrong, "wrong owner configuration");
    Check(Configure(Owner, Value, {"https://EXAMPLE.com:443/"}), "valid options after failures");
    Check(!Configure(Owner, Value), "replacement configuration");
    Check(!Owner.Stats().Accepted && !Owner.Stats().ReservedBytes, "configuration starts work");
    Check(!Owner.Runtime->Network,"configuration starts network worker");
    Environment Empty; Value=Options(); Value.MaxReservedBytes=256*1024;
    Check(Configure(Empty,Value),"minimum reservation configuration");
    Empty.Start("local Ok,Error=pcall(function() Http:GetAsync('http://127.0.0.1:1/') end) assert(not Ok and string.find(Error,'ResourceExhausted',1,true))");
    Empty.Drain(); Check(!Empty.Runtime->Network,"rejected budget starts worker");
}
void MethodsAndOrigins() {
    std::string Reply = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Type: application/octet-stream\r\n\r\n" + std::string("a\0b",3);
    Peer Server(Reply); Environment Owner;
    auto Value = Options(); Value.MaxRequestBytes = 4; Value.MaxResponseBytes = 16;
    Check(Configure(Owner, Value, {Server.Origin()}), "origin configuration");
    Owner.Start("local Url='" + Server.Origin() + "/items' " + R"(
        for _,Method in {'GET','POST','PUT','PATCH','DELETE'} do
            local Input=if Method=='GET' then nil else buffer.fromstring('p\0q')
            local R=Http:RequestAsync({Url=Url,Method=Method,Body=Input,Headers={{Name='Authorization',Value='SyntheticHttpCredentialCanary'}}})
            assert(R.Success and R.StatusCode==200 and R.Body=='a\0b')
        end
        assert(not pcall(function() Http:GetAsync('http://localhost:1/') end))
        assert(not pcall(function() Http:GetAsync(string.gsub(Url,'http:','https:')) end))
        assert(not pcall(function() Http:RequestAsync({Url=Url,Method='POST',Body='12345'}) end))
        assert(not pcall(function() Http:RequestAsync({Url=Url,MaxResponseBytes=17}) end))
    )");
    Owner.Drain(); Owner.Empty();
    Check(Server.Wait(5), "five method exchanges");
    { std::lock_guard<std::mutex> Lock(Server.Mutex);
      Check(Server.Received.size()==5 && Server.Received[1].Body==std::string("p\0q",3), "binary method data"); }
    const char* Diagnostics=Lui_GetDiagnosticsJson(Owner.Runtime);
    Check(!Diagnostics || !std::strstr(Diagnostics,"SyntheticHttpCredentialCanary"), "credential diagnostics");
}
void RedirectsAndOwnership() {
    Peer Target;
    Peer Source("HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: " + Target.Origin() + "/steal\r\n\r\n");
    Environment Owner; Check(Configure(Owner, Options(), {Source.Origin()}), "redirect configuration");
    Owner.Start("local R=Http:RequestAsync({Url='" + Source.Origin() + "/',Headers={{Name='Authorization',Value='SyntheticSecret'}}}) assert(R.StatusCode==302 and not R.Success)");
    Owner.Drain(); Owner.Empty();
    { std::lock_guard<std::mutex> Lock(Target.Mutex); Check(Target.Requests == 0, "redirect followed or credential leaked"); }
    Peer Echo; Environment Copy;
    Copy.Start("local Data=buffer.fromstring('original') task.defer(function() buffer.fill(Data,0,0) end) "
        "Http:RequestAsync({Url='" + Echo.Origin() + "/',Method='POST',Body=Data})");
    Copy.Drain(); Copy.Empty(); Check(Echo.Wait(), "owned request exchange");
    { std::lock_guard<std::mutex> Lock(Echo.Mutex); Check(Echo.Received[0].Body=="original", "caller mutation changed native body"); }
}
void PublicPolicy() {
    Environment Owner; auto Value=Options(); Value.Flags=LUI_HTTP_PUBLIC_ONLY;
    Check(Configure(Owner, Value), "public-only configuration");
    Owner.Start(R"(
        for _,Host in {'127.0.0.1','10.1.2.3','100.64.0.1','169.254.169.254','172.16.0.1','192.168.1.1','192.0.2.1',
            '198.18.0.1','198.51.100.1','203.0.113.1','224.0.0.1','255.255.255.255','[::1]','[::ffff:127.0.0.1]',
            '[fc00::1]','[fe80::1]','[64:ff9b::7f00:1]','[2002:7f00:1::1]','[2001:db8::1]','[3fff::1]','localhost'} do
            local Ok,Error=pcall(function() Http:RequestAsync({Url='http://'..Host..'/',TimeoutMs=200}) end)
            assert(not Ok and string.find(Error,'PolicyDenied',1,true),tostring(Error))
        end
    )");
    Owner.Drain(); Owner.Empty();
    Check(Owner.Stats().Accepted==1, "numeric denied endpoints allocated requests"); // Only DNS localhost reaches worker resolution.
}
void Limits(bool Concurrency) {
    Peer Server; Environment Owner; auto Value=Options();
    Value.MaxResponseBytes=16; Value.MaxConcurrent=Concurrency?1:2;
    Value.MaxReservedBytes=Concurrency?1024*1024:263000;
    Check(Configure(Owner,Value,{Server.Origin()}), "limit configuration");
    const std::string Url=Server.Origin()+"/";
    Owner.Script("local Http=app:GetService('HttpService') task.spawn(function() local R=Http:GetAsync('"+Url+
        "') assert(R.Body=='OK') print('[LUI:HttpPolicyTest] Done') end) task.spawn(function() local Ok,Error=pcall(function() Http:GetAsync('"+
        Url+"') end) assert(not Ok and string.find(Error,'"+(Concurrency?"TooManyRequests":"ResourceExhausted")+"',1,true)) end)");
    Owner.Turn(); Check(Server.Wait(), "bounded request reached peer");
    auto Previous=Owner.Wake.Calls.load();
    if (!Owner.Stats().Completed) Owner.Wake.Wait(Previous);
    auto Queued=Owner.Stats();
    Check(Queued.Completed==1 && Queued.LiveRequests==1 && Queued.ReservedBytes>0, "queued result released reservation early");
    Owner.Drain(); Owner.Empty();
    auto Final=Owner.Stats(); Check(Final.Accepted==1 && Final.Rejected==1 && Final.PeakRequests==1 && Final.PeakReservedBytes<=Value.MaxReservedBytes,"bounded admission metrics");
}
void CancellationAndTeardown() {
    Peer Server("",true); Environment Owner;
    Owner.Start("local Ok,Error=pcall(function() Http:GetAsync('"+Server.Origin()+"/') end) assert(not Ok and string.find(Error,'Canceled',1,true))");
    Owner.Turn(); Check(Server.Wait(),"cancel barrier peer");
    Owner.Script("app:GetService('HttpService'):CancelAll()"); Owner.Drain(); Owner.Empty();
    Peer Slow("",true); Environment Timeout;
    Timeout.Start("local Ok,Error=pcall(function() Http:RequestAsync({Url='"+Slow.Origin()+"/',TimeoutMs=20}) end) assert(not Ok and string.find(Error,'TimedOut',1,true))");
    Timeout.Drain(); Timeout.Empty();
    Peer Outstanding("",true);
    { Environment Closing;
      Closing.Start("Http:GetAsync('"+Outstanding.Origin()+"/') error('late callback')"); Closing.Turn();
      Check(Outstanding.Wait(),"shutdown barrier peer"); Check(Closing.Stats().LiveRequests==1,"shutdown ownership absent"); }
}
void Tokens() {
    Peer Slow("",true), Healthy; Environment Owner;
    Owner.Script("local Http=app:GetService('HttpService') Token=Http:CreateCancellationToken() "
        "assert(not Token.IsCanceled and tostring(Token)=='HttpCancellationToken' and getmetatable(Token)=='HttpCancellationToken') "
        "local Finished=0 local function Finish() Finished+=1 if Finished==2 then print('[LUI:HttpPolicyTest] Done') end end "
        "task.spawn(function() local Ok,Error=pcall(function() Http:RequestAsync({Url='"+Slow.Origin()+"/',CancellationToken=Token}) end) "
        "assert(not Ok and string.find(Error,'Canceled',1,true)) Finish() end) "
        "task.spawn(function() local Reply=Http:GetAsync('"+Healthy.Origin()+"/') assert(Reply.Body=='OK') Finish() end)");
    Owner.Turn(); Check(Slow.Wait(),"token cancellation barrier");
    Owner.Script("for Index=1,1000 do Token:Cancel() end assert(Token.IsCanceled) "
        "assert(not pcall(function() Token.IsCanceled=false end))");
    Owner.Drain(); Owner.Empty();
    Owner.Start("assert(not pcall(function() Http:RequestAsync({Url='"+Healthy.Origin()+"/',CancellationToken=Token}) end)) "
        "assert(not pcall(function() Http:RequestAsync({Url='"+Healthy.Origin()+"/',CancellationToken={}}) end)) "
        "local Kept={Token} for Index=2,128 do Kept[Index]=Http:CreateCancellationToken() end "
        "local Ok,Error=pcall(function() Http:CreateCancellationToken() end) assert(not Ok and string.find(Error,'TooManyCancellationTokens',1,true)) "
        "Token=nil");
    Owner.Drain();
    // Observe the existing generation teardown through its DLL API. Linking a
    // second Luau VM into this executable to invoke GC on a foreign State is
    // invalid because each VM image has its own static runtime sentinels.
    auto Resources=Owner.Runtime->HttpState;
    Owner.Destroy();
    Check(Resources->LiveCancellationTokens==0,"token generation retirement");
}
void Benchmark() {
    Peer Server; Environment Owner; std::vector<double> Samples,Notifications;
    Owner.Measure=true;
    const uint64_t BaselineVmBytes=Owner.Runtime->VmBytes;
    for (int Index=0;Index<128;++Index) {
        Owner.Empty(); Owner.Wake.FirstNotificationMicros=0;
        auto Start=Clock::now(); Owner.Start("Http:GetAsync('"+Server.Origin()+"/')"); Owner.Drain();
        Samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-Start).count());
        const int64_t Notification=Owner.Wake.FirstNotificationMicros.load();
        Check(Notification!=0,"benchmark notification absent");
        if(Notification) Notifications.push_back(static_cast<double>(Micros()-Notification)/1000.0);
    }
    Owner.Empty(); std::sort(Samples.begin(),Samples.end()); std::sort(Notifications.begin(),Notifications.end());
    auto Percentile=[](const std::vector<double>& Values,size_t Percent) {
        return Values.empty()?0.0:Values[(Percent*Values.size()+99)/100-1];
    };
    // Two batches of eight requests use the same existing worker and service.
    // Admission occurs in one owner turn, so the native high-water is meaningful
    // even when the loopback peer responds before that turn finishes.
    const auto BatchStart=Clock::now();
    for(int Batch=0;Batch<2;++Batch) {
        Owner.Done=false;
        Owner.Script("local Http=app:GetService('HttpService') local Finished=0 for Index=1,8 do task.spawn(function() local R=Http:GetAsync('"+Server.Origin()+
            "/') assert(R.Success) Finished+=1 if Finished==8 then print('[LUI:HttpPolicyTest] Done') end end) end");
        Owner.Drain(); Owner.Empty();
    }
    const double BatchMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-BatchStart).count();
    std::sort(Owner.PumpMilliseconds.begin(),Owner.PumpMilliseconds.end()); auto Stats=Owner.Stats();
    Check(Stats.PeakRequests==8 && Stats.Accepted==144,"concurrent benchmark admission");
    std::printf("[LUI:HttpPolicyBenchmark] SequentialRequests=128 TotalRequests=144 ScriptSubmitToOwnerReturnMedianMs=%.6f P95Ms=%.6f P99Ms=%.6f PeakRequests=%llu PeakPayloadReservationBytes=%llu RetainedRequests=%llu RetainedReservationBytes=%llu\n",
        Samples[63],Samples[121],Samples[126],static_cast<unsigned long long>(Stats.PeakRequests),static_cast<unsigned long long>(Stats.PeakReservedBytes),
        static_cast<unsigned long long>(Stats.LiveRequests),static_cast<unsigned long long>(Stats.ReservedBytes));
    std::printf("[LUI:HttpPolicyBenchmark] NotificationRequestEntryToOwnerReturnP95Ms=%.6f P99Ms=%.6f OwnerPumpElapsedP95Ms=%.6f P99Ms=%.6f ConcurrentBatchRequests=16 BatchWidth=8 BatchElapsedMs=%.6f VmBaselineBytes=%llu VmAfterBytes=%llu MaxOwnerObservedVmBytes=%llu\n",
        Percentile(Notifications,95),Percentile(Notifications,99),Percentile(Owner.PumpMilliseconds,95),Percentile(Owner.PumpMilliseconds,99),BatchMilliseconds,
        static_cast<unsigned long long>(BaselineVmBytes),static_cast<unsigned long long>(Owner.Runtime->VmBytes),static_cast<unsigned long long>(Owner.MaxOwnerObservedVmBytes));
}
}
int main(int Count,char** Args) {
    if(Count==2 && std::strcmp(Args[1],"--benchmark")==0) Benchmark();
    else {
        std::fputs("[LUI:HttpPolicyTest] Configuration\n",stderr); Configuration();
        std::fputs("[LUI:HttpPolicyTest] MethodsAndOrigins\n",stderr); MethodsAndOrigins();
        std::fputs("[LUI:HttpPolicyTest] RedirectsAndOwnership\n",stderr); RedirectsAndOwnership();
        std::fputs("[LUI:HttpPolicyTest] PublicPolicy\n",stderr); PublicPolicy();
        std::fputs("[LUI:HttpPolicyTest] Concurrency\n",stderr); Limits(true);
        std::fputs("[LUI:HttpPolicyTest] Reservation\n",stderr); Limits(false);
        std::fputs("[LUI:HttpPolicyTest] CancellationAndTeardown\n",stderr); CancellationAndTeardown();
        std::fputs("[LUI:HttpPolicyTest] Tokens\n",stderr); Tokens();
    }
    if(!Failures) std::puts("[LUI:HttpPolicyTest] policy, ownership, redirect refusal, limits, lifecycle, owner wake and resource accounting passed");
    return Failures?1:0;
}
