#include "Parser.h"
#include "LuiRuntime.h"
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace Lui::Http;
using Tcp = asio::ip::tcp;
namespace {
int Failures = 0;
void Check(bool Value, const std::string& Name) {
    if (!Value) { ++Failures; std::fprintf(stderr, "[LUI:HttpServerTest] %s\n", Name.c_str()); }
}
struct Logs {
    unsigned short Port = 0;
    std::vector<std::string> Errors;
    bool Late = false;
    bool Awaiting = false;
};
void LUI_CALL OnLog(void* Context, const char* Level, const char* Text) {
    auto& Result = *static_cast<Logs*>(Context);
    std::string_view Value(Text), Prefix = "[LUI:HttpServerTest] Port:";
    if (Value.substr(0, Prefix.size()) == Prefix) Result.Port = static_cast<unsigned short>(std::stoi(std::string(Value.substr(Prefix.size()))));
    if (std::string_view(Level) == "Error") Result.Errors.emplace_back(Text);
    if (Value == "[LUI:HttpServerTest] Late") Result.Late = true;
    if (Value == "[LUI:HttpServerTest] Awaiting") Result.Awaiting = true;
}
struct Client {
    asio::io_context Io;
    Tcp::socket Socket{Io};
    std::thread Worker;
    std::atomic<bool> Done{false}, Stop{false};
    std::vector<Message> Replies;
    std::string Error;
    Client(unsigned short Port, std::string Wire, std::vector<std::string> Methods = {}) {
        Worker = std::thread([this, Port, Wire = std::move(Wire), Methods = std::move(Methods)] {
            asio::error_code Code;
            Socket.connect(Tcp::endpoint(asio::ip::make_address("127.0.0.1"), Port), Code);
            if (Code) { Error = Code.message(); Done = true; return; }
            Socket.non_blocking(true, Code);
            auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            size_t Offset = 0;
            while (Offset < Wire.size() && !Stop && std::chrono::steady_clock::now() < End) {
                size_t Count = Socket.write_some(asio::buffer(Wire.data() + Offset, Wire.size() - Offset), Code);
                if (Code == asio::error::would_block || Code == asio::error::try_again) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue;
                }
                if (Code) break;
                Offset += Count;
            }
            std::unique_ptr<Parser> Input;
            auto Reset = [&] { Input = std::make_unique<Parser>(MessageKind::Response, Limits{},
                Replies.size() < Methods.size() ? Methods[Replies.size()] : "GET"); };
            Reset();
            std::array<char, 4096> Buffer;
            while (!Stop && std::chrono::steady_clock::now() < End) {
                size_t Count = Socket.read_some(asio::buffer(Buffer), Code);
                if (Code == asio::error::would_block || Code == asio::error::try_again) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue;
                }
                bool Eof = Code == asio::error::eof || Code == asio::error::connection_reset;
                if (Code && !Eof) { Error = Code.message(); break; }
                std::string_view Bytes(Buffer.data(), Count);
                while (!Bytes.empty()) {
                    auto Result = Input->Feed(Bytes);
                    Bytes.remove_prefix(Result.Consumed);
                    if (Result.Status == ParseStatus::Failed) { Error = GetErrorName(Result.Error); break; }
                    if (Result.Status == ParseStatus::Complete) { Replies.push_back(*Input->GetResult()); Reset(); }
                }
                if (!Error.empty() || Eof) { Done = true; return; }
            }
            if (!Stop) Error = "native client deadline";
            Done = true;
        });
    }
    void Join() { Stop = true; if (Worker.joinable()) Worker.join(); }
    ~Client() { Join(); }
};
bool Pump(LuiRuntime* Runtime, Client& Peer) {
    auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!Peer.Done && std::chrono::steady_clock::now() < End) {
        Lui_Pump(Runtime); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Peer.Join();
    Check(Peer.Done && Peer.Error.empty(), "native client: " + Peer.Error);
    return Peer.Done && Peer.Error.empty();
}
void Run(LuiRuntime* Runtime, const char* Script) {
    Check(Lui_RunScript(Runtime, Script, "HttpServerTest") == 1, Lui_GetLastError(Runtime));
}
std::string Request(std::string_view Target, std::string_view Method = "GET") {
    return std::string(Method) + " " + std::string(Target) + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
}
void Expect(LuiRuntime* Runtime, unsigned short Port, const std::string& Wire, unsigned Status, std::string_view Body,
    std::string Method = "GET") {
    Client Peer(Port, Wire, {Method}); Pump(Runtime, Peer);
    Check(Peer.Replies.size() == 1 && Peer.Replies[0].StatusCode == Status && Peer.Replies[0].Body == Body,
        "wrong hosted response for " + Wire.substr(0, Wire.find("\r\n")));
}
LuiRuntime* Runtime(Logs& Result, bool Outbound = true) {
    auto* Value = Lui_Create(); Lui_SetLogCallback(Value, &Result, OnLog);
    LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION,
        LUI_CAPABILITY_NETWORK_SERVER | (Outbound ? LUI_CAPABILITY_NETWORK_CLIENT : 0)};
    Check(Lui_DeclareCapabilities(Value, &Grant) == 1, "hosted grant failed");
    return Value;
}
void Hosting() {
    auto* Denied = Lui_Create();
    Run(Denied, "assert(not pcall(function() app:GetService('HttpServerService') end))"); Lui_Destroy(Denied);
    Logs ServerOnly;
    auto* Isolated = Runtime(ServerOnly, false);
    Run(Isolated, R"(
        assert(not pcall(function() app:GetService('NetworkService') end))
        assert(not pcall(function() app:GetService('HttpService') end))
        Server = app:GetService('HttpServerService'):CreateServer({Port=0})
        Server:Route('GET', '/ok', function(R)
            assert(R.Path == '/ok' and R.Method == 'GET' and R.HttpVersion == 'HTTP/1.1')
            assert(R.LocalEndpoint.Port == Server.Port and R.RemoteEndpoint.Port > 0)
            assert(not pcall(function() R.Path = 'changed' end))
            assert(not pcall(function() R.Headers[1].Value = 'changed' end))
            return {StatusCode=200, Body='OK'}
        end)
        assert(not pcall(function() Server:Route('GET', '/ok', function() return {} end) end))
        assert(not Server.IsListening and Server.Port > 0)
        assert(#Server.BoundEndpoints == 1 and Server.BoundEndpoints[1].Address == '127.0.0.1')
        Server:Start() Server:Start()
        assert(Server.IsListening)
        assert(not pcall(function() Server:Route('GET', '/late', function() return {} end) end))
        print('[LUI:HttpServerTest] Port:' .. Server.Port)
    )");
    Expect(Isolated, ServerOnly.Port, Request("/ok"), 200, "OK");
    Check(ServerOnly.Errors.empty(), "server-only handler failed");
    Run(Isolated, "Server:Close() Server:Close() assert(not Server.IsListening) assert(not pcall(function() Server:Start() end))");
    Lui_Destroy(Isolated);

    Logs Result;
    auto* Value = Runtime(Result);
    Run(Value, R"(
        local Service = app:GetService('HttpServerService')
        Inner = Service:CreateServer({Port=0})
        Inner:Route('GET', '/wait', function() return {Body='ready'} end)
        Inner:Start()
        Calls, Forbidden = 0, 0
        Scene = Instance.new('Frame', {})
        Server = Service:CreateServer({Port=0, MaxRequestBytes=32, MaxResponseBytes=32, TimeoutMs=2000})
        Server:Route('GET', '/first', function(R)
            assert(R.RawTarget == '/first?x=1' and Calls == 0)
            local Ready = app:GetService('HttpService'):GetAsync('http://127.0.0.1:' .. Inner.Port .. '/wait')
            assert(Ready.Body == 'ready' and Calls == 0)
            Calls += 1 Scene.Name = 'Owner1'
            return {Body='first'}
        end)
        Server:Route('GET', '/second', function()
            assert(Calls == 1 and Scene.Name == 'Owner1')
            Calls += 1 Scene.Name = 'Owner2'
            return {Body='second'}
        end)
        Server:Route('POST', '/echo', function(R)
            assert(R.Body == 'p\0q' and R.Trailers[1].Name == 'server-timing')
            return {Body=buffer.fromstring(R.Body), Headers={{Name='X-Meta', Value='a'}, {Name='X-Meta', Value='b'}}}
        end)
        Server:Route('HEAD', '/head', function() return {Body='representation'} end)
        Server:Route('POST', '/empty', function() return {StatusCode=205} end)
        Server:Route('GET', '/forbidden', function() Forbidden += 1 return {Body='should not run'} end)
        Server:Route('GET', '/error', function() error('PrivateDetail') end)
        Server:Route('GET', '/badreply', function() return {Headers={{Name='Content-Length', Value='5'}}} end)
        Server:Route('GET', '/large', function() return {Body=string.rep('x',33)} end)
        Server:Route('GET', '/wrongtype', function() return 'not a table' end)
        Server:Start()
        print('[LUI:HttpServerTest] Port:' .. Server.Port)
    )");
    Client Pipeline(Result.Port, "GET /first?x=1 HTTP/1.1\r\nHost: localhost\r\n\r\n" + Request("/second"));
    Pump(Value, Pipeline);
    Check(Pipeline.Replies.size() == 2 && Pipeline.Replies[0].Body == "first" && Pipeline.Replies[1].Body == "second",
        "persistent/yielding handler order changed");
    Run(Value, "assert(Calls == 2 and Scene.Name == 'Owner2')");
    std::string Chunk = "POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n"
        "Trailer: Server-Timing\r\nConnection: close\r\n\r\n3\r\n";
    Chunk += std::string("p\0q",3) + "\r\n0\r\nServer-Timing: dur=1\r\n\r\n";
    Client Echo(Result.Port, Chunk); Pump(Value, Echo);
    Check(Echo.Replies.size() == 1 && Echo.Replies[0].Body == std::string("p\0q",3) &&
        Echo.Replies[0].Headers.size() == 4 && Echo.Replies[0].Headers[1].Value == "b", "hosted binary/duplicate fields changed");
    Expect(Value, Result.Port, Request("/head","HEAD"), 200, "", "HEAD");
    Expect(Value, Result.Port, Request("/empty","POST"), 205, "");
    Expect(Value, Result.Port, Request("/missing"), 404, "Not Found");
    Expect(Value, Result.Port, Request("/missing","HEAD"), 404, "", "HEAD");
    for (const auto& Path : {"/error", "/badreply", "/large", "/wrongtype"})
        Expect(Value, Result.Port, Request(Path), 500, "Internal Server Error");
    Check(Result.Errors.size() == 4 && Result.Errors[0].find("[LUI:HttpServer] HandlerFailed:") == 0,
        "handler failure did not produce local structured diagnostics");
    for (const auto& Fields : {"Content-Length: 1\r\nTransfer-Encoding: chunked\r\n", "Content-Length: 0\r\nContent-Length: 0\r\n",
        "Host : invalid\r\n", "Expect: 100-continue\r\n"}) {
        auto Wire = "GET /forbidden HTTP/1.1\r\nHost: localhost\r\n" + std::string(Fields) + "\r\n" + Request("/forbidden");
        Expect(Value, Result.Port, Wire, 400, "Bad Request");
    }
    Expect(Value, Result.Port, "POST /forbidden HTTP/1.1\r\nHost: localhost\r\nContent-Length: 33\r\n\r\n", 413, "Payload Too Large");
    Run(Value, "assert(Forbidden == 0)");
    Run(Value, R"(
        Throttle = app:GetService('HttpServerService'):CreateServer({Port=0,MaxConnections=1,TimeoutMs=500})
        Admitted = 0
        Throttle:Route('GET','/admit',function() Admitted += 1 return {Body='unexpected'} end)
        Throttle:Start() print('[LUI:HttpServerTest] Port:' .. Throttle.Port)
    )");
    Client Holding(Result.Port, "GET /admit HTTP/1.1\r\n");
    auto Accepted = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    while (std::chrono::steady_clock::now() < Accepted) {
        Lui_Pump(Value); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Client Rejected(Result.Port, Request("/admit")); Pump(Value, Rejected);
    Check(Rejected.Replies.empty(), "connection admission bound was bypassed");
    Run(Value, "assert(Admitted == 0) Throttle:Close()"); Pump(Value, Holding);
    Run(Value, R"(
        Counter = app:GetService('HttpServerService'):CreateServer({Port=0})
        Counted = 0
        Counter:Route('GET','/count',function() Counted += 1 return {Body='OK'} end)
        Counter:Start() print('[LUI:HttpServerTest] Port:' .. Counter.Port)
    )");
    std::string Many;
    for (int Index = 0; Index < 101; ++Index) Many += "GET /count HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Client Limited(Result.Port, Many); Pump(Value, Limited);
    Check(Limited.Replies.size() == 100, "persistent connection request count was not bounded");
    Run(Value, "assert(Counted == 100) Counter:Close()");
    Run(Value, "Server:Close() Inner:Close()");
    Lui_Destroy(Value);
}

// An independent native endpoint delays completion past the hosted deadline.
struct DelayedPeer {
    asio::io_context Io;
    Tcp::acceptor Acceptor{Io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"),0)};
    unsigned short Port = Acceptor.local_endpoint().port();
    std::atomic<bool> Stop{false}, Done{false};
    std::thread Worker;
    DelayedPeer() {
        Worker = std::thread([this] {
            asio::error_code Error; Acceptor.non_blocking(true, Error);
            Tcp::socket Socket(Io);
            auto End = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!Stop && std::chrono::steady_clock::now() < End) {
                Acceptor.accept(Socket, Error);
                if (!Error) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (Socket.is_open() && !Error) {
                // Small localhost reply; nonblocking write prevents shutdown from hanging.
                auto Until = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
                while (!Stop && std::chrono::steady_clock::now() < Until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                Socket.non_blocking(true, Error);
                const std::string Reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
                Socket.write_some(asio::buffer(Reply), Error);
            }
            Done = true;
        });
    }
    ~DelayedPeer() { Stop = true; if (Worker.joinable()) Worker.join(); }
};
void DeadlinesAndShutdown() {
    Logs Result; auto* Value = Runtime(Result);
    DelayedPeer Delay;
    std::string Script = "Server=app:GetService('HttpServerService'):CreateServer({Port=0,TimeoutMs=30}) "
        "Server:Route('GET','/slow',function() app:GetService('HttpService'):GetAsync('http://127.0.0.1:" +
        std::to_string(Delay.Port) + "/') print('[LUI:HttpServerTest] Late') return {Body='late'} end) "
        "Server:Start() print('[LUI:HttpServerTest] Port:' .. Server.Port)";
    Run(Value, Script.c_str());
    Client Slow(Result.Port, Request("/slow")); Pump(Value, Slow);
    Check(Slow.Replies.empty(), "handler deadline sent a late response");
    auto End = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
    while (std::chrono::steady_clock::now() < End) { Lui_Pump(Value); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    Check(!Result.Late && Result.Errors.empty(), "expired handler resumed Luau after its await");
    Client Partial(Result.Port, "GET /slow HTTP/1.1\r\nHost: localhost\r\n"); Pump(Value, Partial);
    Check(Partial.Replies.empty(), "partial request escaped deadline");
    Run(Value, "Server:Close() Server:Close()"); Lui_Destroy(Value);

    Logs Closing; auto* Closed = Runtime(Closing);
    Run(Closed, "Server=app:GetService('HttpServerService'):CreateServer({Port=0}) Server:Start() print('[LUI:HttpServerTest] Port:' .. Server.Port)");
    Client Waiting(Closing.Port, "GET / HTTP/1.1\r\n");
    // Let accept/read enter the worker before closing, without completing the request.
    auto Wait = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    while (std::chrono::steady_clock::now() < Wait) { Lui_Pump(Closed); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    Run(Closed, "Server:Close() Server:Close()"); Pump(Closed, Waiting);
    Check(Waiting.Replies.empty(), "close dispatched an incomplete request"); Lui_Destroy(Closed);

    Logs Policy; auto* Restricted = Runtime(Policy, false);
    LuiNetworkPolicyV1 Rules{sizeof(Rules), LUI_EXTENSION_ABI_VERSION, LUI_NETWORK_POLICY_SERVER_LOOPBACK_ONLY,0,0,0,0};
    Check(Lui_SetNetworkPolicy(Restricted,&Rules) == 1, "hosted policy rejected");
    Run(Restricted, "assert(not pcall(function() app:GetService('HttpServerService'):CreateServer({Address='any',Port=0}) end))");
    Lui_Destroy(Restricted);

    for (bool Destroy : {false, true}) {
        Logs Active; auto* Hosting = Runtime(Active);
        DelayedPeer Backend;
        std::string Setup = "Server=app:GetService('HttpServerService'):CreateServer({Port=0,TimeoutMs=2000}) "
            "Server:Route('GET','/slow',function() print('[LUI:HttpServerTest] Awaiting') "
            "app:GetService('HttpService'):GetAsync('http://127.0.0.1:" + std::to_string(Backend.Port) + "/') "
            "print('[LUI:HttpServerTest] Late') return {Body='late'} end) "
            "Server:Start() print('[LUI:HttpServerTest] Port:' .. Server.Port)";
        Run(Hosting, Setup.c_str());
        Client Pending(Active.Port, Request("/slow"));
        auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!Active.Awaiting && std::chrono::steady_clock::now() < Until) {
            Lui_Pump(Hosting); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(Active.Awaiting, "close/teardown fixture did not enter the handler");
        if (Destroy) {
            Lui_Destroy(Hosting);
            while (!Pending.Done && std::chrono::steady_clock::now() < Until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            Pending.Join();
        } else {
            Run(Hosting, "Server:Close()"); Pump(Hosting, Pending);
            Until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
            while (std::chrono::steady_clock::now() < Until) {
                Lui_Pump(Hosting); std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Lui_Destroy(Hosting);
        }
        Check(Pending.Replies.empty() && !Active.Late && Active.Errors.empty(), "closed handler resumed or replied");
    }

    Logs Capacity; auto* Bounded = Runtime(Capacity, false);
    Run(Bounded, "Servers={} for Index=1,4 do Servers[Index]=app:GetService('HttpServerService'):CreateServer({Port=0}) end "
        "assert(not pcall(function() app:GetService('HttpServerService'):CreateServer({Port=0}) end))");
    Lui_Destroy(Bounded);
}
}
int HttpServerTests() { Hosting(); DeadlinesAndShutdown(); return Failures; }
