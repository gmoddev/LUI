#include "Serializer.h"
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
void Check(bool Condition, const std::string& Name) {
    if (!Condition) { ++Failures; std::fprintf(stderr, "[LUI:HttpTest] %s\n", Name.c_str()); }
}
void Serialization() {
    Message Input;
    Input.Method = "POST";
    Input.Target = "/items?x=%20";
    Input.Headers = {{"Content-Type", "application/octet-stream"}, {"X-Meta", "a"}, {"X-Meta", "b"}};
    Input.Body = std::string("a\0b", 3);
    std::string Wire;
    Check(SerializeRequest(Input, "[::1]:8080", Wire) == ErrorCode::None, "request serialization failed");
    Parser Request(MessageKind::Request);
    Check(Request.Feed(Wire).Status == ParseStatus::Complete && Request.GetResult()->Body == Input.Body &&
        !Request.GetResult()->KeepAlive && Request.GetResult()->Headers.size() == 6, "request serialization changed data");
    for (const auto& Name : {"hOSt", "Content-Length", "Transfer-Encoding", "Connection", "Trailer", "TE", "Expect", "Upgrade"}) {
        Input.Headers = {{Name, "x"}};
        Check(SerializeRequest(Input, "localhost", Wire) == ErrorCode::InvalidFraming && Wire.empty(), "managed request field accepted");
    }
    Input.Headers = {{"X-Note", "safe\r\nContent-Length: 99"}};
    Check(SerializeRequest(Input, "localhost", Wire) == ErrorCode::InvalidHeader && Wire.empty(), "header injection accepted");
    Input.Headers.clear();
    Input.Method = "GET";
    Check(SerializeRequest(Input, "localhost", Wire) == ErrorCode::UnsupportedFeature, "GET content accepted");
    Input.Body.clear();
    Input.Method = "POST";
    Check(SerializeRequest(Input, "localhost\r\nX: yes", Wire) == ErrorCode::InvalidStartLine, "authority injection accepted");
    Limits Bounds;
    Bounds.HeaderCount = 1;
    Check(SerializeRequest(Input, "localhost", Wire, Bounds) == ErrorCode::LimitExceeded, "generated headers ignored count limit");
    Message Response;
    Response.StatusCode = 200;
    Response.Reason = "OK";
    Response.Body = std::string("a\0b", 3);
    Response.Headers = {{"Set-Cookie", "a=1"}, {"Set-Cookie", "b=2"}};
    Check(SerializeResponse(Response, Wire) == ErrorCode::None, "response serialization failed");
    Parser Parsed(MessageKind::Response);
    Check(Parsed.Feed(Wire).Status == ParseStatus::Complete && Parsed.GetResult()->Body == Response.Body &&
        Parsed.GetResult()->Headers[1].Name == "set-cookie", "response fields/body changed");
    Check(SerializeResponse(Response, Wire, true) == ErrorCode::None, "HEAD serialization failed");
    Parser Head(MessageKind::Response, {}, "HEAD");
    auto Result = Head.Feed(Wire);
    Check(Result.Status == ParseStatus::Complete && Result.Consumed == Wire.size() && Head.GetResult()->Body.empty(), "HEAD leaked content");
    for (unsigned Status : {204, 205, 304}) {
        Response.StatusCode = Status;
        Check(SerializeResponse(Response, Wire) == ErrorCode::InvalidFraming, "body-free status accepted content");
        Response.Body.clear();
        Check(SerializeResponse(Response, Wire) == ErrorCode::None, "body-free status rejected");
        Parser Empty(MessageKind::Response);
        Check(Empty.Feed(Wire).Status == ParseStatus::Complete, "body-free output not parseable");
        Response.Body = "x";
    }
    Response.StatusCode = 200;
    Response.Reason = "OK\r\nX: yes";
    Check(SerializeResponse(Response, Wire) == ErrorCode::InvalidStartLine && Wire.empty(), "reason injection accepted");
    Url Address;
    Check(ParseUrl("http://localhost:8080?x=%20", Address) == ErrorCode::None && Address.Host == "localhost" &&
        Address.Port == 8080 && Address.Target == "/?x=%20", "query-only URL parsing failed");
    Check(ParseUrl("http://[::1]/", Address) == ErrorCode::None && Address.Host == "::1", "IPv6 URL rejected");
    for (const auto& Text : {"https://localhost/", "http://a/#frag", "http://u:p@a/", "http://a:0/", "http://a:65536/",
        "http://a:/", "http://[::1]:/", "http://a:99999999999/", "http://a/\r\nHost:b", "http://a/%xx", "http://a/with space"})
        Check(ParseUrl(Text, Address) != ErrorCode::None, std::string("invalid URL accepted: ") + Text);
}

