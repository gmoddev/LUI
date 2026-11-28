#include "LuiWebSocket.h"
#include "LuiHttp.h"
#include "LuiOwnerWake.h"
#include "../../runtime/internal/State.h"
#include "../../runtime/internal/WebSocket.h"
#include "../../runtime/network/tls/Provider.h"
#include <openssl/evp.h>
#include <asio.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

extern "C" int Lui_WebSocketLayoutProof(void);

namespace {
using Tcp = asio::ip::tcp;
using Clock = std::chrono::steady_clock;
int Failures = 0;
void Check(bool Ok, const std::string& Name) { if (!Ok) { ++Failures; std::fprintf(stderr,"[LUI:WebSocketTest] %s\n",Name.c_str()); } }
std::string Fixture(const char* Name) {
    std::ifstream Input(std::string(LUI_TLS_FIXTURES)+"/"+Name,std::ios::binary);
    Check(bool(Input),"TLS fixture missing"); return {std::istreambuf_iterator<char>(Input),{}};
}
// Independent synthetic peer: no production protocol parser or serializer.
struct Peer {
    asio::io_context Io;
    Tcp::acceptor Acceptor{Io,{asio::ip::make_address("127.0.0.1"),0}};
    unsigned short Port = Acceptor.local_endpoint().port();
    bool Secure, StallTls, StallUpgrade;
    std::shared_ptr<Lui::Tls::Settings> Settings;
    std::mutex Mutex; std::condition_variable Changed;
    unsigned Accepted=0, Upgrades=0, Messages=0, TlsHellos=0;
    bool InvalidClientFrame=false;
    struct Exchange {
        Tcp::socket Socket;
        std::unique_ptr<Lui::Tls::Stream> Tls;
        std::string Request, Reply;
        std::array<unsigned char,14> Header{};
        std::vector<unsigned char> Payload;
        unsigned Opcode=0; size_t Length=0, Prefix=0;
        explicit Exchange(asio::io_context& Io):Socket(Io){}
    };
    std::vector<std::shared_ptr<Exchange>> Held; // Worker only.
    std::thread Worker;
    Peer(bool Secure=false,const char* Credential="Valid.p12",bool StallTls=false,bool StallUpgrade=false)
        :Secure(Secure),StallTls(StallTls),StallUpgrade(StallUpgrade) {
        if(Secure) {
            std::string Error;
            Settings=Lui::Tls::Settings::Load({},Fixture(Credential),"test-only",Error);
            Check(Settings && Settings->Server,"TLS peer settings");
        }
        Accept(); Worker=std::thread([this]{Io.run();});
    }
    template<typename Handler> void ReadExact(const std::shared_ptr<Exchange>& Value,asio::mutable_buffer Buffer,Handler HandlerValue) {
        if(Value->Tls) asio::async_read(Value->Tls->Socket,Buffer,std::move(HandlerValue));
        else asio::async_read(Value->Socket,Buffer,std::move(HandlerValue));
    }
    template<typename Handler> void Write(const std::shared_ptr<Exchange>& Value,Handler HandlerValue) {
        if(Value->Tls) asio::async_write(Value->Tls->Socket,asio::buffer(Value->Reply),std::move(HandlerValue));
        else asio::async_write(Value->Socket,asio::buffer(Value->Reply),std::move(HandlerValue));
    }
    void Accept() {
        auto Value=std::make_shared<Exchange>(Io);
        Acceptor.async_accept(Value->Socket,[this,Value](const asio::error_code& Error){
            if(Error)return; Held.push_back(Value); Accept();
            {std::lock_guard<std::mutex> Lock(Mutex);++Accepted;} Changed.notify_all();
            if(Secure) {
                if(StallTls) {
                    // Observe a real TLS handshake record / ClientHello before
                    // releasing a cancellation/shutdown barrier; send no reply.
                    asio::async_read(Value->Socket,asio::buffer(Value->Header.data(),6),[this,Value](const asio::error_code& Error,size_t){
                        if(Error||Value->Header[0]!=22||Value->Header[5]!=1)return;
                        {std::lock_guard<std::mutex> Lock(Mutex);++TlsHellos;}Changed.notify_all();
                    });
                    return;
                }
                Value->Tls=std::make_unique<Lui::Tls::Stream>(Value->Socket,Settings,true);
                Value->Tls->Socket.async_handshake(asio::ssl::stream_base::server,[this,Value](const asio::error_code& Error){if(!Error)Upgrade(Value);});
            }else Upgrade(Value);
        });
    }
    void Upgrade(const std::shared_ptr<Exchange>& Value) {
        auto Handler=[this,Value](const asio::error_code& Error,size_t){
            if(Error)return;
            {std::lock_guard<std::mutex> Lock(Mutex);++Upgrades;} Changed.notify_all();
            if(StallUpgrade)return;
            auto Start=Value->Request.find("Sec-WebSocket-Key:");
            if(Start==std::string::npos)return;
            Start+=18; while(Start<Value->Request.size()&&Value->Request[Start]==' ')++Start;
            auto Key=Value->Request.substr(Start,Value->Request.find("\r\n",Start)-Start);
            Key+="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            unsigned char Digest[EVP_MAX_MD_SIZE];unsigned Count=0;
            EVP_Digest(Key.data(),Key.size(),Digest,&Count,EVP_sha1(),nullptr);
            unsigned char Encoded[64]{};auto Bytes=EVP_EncodeBlock(Encoded,Digest,Count);
            Value->Reply="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+std::string(reinterpret_cast<char*>(Encoded),Bytes)+"\r\n\r\n";
            Write(Value,[this,Value](const asio::error_code& Error,size_t){if(!Error)Frame(Value);});
        };
        if(Value->Tls)asio::async_read_until(Value->Tls->Socket,asio::dynamic_buffer(Value->Request,32768),"\r\n\r\n",std::move(Handler));
        else asio::async_read_until(Value->Socket,asio::dynamic_buffer(Value->Request,32768),"\r\n\r\n",std::move(Handler));
    }
    void Frame(const std::shared_ptr<Exchange>& Value) {
        ReadExact(Value,asio::buffer(Value->Header.data(),2),[this,Value](const asio::error_code& Error,size_t){
            if(Error)return;
            Value->Opcode=Value->Header[0]&15;
            if((Value->Header[0]&0xf0)!=0x80 || !(Value->Header[1]&128)) {
                {std::lock_guard<std::mutex> Lock(Mutex);InvalidClientFrame=true;}return;
            }
            auto Length=Value->Header[1]&127;Value->Prefix=Length==126?2:Length==127?8:0;
            Value->Length=Length;
            ReadExact(Value,asio::buffer(Value->Header.data()+2,Value->Prefix+4),[this,Value](const asio::error_code& Error,size_t){
                if(Error)return;
                if(Value->Prefix){Value->Length=0;for(size_t Index=0;Index<Value->Prefix;++Index)Value->Length=(Value->Length<<8)|Value->Header[2+Index];}
                if(Value->Length>1024*1024)return;
                Value->Payload.resize(Value->Length);
                ReadExact(Value,asio::buffer(Value->Payload),[this,Value](const asio::error_code& Error,size_t){
                    if(Error)return;
                    for(size_t Index=0;Index<Value->Length;++Index)Value->Payload[Index]^=Value->Header[2+Value->Prefix+Index%4];
                    Value->Reply.assign(1,static_cast<char>(0x80|Value->Opcode));
                    if(Value->Length<126)Value->Reply.push_back(static_cast<char>(Value->Length));
                    else if(Value->Length<=65535){Value->Reply.push_back(126);Value->Reply.push_back(char(Value->Length>>8));Value->Reply.push_back(char(Value->Length));}
                    else {Value->Reply.push_back(127);for(int Shift=56;Shift>=0;Shift-=8)Value->Reply.push_back(char(uint64_t(Value->Length)>>Shift));}
                    Value->Reply.append(reinterpret_cast<const char*>(Value->Payload.data()),Value->Payload.size());
                    if(Value->Opcode==1||Value->Opcode==2){std::lock_guard<std::mutex> Lock(Mutex);++Messages;Changed.notify_all();}
                    if(Value->Opcode==9)Value->Reply[0]=char(0x8a);
                    Write(Value,[this,Value](const asio::error_code& Error,size_t){
                        if(Error)return;
                        if(Value->Opcode==8){asio::error_code Ignored;Value->Socket.close(Ignored);}
                        else Frame(Value);
                    });
                });
            });
        });
    }
    bool Wait(bool UpgradeStage=false) {
        std::unique_lock<std::mutex> Lock(Mutex);
        return Changed.wait_for(Lock,std::chrono::seconds(5),[&]{return UpgradeStage?Upgrades>0:Accepted>0;});
    }
    bool WaitMessage() {
        std::unique_lock<std::mutex> Lock(Mutex);
        return Changed.wait_for(Lock,std::chrono::seconds(5),[&]{return Messages>0;});
    }
    bool WaitTls() {
        std::unique_lock<std::mutex> Lock(Mutex);
        return Changed.wait_for(Lock,std::chrono::seconds(5),[&]{return TlsHellos>0;});
    }
    std::string Url()const{return std::string(Secure?"wss":"ws")+"://127.0.0.1:"+std::to_string(Port)+"/echo";}
    ~Peer(){Io.stop();if(Worker.joinable())Worker.join();}
};
struct Environment {
    LuiRuntime* Runtime=Lui_Create();
    bool Done=false,WrongThread=false;
    unsigned short Port=0;
    bool Authorizing=false,HandlerEntered=false;
    std::string Error;
    std::thread::id Owner=std::this_thread::get_id();
    std::vector<double> Samples,Setup,Pumps;
    Clock::time_point Stamp;
    std::mutex Mutex;std::condition_variable Changed;
    std::atomic<uint64_t> WakeCount{0};
    LuiNativeDomainV2* Domain=nullptr;uint64_t Registration=0;
    static uint32_t LUI_CALL Wake(void* Context){auto* Value=static_cast<Environment*>(Context);{std::lock_guard<std::mutex> Lock(Value->Mutex);++Value->WakeCount;}Value->Changed.notify_all();return 1;}
    static void LUI_CALL Release(void*){}
    static void LUI_CALL Log(void* Context,const char* Level,const char* Message){
        auto* Value=static_cast<Environment*>(Context);Value->WrongThread|=Value->Owner!=std::this_thread::get_id();
        if(!std::strcmp(Level,"Error"))Value->Error=Message;
        if(!std::strncmp(Message,"[LUI:HostedTest] Port:",22))Value->Port=static_cast<unsigned short>(std::stoi(Message+22));
        if(!std::strcmp(Message,"[LUI:HostedTest] Authorizing"))Value->Authorizing=true;
        if(!std::strcmp(Message,"[LUI:HostedTest] Entered"))Value->HandlerEntered=true;
        if(!std::strcmp(Message,"[LUI:WebSocketTest] Done"))Value->Done=true;
        if(!std::strcmp(Message,"[LUI:WebSocketTest] Begin"))Value->Stamp=Clock::now();
        if(!std::strcmp(Message,"[LUI:WebSocketTest] Sample"))Value->Samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-Value->Stamp).count());
        if(!std::strcmp(Message,"[LUI:WebSocketTest] Setup"))Value->Setup.push_back(std::chrono::duration<double,std::milli>(Clock::now()-Value->Stamp).count());
    }
    explicit Environment(bool Grant=true,bool Trust=true,uint64_t ExtraGrants=0){
        Lui_SetLogCallback(Runtime,this,Log);
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants),1,(Grant?LUI_CAPABILITY_NETWORK_CLIENT:0)|ExtraGrants};
        Check(Lui_DeclareCapabilities(Runtime,&Grants)==1,"capability grant");
        if(Trust){auto Roots=Fixture("Root.pem");LuiTlsOptionsV1 Options{sizeof(Options),1,Roots.data(),uint32_t(Roots.size()),nullptr,0,nullptr,0};Check(Lui_SetTlsOptions(Runtime,&Options)==1,"trust options");}
        Domain=Lui_GetOwnerDomainV1(Runtime);
        LuiNativeWakeV1 Callback{sizeof(Callback),1,this,Wake,Release};
        Check(Domain&&Lui_SetNativeWakeV1(Domain,&Callback,&Registration)==LUI_NATIVE_OK,"owner wake");
    }
    void Script(const std::string& Source){Check(Lui_RunScript(Runtime,Source.c_str(),"WebSocketFixture")==1,"script execution");}
    void Start(const std::string& Source){Done=false;Script("local WebSocket=app:GetService('WebSocketService') task.spawn(function() "+Source+" print('[LUI:WebSocketTest] Done') end)");}
    void Turn(){Lui_AcknowledgeNativeWakeV1(Domain,Registration);auto Start=Clock::now();Lui_Pump(Runtime);Pumps.push_back(std::chrono::duration<double,std::milli>(Clock::now()-Start).count());}
    bool Drain(){auto End=Clock::now()+std::chrono::seconds(5);while(!Done&&Error.empty()&&Clock::now()<End){auto Before=WakeCount.load();Turn();if(!Done&&Error.empty()){std::unique_lock<std::mutex> Lock(Mutex);Changed.wait_for(Lock,std::chrono::milliseconds(10),[&]{return WakeCount>Before;});}}
        if(!Error.empty())std::fprintf(stderr,"[LUI:WebSocketTest] %s\n",Error.c_str());Check(Done&&Error.empty()&&!WrongThread,"owner settlement");return Done&&Error.empty();}
    LuiWebSocketStatsV1 Stats(){LuiWebSocketStatsV1 Value{sizeof(Value),1};Check(Lui_GetWebSocketStatsV1(Runtime,&Value)==1,"statistics");return Value;}
    void Destroy(){if(!Runtime)return;auto Resources=Runtime->WebSocketState;Lui_SetNativeWakeV1(Domain,nullptr,&Registration);Lui_Destroy(Runtime);Lui_ReleaseNativeDomainV2(Domain);Runtime=nullptr;
        Check(!Resources||(!Resources->LiveConnections&&!Resources->ReservedBytes&&!Resources->InboundBytes&&!Resources->OutboundBytes&&!Resources->PendingSends),"actual ownership after destruction");}
    ~Environment(){Destroy();}
};
LuiWebSocketOptionsV1 Options(){return {sizeof(LuiWebSocketOptionsV1),1,8,1024*1024,1024*1024,4*1024*1024,16,0,128ull*1024*1024};}
void HostedEcho(bool Secure=false){
    Environment Owner(true,!Secure,LUI_CAPABILITY_NETWORK_SERVER);
    if(Secure) {
        auto Roots=Fixture("Root.pem"),Pfx=Fixture("Valid.p12");
        LuiTlsOptionsV1 Tls{sizeof(Tls),1,Roots.data(),uint32_t(Roots.size()),Pfx.data(),uint32_t(Pfx.size()),"test-only",9};
        Check(Lui_SetTlsOptions(Owner.Runtime,&Tls)==1,"hosted TLS credentials");
    }
    Owner.Start(std::string("local Secure=")+(Secure?"true":"false")+R"(
        local Server=app:GetService('HttpServerService'):CreateServer({Port=0,TLS=Secure})
        Server:Route('GET','/health',function() return {Body='ok'} end)
        Server:RouteWebSocket('/echo',function(Socket,Request)
            assert(Request.Method=='GET' and Request.Path=='/echo')
            assert(Request.LocalEndpoint.Port==Server.Port)
            local Message=Socket:ReceiveAsync();assert(Message)
            Socket:SendAsync(Message.Data,Message.Binary)
            Socket:Close()
        end,{Protocols={'lui.echo.v1'},Authorize=function(Request) assert(Request.Path=='/echo');return true end})
        Server:Start()
        local Socket=WebSocket:ConnectAsync((Secure and 'wss' or 'ws')..'://127.0.0.1:'..Server.Port..'/echo',{Protocols={'lui.echo.v1'}})
        assert(Socket.Subprotocol=='lui.echo.v1')
        Socket:SendAsync(buffer.fromstring('hosted\0binary'))
        local Message=Socket:ReceiveAsync();assert(Message and Message.Binary and Message.Data=='hosted\0binary')
        Socket:Close();assert(Socket:ReceiveAsync()==nil);Server:Close()
    )");
    Owner.Drain();Owner.Destroy();
}
struct HostedPeer {
    asio::io_context Io;
    Tcp::socket Socket{Io};
    std::thread Worker;
    std::atomic<bool> Done{false};
    std::string Reply,Error;
    HostedPeer(unsigned short Port,std::string Wire,bool Upgrade=false) {
        Worker=std::thread([this,Port,Wire=std::move(Wire),Upgrade] {
            try {
                Socket.connect(Tcp::endpoint(asio::ip::make_address("127.0.0.1"),Port));
                Socket.non_blocking(true);size_t Offset=0;const auto Deadline=Clock::now()+std::chrono::seconds(3);
                std::array<char,16384> Bytes{};
                while(Clock::now()<Deadline) {
                    asio::error_code Code;
                    if(Offset<Wire.size()) {
                        auto Count=Socket.write_some(asio::buffer(Wire.data()+Offset,Wire.size()-Offset),Code);
                        if(!Code)Offset+=Count;
                    }else {
                        auto Count=Socket.read_some(asio::buffer(Bytes),Code);if(!Code)Reply.append(Bytes.data(),Count);
                        if(Code && Code!=asio::error::would_block && Code!=asio::error::try_again)break;
                        auto End=Reply.find("\r\n\r\n");
                        if(Upgrade && End!=std::string::npos && Reply.size()>End+4)break;
                    }
                    if(Code && Code!=asio::error::would_block && Code!=asio::error::try_again)break;
                    std::this_thread::yield();
                }
                asio::error_code Ignored;Socket.close(Ignored);
            }catch(const std::exception& Failure){Error=Failure.what();}
            Done=true;
        });
    }
    ~HostedPeer(){if(Worker.joinable())Worker.join();}
};
const std::string HostedRequest="GET /echo HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
void HostedBoundaryCases() {
    const std::string Handler="function(Socket) print('[LUI:HostedTest] Entered');Socket:SendAsync('accepted');Socket:Close() end";
    auto Run=[&](const std::string& Name,const std::string& Settings,const std::string& Request,int Status,bool Approved=false,int Fault=0,const std::string& CustomHandler="",const std::string& ExpectedData="") {
        Environment Owner(Name=="yielding approval" || Name=="authorization timeout",false,LUI_CAPABILITY_NETWORK_SERVER);
        if(Fault<0){auto Limits=Options();Limits.MaxReservedBytes=512*1024;Check(Lui_SetWebSocketOptionsV1(Owner.Runtime,&Limits)==1,"narrow hosted reservation budget");}
        Owner.Script("Server=app:GetService('HttpServerService'):CreateServer({Port=0,TimeoutMs=250}) "
            "Approvals=0;Server:Route('GET','/approve',function() Approvals+=1;return {Body='ok'} end);Server:RouteWebSocket('/echo',"+(CustomHandler.empty()?Handler:CustomHandler)+","+Settings+") Server:Start();print('[LUI:HostedTest] Port:'..Server.Port)");
        Check(Owner.Port!=0,"hosted fixture port");
        if(Fault>0)Owner.Runtime->WebSocketState->FaultStage=Fault;
        HostedPeer Peer(Owner.Port,Request,Status==101);bool Released=false;
        const auto Deadline=Clock::now()+std::chrono::seconds(4);
        while(!Peer.Done && Clock::now()<Deadline) {
            Owner.Turn();
            if(Owner.Authorizing && !Released) {Check(!Owner.HandlerEntered,"handler must not run before approval");Owner.Script("Permit=true");Released=true;}
            std::this_thread::yield();
        }
        Check(Peer.Done && Peer.Error.empty(),Name+" peer completed");
        if(Peer.Worker.joinable())Peer.Worker.join();
        Check(Status?Peer.Reply.find("HTTP/1.1 "+std::to_string(Status)+" ")==0:Peer.Reply.empty(),Name+" response status");
        Check(Owner.HandlerEntered==Approved,Name+" handler boundary");
        if(!ExpectedData.empty())Check(Peer.Reply.find(ExpectedData)!=std::string::npos,Name+" exact transferred suffix payload");
        if(Name!="yielding approval")Owner.Script("assert(Approvals==0)");
        if(Status==426)Check(Peer.Reply.find("Sec-WebSocket-Version: 13\r\n")!=std::string::npos,"supported version response");
        Check(Owner.Error.empty(),Name+" generic rejection must not fail host");Owner.Destroy();
    };
    Run("server-only grant","{}",HostedRequest,101,true);
    Run("yielding approval","{Authorize=function() print('[LUI:HostedTest] Authorizing');local Reply=app:GetService('HttpService'):GetAsync('http://127.0.0.1:'..Server.Port..'/approve');return Reply.StatusCode==200 end}",HostedRequest,101,true);
    Run("denied authorization","{Authorize=function() return false end}",HostedRequest,403);
    Run("wrong authorization type","{Authorize=function() return 1 end}",HostedRequest,500);
    Run("authorization error","{Authorize=function() error('HOSTED_SECRET_CANARY') end}",HostedRequest,500);
    Run("authorization closes server","{Authorize=function() Server:Close();return true end}",HostedRequest,0);
    {
        Peer Stalled(false,"Valid.p12",false,true);
        Run("authorization timeout","{Authorize=function() app:GetService('HttpService'):GetAsync('http://127.0.0.1:"+std::to_string(Stalled.Port)+"/stalled');return true end}",HostedRequest,0);
    }
    auto Origin=HostedRequest.substr(0,HostedRequest.size()-2)+"Origin: https://app.example\r\n\r\n";
    Run("origin default deny","{}",Origin,403);
    Run("origin allowlist","{Origins={'https://app.example'}}",Origin,101,true);
    auto Old=HostedRequest;Old.replace(Old.find("Version: 13"),11,"Version: 12");Run("unsupported version","{}",Old,426);
    auto Smuggled=HostedRequest.substr(0,HostedRequest.size()-2)+"Content-Length: 0\r\n\r\nGET /echo HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Run("no body framing or fallback","{}",Smuggled,400);
    Run("prospective allocation fault","{}",HostedRequest,503,false,8);
    Run("reservation saturation","{}",HostedRequest,503,false,-1);
    Run("commit allocation fault","{}",HostedRequest,0,false,9);
    Run("publication allocation fault","{}",HostedRequest,101,false,10);
    Run("task admission allocation fault","{}",HostedRequest,101,false,11);
    Run("masked read suffix","{}",HostedRequest+std::string("\x81\x82\x01\x02\x03\x04ni",8),101,true,0,
        "function(Socket) print('[LUI:HostedTest] Entered');assert(Socket:ReceiveAsync().Data=='ok');Socket:SendAsync('suffix-ok');Socket:Close() end","suffix-ok");
    Run("HTTP suffix never routes again","{}",HostedRequest+"GET /approve HTTP/1.1\r\nHost: localhost\r\n\r\n",101,true);
}
void HostedRouteValidation() {
    Environment Owner(false,false,LUI_CAPABILITY_NETWORK_SERVER);
    Owner.Script(R"(
        local Server=app:GetService('HttpServerService'):CreateServer({Port=0})
        local Handler=function() end
        for _,Options in {{Protocols={string.rep('x',129)}},{Origins={string.rep('x',2097152)}},
            {Protocols={'same','same'}},{Protocols={[2]='sparse'}},{Origins={'https://a.example/'}},
            {MaxMessageBytes=0},{MaxFrameBytes=1048577},{MaxQueuedMessages=17},{Authorize=true}} do
            assert(not pcall(function() Server:RouteWebSocket('/invalid',Handler,Options) end))
        end
        Server:RouteWebSocket('/invalid',Handler)
        assert(not pcall(function() Server:Route('GET','/invalid',Handler) end))
        Server:Route('GET','/http',Handler)
        assert(not pcall(function() Server:RouteWebSocket('/http',Handler) end))
        for Index=1,126 do Server:RouteWebSocket('/route'..Index,Handler) end
        assert(not pcall(function() Server:RouteWebSocket('/overflow',Handler) end))
        Server:Start();assert(not pcall(function() Server:RouteWebSocket('/late',Handler) end));Server:Close()
    )");
    Check(!Owner.Stats().LiveConnections && !Owner.Stats().ReservedBytes,"invalid routes never reserve a socket");
}
void HostedStopRetry() {
    Environment Owner(true,true,LUI_CAPABILITY_NETWORK_SERVER);
    Owner.Start("Server=app:GetService('HttpServerService'):CreateServer({Port=0});Server:RouteWebSocket('/echo',function(Socket) Held=Socket;Socket:ReceiveAsync() end);Server:Start();Client=WebSocket:ConnectAsync('ws://127.0.0.1:'..Server.Port..'/echo')");Owner.Drain();
    Owner.Runtime->WebSocketState->FaultStage=12;Owner.Script("Server:Close();assert(not Server.IsListening)");
    Check(Owner.Runtime->WebSocketState->FaultStage==0,"stop-post allocation fault consumed");
    Owner.Start("pcall(function() Client:ReceiveAsync() end);assert(Held.State=='Closed')");Owner.Drain();
    auto Stats=Owner.Stats();Check(Stats.LiveConnections==2 && Stats.ReservedBytes,"held closed sockets retain reservations");Owner.Destroy();
}
void HostedQuotaRelease() {
    Environment Owner(true,true,LUI_CAPABILITY_NETWORK_SERVER);
    Owner.Start("Server=app:GetService('HttpServerService'):CreateServer({Port=0,MaxConnections=1});Server:RouteWebSocket('/echo',function(Socket) Held=Socket;Socket:Close();Socket:ReceiveAsync() end);Server:Start();Url='ws://127.0.0.1:'..Server.Port..'/echo';Client=WebSocket:ConnectAsync(Url);Client:Close();assert(Client:ReceiveAsync()==nil);local Ok=pcall(function() WebSocket:ConnectAsync(Url) end);assert(not Ok)");
    Owner.Drain();Check(Owner.Stats().LiveConnections==2,"closed accepted handle retains hosted-session quota");
    Owner.Script("Held=nil;Client=nil");
    const auto Deadline=Clock::now()+std::chrono::seconds(3);
    while(Owner.Stats().LiveConnections && Clock::now()<Deadline){
        // Drive the DLL-owned VM's ordinary collector through its own allocator.
        // Never link/call a second static Luau VM against this runtime's state.
        Owner.Script("for Index=1,512 do local Garbage=table.create(256) end");Owner.Turn();std::this_thread::yield();
    }
    Check(!Owner.Stats().LiveConnections && !Owner.Stats().ReservedBytes,"hosted lease retirement releases both quotas");
    Owner.Start("local Socket=WebSocket:ConnectAsync(Url);Socket:Close();assert(Socket:ReceiveAsync()==nil);Server:Close()");Owner.Drain();
}
void HostedCancelAtCommit() {
    Environment Owner(false,false,LUI_CAPABILITY_NETWORK_SERVER);
    Owner.Script("Server=app:GetService('HttpServerService'):CreateServer({Port=0});Server:RouteWebSocket('/echo',function() print('[LUI:HostedTest] Entered') end);Server:Start();print('[LUI:HostedTest] Port:'..Server.Port)");
    auto State=Owner.Runtime->WebSocketState;State->FaultStage=13;
    struct ReleaseBarrier {
        std::shared_ptr<WebSocketRuntimeState> State;
        void Release(){std::lock_guard<std::mutex> Lock(State->QualificationMutex);State->QualificationCommitReleased=true;State->QualificationChanged.notify_all();}
        ~ReleaseBarrier(){Release();}
    } Barrier{State};
    HostedPeer Peer(Owner.Port,HostedRequest);
    bool Entered=false;const auto Deadline=Clock::now()+std::chrono::seconds(3);
    while(!Entered && Clock::now()<Deadline) {
        Owner.Turn();std::unique_lock<std::mutex> Lock(State->QualificationMutex);Entered=State->QualificationCommitEntered;
        if(!Entered)State->QualificationChanged.wait_for(Lock,std::chrono::milliseconds(1));
    }
    Check(Entered && !Owner.HandlerEntered,"controlled pre-101 commit barrier");
    // This must execute on the owner while the qualification-only worker barrier
    // is held. Closing invalidates commit without waiting for that worker.
    Owner.Script("Server:Close();Server:Close();assert(not Server.IsListening);Responsive=true");
    {std::lock_guard<std::mutex> Lock(State->QualificationMutex);Check(!State->QualificationCommitTimedOut,"owner canceled while commit barrier remained held");}
    Barrier.Release();
    while(!Peer.Done && Clock::now()<Deadline){Owner.Turn();std::this_thread::yield();}
    if(Peer.Worker.joinable())Peer.Worker.join();
    Check(Peer.Done && Peer.Reply.empty() && !Owner.HandlerEntered && Owner.Error.empty(),"cancel before commit never writes101 or publishes handler");
    Owner.Destroy();
}
void Configuration(){
    Environment Owner;auto Value=Options();
    auto Reject=[&](LuiWebSocketOptionsV1 Input){Check(!Lui_SetWebSocketOptionsV1(Owner.Runtime,&Input),"invalid configuration accepted");};
    Value.StructSize--;Reject(Value);Value=Options();Value.AbiVersion++;Reject(Value);
    Value=Options();Value.Reserved=1;Reject(Value);Value=Options();Value.MaxConnections=9;Reject(Value);
    Value=Options();Value.MaxMessageBytes++;Reject(Value);Value=Options();Value.MaxFrameBytes++;Reject(Value);
    Value=Options();Value.MaxQueuedBytes++;Reject(Value);Value=Options();Value.MaxQueuedMessages=17;Reject(Value);
    Value=Options();Value.MaxReservedBytes++;Reject(Value);Value=Options();Value.MaxConnections=0;Reject(Value);
    Value=Options();bool Result=true;std::thread Wrong([&]{Result=Lui_SetWebSocketOptionsV1(Owner.Runtime,&Value);LuiWebSocketStatsV1 Stats{sizeof(Stats),1};Check(!Lui_GetWebSocketStatsV1(Owner.Runtime,&Stats),"wrong owner query");});Wrong.join();Check(!Result,"wrong owner config");
    Check(Lui_SetWebSocketOptionsV1(Owner.Runtime,&Value)==1,"valid configuration after refusals");
    Check(!Lui_SetWebSocketOptionsV1(Owner.Runtime,&Value),"duplicate configuration");
    Owner.Script("local Service=app:GetService('WebSocketService') assert(Service) assert(not pcall(function() Service:ConnectAsync('ws://localhost/') end))");
    Check(!Owner.Runtime->Network&&!Owner.Stats().LiveConnections,"unused service starts worker or owns socket");
    Environment Denied(false);Denied.Script("assert(not pcall(function() app:GetService('WebSocketService') end))");Check(!Denied.Runtime->Network,"denied service initializes network");
}
void Exchange(bool Secure){
    Peer Server(Secure);Environment Owner;
    Owner.Start("local Socket=WebSocket:ConnectAsync('"+Server.Url()+"') assert(Socket.State=='Open') "
        "Socket:SendAsync('hello') local M=Socket:ReceiveAsync() assert(M.Data=='hello' and not M.Binary) "
        "local B=buffer.fromstring('a\\0b') task.defer(function() buffer.fill(B,0,90) end) Socket:SendAsync(B,true) M=Socket:ReceiveAsync() assert(M.Data=='a\\0b' and M.Binary) "
        "Socket:Close(1000,'done') assert(Socket:ReceiveAsync()==nil)");
    Owner.Drain();auto Stats=Owner.Stats();Check(Stats.Accepted==1&&Stats.ReceivedMessages==2&&Stats.SentMessages==2,"message metrics");
    Check(!Stats.InboundBytes&&!Stats.OutboundBytes&&!Stats.PendingSends,"settled queue accounting");
    {std::lock_guard<std::mutex> Lock(Server.Mutex);Check(!Server.InvalidClientFrame&&Server.Messages==2,"client masking and FIFO writes");}
}
void BoundaryPayload(){
    Peer Server;Environment Owner;
    Owner.Start("local Socket=WebSocket:ConnectAsync('"+Server.Url()+"') local Limit=1024*1024 local Bytes=buffer.create(Limit) "
        "buffer.fill(Bytes,0,167) task.defer(function() buffer.fill(Bytes,0,0) end) Socket:SendAsync(Bytes) "
        "local Message=Socket:ReceiveAsync() assert(Message.Binary and Message.Data==string.rep(string.char(167),Limit)) "
        "local Ok,Error=pcall(function() Socket:SendAsync(buffer.create(Limit+1)) end) assert(not Ok and string.find(Error,'MessageTooLarge',1,true)) "
        "Socket:SendAsync('valid') assert(Socket:ReceiveAsync().Data=='valid') Socket:Close() assert(Socket:ReceiveAsync()==nil)");
    Owner.Drain();auto Stats=Owner.Stats();Check(Stats.SentMessages==2&&Stats.ReceivedMessages==2,"boundary exchange and rejection preserve state");
}
void TlsFailure(const char* Credential,bool Trust=true){
    Peer Server(true,Credential);Environment Owner(true,Trust);
    Owner.Start("local Ok,Error=pcall(function() WebSocket:ConnectAsync('"+Server.Url()+"') end) assert(not Ok and string.find(Error,'CertificateRejected',1,true),tostring(Error))");
    Owner.Drain();{std::lock_guard<std::mutex> Lock(Server.Mutex);Check(!Server.Upgrades,"upgrade before certificate validation");}
}
void CancelStage(bool Tls){
    Peer Server(Tls,"Valid.p12",Tls,!Tls);Environment Owner;
    Owner.Script("Token=app:GetService('HttpService'):CreateCancellationToken() task.spawn(function() local Ok,Error=pcall(function() app:GetService('WebSocketService'):ConnectAsync('"+Server.Url()+"',{CancellationToken=Token}) end) assert(not Ok and string.find(Error,'Canceled',1,true),tostring(Error)) print('[LUI:WebSocketTest] Done') end)");
    Owner.Turn();Check(Tls?Server.WaitTls():Server.Wait(true),"controlled connection stage");
    Owner.Script("Token:Cancel() Token:Cancel()");Owner.Drain();
}
void Shutdown(bool Tls){
    Peer Server(Tls,"Valid.p12",Tls,!Tls);Environment Owner;
    Owner.Start("WebSocket:ConnectAsync('"+Server.Url()+"') error('late callback')");Owner.Turn();Check(Tls?Server.WaitTls():Server.Wait(true),"shutdown stage barrier");Owner.Destroy();
    Check(Owner.Error.empty()&&!Owner.Done,"retired callback delivered");
}
void Budgets(){
    Peer Server;Environment Owner;auto Value=Options();Value.MaxConnections=1;
    Check(Lui_SetWebSocketOptionsV1(Owner.Runtime,&Value)==1,"narrow connection budget");
    Owner.Start("Held=WebSocket:ConnectAsync('"+Server.Url()+"') Held:Close() assert(Held:ReceiveAsync()==nil) "
        "local Ok,Error=pcall(function() WebSocket:ConnectAsync('"+Server.Url()+"') end) assert(not Ok and string.find(Error,'ResourceExhausted',1,true))");
    Owner.Drain();auto Stats=Owner.Stats();Check(Stats.LiveConnections==1&&Stats.ReservedBytes&&Stats.Rejected>=1,"retained closed handle budget");
    Environment Tight;Value=Options();Value.MaxReservedBytes=512*1024;
    Check(Lui_SetWebSocketOptionsV1(Tight.Runtime,&Value)==1,"tight aggregate configuration");
    Tight.Start("local Ok,Error=pcall(function() WebSocket:ConnectAsync('"+Server.Url()+"') end) assert(not Ok and string.find(Error,'ResourceExhausted',1,true))");Tight.Drain();Check(!Tight.Runtime->Network&&!Tight.Stats().ReservedBytes,"aggregate refusal before allocation/worker");
}
void Policy(){
    Peer Server;Environment Owner;const std::string Origin="http://127.0.0.1:"+std::to_string(Server.Port);
    LuiHttpOriginV1 Entry{Origin.data(),uint32_t(Origin.size()),0};
    LuiHttpOptionsV1 Http{sizeof(Http),1,0,1024*1024,1024*1024,8,64ull*1024*1024,&Entry,1,0};
    Check(Lui_SetHttpOptionsV1(Owner.Runtime,&Http)==1,"mapped origin configuration");
    Owner.Start("local Socket=WebSocket:ConnectAsync('"+Server.Url()+"') Socket:Cancel() "
        "local Ok,Error=pcall(function() WebSocket:ConnectAsync('ws://localhost:1/') end) assert(not Ok and string.find(Error,'PolicyDenied',1,true)) "
        "Ok,Error=pcall(function() WebSocket:ConnectAsync('wss://127.0.0.1:"+std::to_string(Server.Port)+"/') end) assert(not Ok and string.find(Error,'PolicyDenied',1,true))");Owner.Drain();
    Environment Public;Http= {sizeof(Http),1,LUI_HTTP_PUBLIC_ONLY,1024*1024,1024*1024,8,64ull*1024*1024,nullptr,0,0};
    Check(Lui_SetHttpOptionsV1(Public.Runtime,&Http)==1,"public address configuration");
    Public.Start("for _,Host in {'127.0.0.1','[::ffff:127.0.0.1]','localhost','169.254.169.254'} do local Ok,Error=pcall(function() WebSocket:ConnectAsync('ws://'..Host..'/',{ConnectTimeoutMs=500}) end) assert(not Ok and string.find(Error,'PolicyDenied',1,true),tostring(Error)) end");Public.Drain();
    Environment Ports;LuiNetworkPolicyV1 Network{sizeof(Network),1,LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY,1,1,0,0};
    Check(Lui_SetNetworkPolicy(Ports.Runtime,&Network)==1,"legacy network policy");
    Ports.Start("local Ok,Error=pcall(function() WebSocket:ConnectAsync('"+Server.Url()+"') end) assert(not Ok and string.find(Error,'PolicyDenied',1,true),tostring(Error))");Ports.Drain();
}
void QueuedOwnership(){
    Peer Server;Environment Owner;
    Owner.Start("Socket=WebSocket:ConnectAsync('"+Server.Url()+"')");Owner.Drain();
    Owner.Start("Socket:SendAsync('owned')");Owner.Turn();Check(Server.WaitMessage(),"queued-result peer barrier");
    auto Stats=Owner.Stats();Check(Stats.PendingSends==1&&Stats.OutboundBytes==5&&Stats.ReservedBytes,"ownership survives undrained result");
    Owner.Drain();Owner.Destroy();
}
void FaultRecovery(){
    Peer Server;
    for(int Stage : {1,4,5,6}) {
        Environment Owner;auto Limits=Options();
        Check(Lui_SetWebSocketOptionsV1(Owner.Runtime,&Limits)==1,"fault configuration");
        Owner.Runtime->WebSocketState->FaultStage=Stage;
        Owner.Start("local Ok,Error=pcall(function() WebSocket:ConnectAsync('"+Server.Url()+"') end) assert(not Ok and string.find(Error,'ResourceExhausted',1,true),tostring(Error)) "
            "local Socket=WebSocket:ConnectAsync('"+Server.Url()+"') Socket:Close() assert(Socket:ReceiveAsync()==nil)");
        Owner.Drain();Check(Owner.Runtime->WebSocketState->FaultStage==0,"connect fault consumed");
        Check(Owner.Runtime->PendingAsyncReferences.empty(),"connect rollback/emergency settlement");
    }
    for(int Stage : {2,3,5}) {
        Environment Owner;auto Limits=Options();Check(Lui_SetWebSocketOptionsV1(Owner.Runtime,&Limits)==1,"active fault configuration");
        Owner.Start("Socket=WebSocket:ConnectAsync('"+Server.Url()+"')");Owner.Drain();
        Owner.Runtime->WebSocketState->FaultStage=Stage;
        std::string Operation=Stage==2?"Socket:ReceiveAsync()":"Socket:SendAsync('fault')";
        std::string After=Stage==5?
            "for Index=1,100 do local Ok,Error=pcall(function() Socket:ReceiveAsync() end) assert(not Ok and string.find(Error,'ResourceExhausted',1,true),tostring(Error)) end":
            "Socket:SendAsync('recovered') assert(Socket:ReceiveAsync().Data=='recovered') Socket:Close() assert(Socket:ReceiveAsync()==nil)";
        Owner.Start("local Ok,Error=pcall(function() "+Operation+" end) assert(not Ok and string.find(Error,'ResourceExhausted',1,true),tostring(Error)) "+After);
        Owner.Drain();Check(Owner.Runtime->WebSocketState->FaultStage==0,"active fault consumed");
        Check(Owner.Runtime->PendingAsyncReferences.empty(),"active rollback/emergency settlement");
    }
}
void RetiredDialOwnership(){
    Peer Server;Environment Owner;auto Limits=Options();Limits.MaxConnections=1;
    Check(Lui_SetWebSocketOptionsV1(Owner.Runtime,&Limits)==1,"retained dial configuration");
    auto State=Owner.Runtime->WebSocketState;State->FaultStage=7;
    Owner.Start("local Ok,Error=pcall(function() WebSocket:ConnectAsync('"+Server.Url()+"') end) assert(not Ok and string.find(Error,'Canceled',1,true),tostring(Error))");
    Owner.Drain();
    // Claim the synthetic native owner, breaking the qualification-only state
    // cycle. Pin is destroyed before Owner, including on early return.
    auto Pin=std::move(State->QualificationRetainedDial);
    Check(bool(Pin),"native dial owner captured");
    auto End=Clock::now()+std::chrono::seconds(5);
    while(!State->QualificationRetiredSocket.expired()&&Clock::now()<End){Owner.Turn();std::this_thread::yield();}
    Check(State->QualificationRetiredSocket.expired(),"all socket owners retired before dial release");
    auto Stats=Owner.Stats();
    Check(Stats.LiveConnections==1&&Stats.ReservedBytes==3ull*Limits.MaxQueuedBytes+2ull*Limits.MaxMessageBytes+512*1024,
        "retired dial still owns full reservation");
    Owner.Start("local Ok,Error=pcall(function() WebSocket:ConnectAsync('"+Server.Url()+"') end) assert(not Ok and string.find(Error,'ResourceExhausted',1,true),tostring(Error))");Owner.Drain();
    Pin.reset(); // The io_context remains alive until Environment destruction.
    End=Clock::now()+std::chrono::seconds(5);
    while(State->LiveConnections&&Clock::now()<End){Owner.Turn();std::this_thread::yield();}
    Check(!State->LiveConnections&&!State->ReservedBytes,"native dial release restores quota");
    Owner.Start("local Socket=WebSocket:ConnectAsync('"+Server.Url()+"') Socket:SendAsync('restored') assert(Socket:ReceiveAsync().Data=='restored') Socket:Close() assert(Socket:ReceiveAsync()==nil)");Owner.Drain();
}
double Percentile(std::vector<double> Values,double Q){std::sort(Values.begin(),Values.end());return Values.empty()?0:Values[size_t(std::ceil(Q*Values.size()))-1];}
void HostedBenchmark() {
    Environment Owner(true,true,LUI_CAPABILITY_NETWORK_SERVER);
    Owner.Start("local Server=app:GetService('HttpServerService'):CreateServer({Port=0});Server:RouteWebSocket('/echo',function(Socket) while true do local Message=Socket:ReceiveAsync();if not Message then return end;Socket:SendAsync(Message.Data,Message.Binary) end end);Server:Start();local Socket=WebSocket:ConnectAsync('ws://127.0.0.1:'..Server.Port..'/echo');for Index=1,128 do print('[LUI:WebSocketTest] Begin');Socket:SendAsync('ok');assert(Socket:ReceiveAsync().Data=='ok');print('[LUI:WebSocketTest] Sample') end;Socket:Close();assert(Socket:ReceiveAsync()==nil);Server:Close()");
    Owner.Drain();auto Stats=Owner.Stats();Check(Owner.Samples.size()==128,"hosted benchmark samples");
    std::printf("[LUI:HostedWebSocketBenchmark] Samples=%zu RoundTripMedianMs=%.6f RoundTripP95Ms=%.6f RoundTripP99Ms=%.6f OwnerPumpP95Ms=%.6f PeakConnections=%llu PeakReservedBytes=%llu InboundBytes=%llu OutboundBytes=%llu PendingSends=%llu\n",Owner.Samples.size(),Percentile(Owner.Samples,.5),Percentile(Owner.Samples,.95),Percentile(Owner.Samples,.99),Percentile(Owner.Pumps,.95),static_cast<unsigned long long>(Stats.PeakConnections),static_cast<unsigned long long>(Stats.PeakReservedBytes),static_cast<unsigned long long>(Stats.InboundBytes),static_cast<unsigned long long>(Stats.OutboundBytes),static_cast<unsigned long long>(Stats.PendingSends));
    auto State=Owner.Runtime->WebSocketState;Owner.Destroy();
    std::printf("[LUI:HostedWebSocketBenchmark] RetiredConnections=%llu RetiredReservedBytes=%llu\n",static_cast<unsigned long long>(State->LiveConnections.load()),static_cast<unsigned long long>(State->ReservedBytes.load()));
}
void Benchmark(){
    Peer Server;std::vector<double> SetupSamples;
    for(int Index=0;Index<16;++Index) {
        Environment SetupOwner;
        SetupOwner.Start("print('[LUI:WebSocketTest] Begin') local Socket=WebSocket:ConnectAsync('"+Server.Url()+"') print('[LUI:WebSocketTest] Setup') Socket:Close() assert(Socket:ReceiveAsync()==nil)");
        SetupOwner.Drain();SetupSamples.insert(SetupSamples.end(),SetupOwner.Setup.begin(),SetupOwner.Setup.end());
    }
    Environment Owner;
    std::string Body="print('[LUI:WebSocketTest] Begin') local Socket=WebSocket:ConnectAsync('"+Server.Url()+"') print('[LUI:WebSocketTest] Setup') for Index=1,128 do print('[LUI:WebSocketTest] Begin') Socket:SendAsync('ok') local M=Socket:ReceiveAsync() assert(M.Data=='ok') print('[LUI:WebSocketTest] Sample') end Socket:Close() assert(Socket:ReceiveAsync()==nil)";
    Owner.Start(Body);Owner.Drain();auto Stats=Owner.Stats();
    Check(Owner.Samples.size()==128,"benchmark samples");
    std::printf("[LUI:WebSocketBenchmark] Samples=%zu SetupMs=%.6f MessageMedianMs=%.6f MessageP95Ms=%.6f MessageP99Ms=%.6f PumpElapsedP95Ms=%.6f PeakConnections=%llu PeakReservedBytes=%llu InboundBytes=%llu OutboundBytes=%llu PendingSends=%llu\n",Owner.Samples.size(),Owner.Setup.empty()?0:Owner.Setup[0],Percentile(Owner.Samples,.5),Percentile(Owner.Samples,.95),Percentile(Owner.Samples,.99),Percentile(Owner.Pumps,.95),static_cast<unsigned long long>(Stats.PeakConnections),static_cast<unsigned long long>(Stats.PeakReservedBytes),static_cast<unsigned long long>(Stats.InboundBytes),static_cast<unsigned long long>(Stats.OutboundBytes),static_cast<unsigned long long>(Stats.PendingSends));
    auto Begin=Clock::now();
    Owner.Done=false;
    Owner.Script("local WebSocket=app:GetService('WebSocketService') local Finished,Connected=0,0 local Sockets={} "
        "for Index=1,7 do task.spawn(function() Sockets[Index]=WebSocket:ConnectAsync('"+Server.Url()+"') Connected+=1 "
        "if Connected==7 then for _,S in Sockets do task.spawn(function() S:SendAsync('ok') assert(S:ReceiveAsync().Data=='ok') S:Close() assert(S:ReceiveAsync()==nil) Finished+=1 if Finished==7 then print('[LUI:WebSocketTest] Done') end end) end end end) end");Owner.Drain();
    auto BatchMs=std::chrono::duration<double,std::milli>(Clock::now()-Begin).count();Stats=Owner.Stats();
    std::printf("[LUI:WebSocketBenchmark] SetupSamples=%zu SetupMedianMs=%.6f SetupP95Ms=%.6f SetupP99Ms=%.6f ConcurrentAdditional=7 BatchOwnerReturnMs=%.6f PeakConnections=%llu PeakReservedBytes=%llu\n",SetupSamples.size(),Percentile(SetupSamples,.5),Percentile(SetupSamples,.95),Percentile(SetupSamples,.99),BatchMs,static_cast<unsigned long long>(Stats.PeakConnections),static_cast<unsigned long long>(Stats.PeakReservedBytes));
    auto Resources=Owner.Runtime->WebSocketState;Owner.Destroy();
    std::printf("[LUI:WebSocketBenchmark] RetiredConnections=%llu RetiredReservedBytes=%llu RetiredInboundBytes=%llu RetiredOutboundBytes=%llu RetiredPendingSends=%llu\n",static_cast<unsigned long long>(Resources->LiveConnections.load()),static_cast<unsigned long long>(Resources->ReservedBytes.load()),static_cast<unsigned long long>(Resources->InboundBytes.load()),static_cast<unsigned long long>(Resources->OutboundBytes.load()),static_cast<unsigned long long>(Resources->PendingSends.load()));
}
}
int main(int Count,char** Args){
    Check(Lui_WebSocketLayoutProof()==1,"C host layout proof");
    if(Count==2&&!std::strcmp(Args[1],"--benchmark"))Benchmark();
    else if(Count==2&&!std::strcmp(Args[1],"--server-benchmark"))HostedBenchmark();
    else {Configuration();HostedRouteValidation();HostedEcho();HostedEcho(true);HostedBoundaryCases();HostedStopRetry();HostedQuotaRelease();HostedCancelAtCommit();Exchange(false);Exchange(true);BoundaryPayload();TlsFailure("WrongName.p12");TlsFailure("Expired.p12");TlsFailure("Valid.p12",false);CancelStage(false);CancelStage(true);Shutdown(false);Shutdown(true);Budgets();Policy();QueuedOwnership();FaultRecovery();RetiredDialOwnership();}
    std::printf("[LUI:WebSocketTest] Failures=%d\n",Failures);return Failures?1:0;
}