// Bounded native peer lets the Luau client run with network.client alone.
struct Peer {
    asio::io_context Io;
    Tcp::acceptor Acceptor{Io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)};
    unsigned short Port = Acceptor.local_endpoint().port();
    std::atomic<bool> Stop{false};
    std::thread Worker;
    std::string RequestBody, Target, Error;
    std::vector<Field> Headers;
    Peer(std::string Reply, bool Stall = false, bool Fragment = false) {
        Worker = std::thread([this, Reply = std::move(Reply), Stall, Fragment] {
            asio::error_code Code;
            Acceptor.non_blocking(true, Code);
            Tcp::socket Socket(Io);
            auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!Stop && std::chrono::steady_clock::now() < End) {
                Acceptor.accept(Socket, Code);
                if (!Code) break;
                if (Code != asio::error::would_block && Code != asio::error::try_again) { Error = Code.message(); return; }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!Socket.is_open()) return;
            Socket.non_blocking(true, Code);
            Parser Input(MessageKind::Request);
            std::array<char, 4096> Bytes;
            while (!Stop && std::chrono::steady_clock::now() < End) {
                size_t Count = Socket.read_some(asio::buffer(Bytes), Code);
                if (Code == asio::error::would_block || Code == asio::error::try_again) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue;
                }
                if (Code) return;
                auto Result = Input.Feed({Bytes.data(), Count});
                if (Result.Status == ParseStatus::Failed) { Error = GetErrorName(Result.Error); return; }
                if (Result.Status != ParseStatus::Complete) continue;
                RequestBody = Input.GetResult()->Body;
                Target = Input.GetResult()->Target;
                Headers = Input.GetResult()->Headers;
                if (Stall) {
                    while (!Stop && std::chrono::steady_clock::now() < End) {
                        Socket.read_some(asio::buffer(Bytes), Code);
                        if (Code == asio::error::eof || Code == asio::error::connection_reset) return;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    return;
                }
                // Replies are small; retain the same bounded nonblocking deadline for writes.
                size_t Offset = 0;
                while (Offset < Reply.size() && !Stop && std::chrono::steady_clock::now() < End) {
                    size_t Count = Socket.write_some(asio::buffer(Reply.data() + Offset, Fragment ? 1 : Reply.size() - Offset), Code);
                    if (Code == asio::error::would_block || Code == asio::error::try_again) continue;
                    if (Code) return; // The client may deliberately reject and close early.
                    Offset += Count;
                }
                return; // Destruction supplies EOF for close-delimited/truncated tests.
            }
        });
    }
    void Join() { Stop = true; if (Worker.joinable()) Worker.join(); }
    ~Peer() { Join(); }
};
struct Logs { bool Done = false; std::string Error; };
void LUI_CALL OnLog(void* Context, const char* Level, const char* Text) {
    auto& Result = *static_cast<Logs*>(Context);
    if (std::string_view(Level) == "Error") Result.Error = Text;
    if (std::string_view(Text) == "[LUI:HttpTest] ClientDone") Result.Done = true;
}
LuiRuntime* Client(Logs& Result) {
    auto* Runtime = Lui_Create();
    Lui_SetLogCallback(Runtime, &Result, OnLog);
    LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION, LUI_CAPABILITY_NETWORK_CLIENT};
    Check(Lui_DeclareCapabilities(Runtime, &Grant) == 1, "client grant failed");
    return Runtime;
}
void Run(Peer& Server, const std::string& Body, bool Policy = false) {
    Logs Result;
    auto* Runtime = Client(Result);
    if (Policy) {
        LuiNetworkPolicyV1 Rules{sizeof(Rules), LUI_EXTENSION_ABI_VERSION, LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY,
            Server.Port, Server.Port, 0, 0};
        Check(Lui_SetNetworkPolicy(Runtime, &Rules) == 1, "HTTP host policy rejected");
    }
    std::string Script = "local Http = app:GetService('HttpService')\n"
        "assert(not pcall(function() app:GetService('NetworkService') end))\n"
        "local Url = 'http://127.0.0.1:" + std::to_string(Server.Port) + "/test?x=%20'\n" + Body;
    Check(Lui_RunScript(Runtime, Script.c_str(), "HttpClient") == 1, Lui_GetLastError(Runtime));
    auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!Result.Done && Result.Error.empty() && std::chrono::steady_clock::now() < End) {
        Lui_Pump(Runtime);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(Result.Done && Result.Error.empty(), "client fixture: " + Result.Error + "\n" + Body);
    Lui_Destroy(Runtime);
    Server.Join();
    Check(Server.Error.empty(), "native fixture: " + Server.Error);
}
std::string Await(const std::string& Body) {
    return "task.spawn(function()\n" + Body + "\nprint('[LUI:HttpTest] ClientDone')\nend)";
}
void Clients() {
    auto* Denied = Lui_Create();
    Check(Lui_RunScript(Denied, "assert(not pcall(function() app:GetService('HttpService') end))", "HttpDenied") == 1,
        "HTTP service available without client grant");
    Lui_Destroy(Denied);
    std::string Reply = "HTTP/1.1 103 Early Hints\r\nLink: </style>\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 3\r\n"
        "Set-Cookie: a=1\r\nSet-Cookie: b=2\r\n\r\n";
    Reply += std::string("a\0b", 3);
    Peer RoundTrip(Reply, false, true);
    Run(RoundTrip, Await(R"(
        local R = Http:RequestAsync({Url = Url, Method = 'POST', Body = buffer.fromstring('p\0q'),
            Headers = {{Name = 'X-Order', Value = 'a'}, {Name = 'X-Order', Value = 'b'}}})
        assert(R.Success and R.StatusCode == 200 and R.Body == 'a\0b')
        assert(R.Headers[2].Name == 'set-cookie' and R.Headers[3].Value == 'b=2')
        assert(not pcall(function() R.Body = 'changed' end))
        assert(not pcall(function() R.Headers[1].Value = 'changed' end))
    )"), true);
    Check(RoundTrip.RequestBody == std::string("p\0q", 3) && RoundTrip.Target == "/test?x=%20", "outbound body/target changed");
    Check(RoundTrip.Headers.size() >= 3 && RoundTrip.Headers[1].Value == "a" && RoundTrip.Headers[2].Value == "b", "outbound duplicate fields changed");
    Peer Chunked("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTrailer: Server-Timing\r\n\r\n2\r\nOK\r\n0\r\nServer-Timing: dur=1\r\n\r\n");
    Run(Chunked, Await("local R = Http:GetAsync(Url) assert(R.Body == 'OK' and R.Trailers[1].Name == 'server-timing')"));
    Peer Close("HTTP/1.1 404 Missing\r\n\r\nno");
    Run(Close, Await("local R = Http:GetAsync(Url) assert(not R.Success and R.StatusCode == 404 and R.Body == 'no')"));
    Peer Head("HTTP/1.1 200 OK\r\nContent-Length: 999999999\r\n\r\n");
    Run(Head, Await("local R = Http:RequestAsync({Url=Url, Method='HEAD', MaxResponseBytes=0}) assert(R.Body == '')"));
    for (const auto& Fixture : std::vector<std::pair<std::string, std::string>>{
        {"HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc", "InvalidFraming"},
        {"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na", "UnexpectedEof"},
        {"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc", "LimitExceeded"},
        {"HTTP/1.1 103 Hints\r\n\r\n", "UnexpectedEof"},
        {"HTTP/1.1 101 Switching\r\n\r\n", "UnsupportedFeature"}}) {
        Peer Invalid(Fixture.first);
        Run(Invalid, Await("local Ok, Error = pcall(function() return Http:RequestAsync({Url=Url, MaxResponseBytes=" +
            std::string(Fixture.second == "UnexpectedEof" ? "8" : "2") + "}) end) "
            "assert(not Ok and string.find(Error, '[LUI:Http] " + Fixture.second + "', 1, true))"));
    }
    Peer Slow("", true);
    Run(Slow, Await("local Ok, Error = pcall(function() Http:RequestAsync({Url=Url, TimeoutMs=30}) end) "
        "assert(not Ok and string.find(Error, 'TimedOut', 1, true))"));
    std::string Hints;
    for (int Index = 0; Index < 9; ++Index) Hints += "HTTP/1.1 103 Hints\r\n\r\n";
    Peer TooManyHints(Hints);
    Run(TooManyHints, Await("local Ok, Error = pcall(function() Http:GetAsync(Url) end) "
        "assert(not Ok and string.find(Error, 'LimitExceeded', 1, true))"));
    Peer Canceled("", true);
    Run(Canceled, "task.spawn(function() local Ok, Error = pcall(function() Http:GetAsync(Url) end) "
        "assert(not Ok and string.find(Error, 'Canceled', 1, true)) print('[LUI:HttpTest] ClientDone') end) "
        "task.defer(function() for Index=1,1000 do Http:CancelAll() end end)");
    Peer Quota("", true);
    Run(Quota, R"(
        local Finished = 0
        for Index = 1, 33 do
            task.spawn(function()
                local Ok, Error = pcall(function() Http:GetAsync(Url) end)
                assert(not Ok)
                if Index == 33 then assert(string.find(Error, 'TooManyRequests', 1, true)) end
                Finished += 1
                if Finished == 33 then print('[LUI:HttpTest] ClientDone') end
            end)
        end
        task.defer(function() Http:CancelAll() end)
    )");
    Peer Policy("", true);
    Run(Policy, Await(R"(
        assert(not pcall(function() Http:GetAsync('http://192.0.2.1/') end))
        assert(not pcall(function() Http:GetAsync('http://127.0.0.1:1/') end))
        assert(not pcall(function() Http:GetAsync('https://localhost/') end))
        assert(not pcall(function() Http:RequestAsync({Url=Url, Headers={{Name='Content-Length', Value='4'}}}) end))
        assert(not pcall(function() Http:RequestAsync({Url=Url, Body='a', Method='GET'}) end))
        assert(not pcall(function() Http:RequestAsync({Url=Url, TimeoutMs=0}) end))
        assert(not pcall(function() Http:RequestAsync({Url=Url, Headers={Authorization='secret'}}) end))
    )"), true);
    Peer Later("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    Peer Earlier("", true);
    Run(Later, "task.spawn(function() pcall(function() Http:GetAsync('http://127.0.0.1:" +
        std::to_string(Earlier.Port) + "/') end) end) " + R"(
        task.defer(function()
            for Index = 1, 1000 do Http:CancelAll() end
            task.spawn(function()
                local Response = Http:GetAsync(Url)
                assert(Response.Body == 'OK')
                print('[LUI:HttpTest] ClientDone')
            end)
        end)
    )");
    // Runtime destruction cancels ownership without invoking callbacks after teardown.
    Peer Teardown("", true);
    Logs Result;
    auto* Runtime = Client(Result);
    std::string Script = "task.spawn(function() app:GetService('HttpService'):GetAsync('http://127.0.0.1:" +
        std::to_string(Teardown.Port) + "/') error('unexpected resume after teardown') end)";
    Check(Lui_RunScript(Runtime, Script.c_str(), "HttpTeardown") == 1, "teardown fixture failed");
    Lui_Pump(Runtime);
    Lui_Destroy(Runtime);
    Check(Result.Error.empty(), "runtime teardown resumed a task");
}
}
int HttpClientTests() { Serialization(); Clients(); return Failures; }
