#include "LuiRuntime.h"
#include "../../runtime/network/tls/Provider.h"
#include <openssl/ssl.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>

namespace {
using Tcp = asio::ip::tcp;
int Failures = 0;
void Check(bool Ok, const std::string& Message) {
    if (!Ok) { ++Failures; std::fprintf(stderr, "[LUI:TlsTest] %s\n", Message.c_str()); }
}
std::string Fixture(const char* Name) {
    std::ifstream Input(std::string(LUI_TLS_FIXTURES) + "/" + Name, std::ios::binary);
    Check(static_cast<bool>(Input), std::string("missing fixture: ") + Name);
    return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
}
struct Logs {
    std::thread::id Owner = std::this_thread::get_id();
    bool WrongThread = false, Done = false;
    int Dispatches = 0;
    unsigned short Port = 0;
    std::string Error;
};
void LUI_CALL OnLog(void* Context, const char* Level, const char* Message) {
    auto& Result = *static_cast<Logs*>(Context);
    Result.WrongThread |= std::this_thread::get_id() != Result.Owner;
    if (std::string(Level) == "Error") Result.Error = Message;
    if (std::string(Message) == "[LUI:TlsTest] Done") Result.Done = true;
    if (std::string(Message) == "[LUI:TlsTest] Dispatch") ++Result.Dispatches;
    if (std::string(Message).find("[LUI:TlsTest] Port ") == 0)
        Result.Port = static_cast<unsigned short>(std::stoi(std::string(Message).substr(19)));
}
LuiRuntime* Runtime(Logs& Result, bool Server = false, const char* Credential = "Valid.p12", bool Trust = true) {
    auto* Owner = Lui_Create();
    Lui_SetLogCallback(Owner, &Result, OnLog);
    LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION,
        LUI_CAPABILITY_NETWORK_CLIENT | (Server ? LUI_CAPABILITY_NETWORK_SERVER : 0)};
    Check(Lui_DeclareCapabilities(Owner, &Grant) == 1, "TLS grants rejected");
    std::string Roots = Trust ? Fixture("Root.pem") : "", Pfx = Server ? Fixture(Credential) : "";
    if (Trust || Server) {
        LuiTlsOptionsV1 Options{sizeof(Options), LUI_EXTENSION_ABI_VERSION, Roots.data(),
            static_cast<uint32_t>(Roots.size()), Pfx.data(), static_cast<uint32_t>(Pfx.size()),
            Server ? "test-only" : nullptr, Server ? 9u : 0u};
        Check(Lui_SetTlsOptions(Owner, &Options) == 1, Lui_GetLastError(Owner));
    }
    return Owner;
}
void Pump(LuiRuntime* Owner, Logs& Result) {
    auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!Result.Done && Result.Error.empty() && std::chrono::steady_clock::now() < End) {
        Lui_Pump(Owner);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(Result.Done && Result.Error.empty() && !Result.WrongThread, "TLS scheduler fixture: " + Result.Error);
}
enum class PeerMode { Normal, StallHandshake, StallShutdown, Truncated, Legacy, Tls12 };
struct Peer {
    asio::io_context Io;
    Tcp::acceptor Acceptor{Io, {asio::ip::make_address("127.0.0.1"), 0}};
    Tcp::socket Socket{Io};
    asio::steady_timer Deadline{Io};
    unsigned short Port = Acceptor.local_endpoint().port();
    std::shared_ptr<Lui::Tls::Settings> Settings;
    std::unique_ptr<Lui::Tls::Stream> Tls;
    std::string Request, Reply, Sni, Version;
    std::thread Worker;
    Peer(PeerMode Mode = PeerMode::Normal, const char* Credential = "Valid.p12") {
        std::string Error;
        Settings = Lui::Tls::Settings::Load(Fixture("Root.pem"), Fixture(Credential), "test-only", Error);
        Check(Settings && Settings->Server, "peer credential load failed: " + Error);
        if (!Settings || !Settings->Server) return;
        if (Mode == PeerMode::Legacy) {
            SSL_CTX_set_min_proto_version(Settings->Server->native_handle(), TLS1_VERSION);
            SSL_CTX_set_max_proto_version(Settings->Server->native_handle(), TLS1_1_VERSION);
            SSL_CTX_set_security_level(Settings->Server->native_handle(), 0);
        } else if (Mode == PeerMode::Tls12) SSL_CTX_set_max_proto_version(Settings->Server->native_handle(), TLS1_2_VERSION);
        Reply = Mode == PeerMode::Truncated ? "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"
            : "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\n";
        Reply += std::string("a\0b", 3);
        Deadline.expires_after(std::chrono::seconds(5));
        Deadline.async_wait([this](const asio::error_code& Error) { if (!Error) StopWorker(); });
        Acceptor.async_accept(Socket, [this, Mode](const asio::error_code& Error) {
            if (Error || Mode == PeerMode::StallHandshake) return;
            Tls = std::make_unique<Lui::Tls::Stream>(Socket, Settings, true);
            Tls->Socket.async_handshake(asio::ssl::stream_base::server, [this, Mode](const asio::error_code& Error) {
                if (Error) return;
                if (auto* Name = SSL_get_servername(Tls->Socket.native_handle(), TLSEXT_NAMETYPE_host_name)) Sni = Name;
                Version = SSL_get_version(Tls->Socket.native_handle());
                asio::async_read_until(Tls->Socket, asio::dynamic_buffer(Request, 32768), "\r\n\r\n",
                    [this, Mode](const asio::error_code& Error, size_t) {
                        if (Error) return;
                        asio::async_write(Tls->Socket, asio::buffer(Reply), [this, Mode](const asio::error_code& Error, size_t) {
                            if (Error || Mode == PeerMode::StallShutdown) return;
                            if (Mode == PeerMode::Truncated) { asio::error_code Error; Socket.close(Error); return; }
                            Tls->Socket.async_shutdown([this](const asio::error_code&) {
                                asio::error_code Error; Socket.close(Error);
                            });
                        });
                    });
            });
        });
        Worker = std::thread([this] { Io.run(); });
    }
    void StopWorker() { asio::error_code Error; Acceptor.close(Error); Socket.close(Error); Deadline.cancel(); }
    void Join() {
        if (!Worker.joinable()) return;
        asio::post(Io, [this] { StopWorker(); });
        Worker.join();
    }
    ~Peer() { Join(); }
};
void Client(PeerMode Mode, const char* Credential, const char* ExpectedError, bool Trust = true,
    const char* Host = "127.0.0.1") {
    Peer Server(Mode, Credential);
    Logs Result;
    auto* Owner = Runtime(Result, false, "Valid.p12", Trust);
    LuiNetworkPolicyV1 Policy{sizeof(Policy), LUI_EXTENSION_ABI_VERSION,
        LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY, Server.Port, Server.Port, 0, 0};
    Check(Lui_SetNetworkPolicy(Owner, &Policy) == 1, "HTTPS host policy rejected");
    std::string Script = "local Http = app:GetService('HttpService')\nlocal Url = 'https://" +
        std::string(Host) + ":" + std::to_string(Server.Port) + "/test'\ntask.spawn(function()\n";
    const bool Stall = Mode == PeerMode::StallHandshake || Mode == PeerMode::StallShutdown;
    Script += "local Ok, Response = pcall(function() return Http:RequestAsync({Url=Url, TimeoutMs=" +
        std::to_string(Stall ? 100 : 3000) + ", Verify=false}) end)\n";
    if (ExpectedError) Script += "assert(not Ok and string.find(Response, '" + std::string(ExpectedError) + "'), tostring(Response))\n";
    else Script += "assert(Ok and Response.Success and Response.Body == 'a\\0b', tostring(Response))\n";
    Script += "print('[LUI:TlsTest] Done')\nend)";
    Check(Lui_RunScript(Owner, Script.c_str(), "HttpsClient") == 1, Lui_GetLastError(Owner));
    Pump(Owner, Result);
    Lui_Destroy(Owner);
    Server.Join();
    if (ExpectedError && std::string(ExpectedError) == "CertificateRejected")
        Check(Server.Request.empty(), "HTTP bytes reached a peer before certificate validation");
    if (!ExpectedError && std::string(Host) == "localhost") Check(Server.Sni == "localhost", "DNS SNI was not sent");
    if (Mode == PeerMode::Tls12) Check(Server.Version == "TLSv1.2", "TLS 1.2 interoperability failed");
}
void Configuration() {
    auto* Owner = Lui_Create();
    auto Roots = Fixture("Root.pem"), Pfx = Fixture("Valid.p12");
    LuiTlsOptionsV1 Options{sizeof(Options), LUI_EXTENSION_ABI_VERSION, Roots.data(),
        static_cast<uint32_t>(Roots.size()), Pfx.data(), static_cast<uint32_t>(Pfx.size()), "test-only", 9};
    auto Bad = Options;
    Bad.AbiVersion++;
    Check(Lui_SetTlsOptions(Owner, &Bad) == 0, "TLS ABI mismatch accepted");
    Bad = Options; Bad.TrustAnchorsPem = nullptr;
    Check(Lui_SetTlsOptions(Owner, &Bad) == 0, "null TLS bytes accepted");
    Bad = Options; Bad.ServerPkcs12Bytes = 256 * 1024 + 1;
    Check(Lui_SetTlsOptions(Owner, &Bad) == 0, "oversized TLS credentials accepted");
    Bad = Options; Bad.Password = "wrongpass";
    Check(Lui_SetTlsOptions(Owner, &Bad) == 0 && std::string(Lui_GetLastError(Owner)) == "[LUI:Tls] InvalidCredential",
        "wrong PKCS#12 password did not fail safely");
    Bad = Options; Bad.TrustAnchorsPem = "garbage"; Bad.TrustAnchorsBytes = 7;
    Check(Lui_SetTlsOptions(Owner, &Bad) == 0, "malformed trust accepted");
    Bad = Options; Bad.Password = "test\0only";
    Check(Lui_SetTlsOptions(Owner, &Bad) == 0, "embedded password null accepted");
    Check(Lui_SetTlsOptions(Owner, &Options) == 1, Lui_GetLastError(Owner));
    Check(Lui_SetTlsOptions(Owner, &Options) == 0, "duplicate TLS configuration accepted");
    Lui_Destroy(Owner);
    Owner = Lui_Create();
    Check(Lui_RunScript(Owner, "assert(true)", "TlsLate") == 1 && Lui_SetTlsOptions(Owner, &Options) == 0,
        "late TLS configuration accepted");
    Lui_Destroy(Owner);
}
void Hosted(const char* Family, const char* Host) {
    Logs Result;
    auto* Owner = Runtime(Result, true);
    std::string Script = R"(
        local Server = app:GetService('HttpServerService'):CreateServer({Port=0, TLS=true, Family=')" +
        std::string(Family) + R"('})
        Server:Route('POST', '/echo', function(Request)
            print('[LUI:TlsTest] Dispatch')
            return {Body=Request.Body, Headers={{Name='X-TLS', Value='yes'}}}
        end)
        Server:Start()
        task.spawn(function()
            local Reply = app:GetService('HttpService'):RequestAsync({
                Url='https://)" + std::string(Host) + R"(:' .. tostring(Server.Port) .. '/echo', Method='POST', Body='p\0q'})
            assert(Reply.Body == 'p\0q' and Reply.Success)
            Server:Close()
            print('[LUI:TlsTest] Done')
        end)
    )";
    Check(Lui_RunScript(Owner, Script.c_str(), "HostedTls") == 1, Lui_GetLastError(Owner));
    Pump(Owner, Result);
    Check(Result.Dispatches == 1, "TLS route did not execute exactly once");
    Lui_Destroy(Owner);
}
void HandshakeLifecycle(bool Close) {
    Logs Result;
    auto* Owner = Runtime(Result, true);
    Check(Lui_RunScript(Owner, R"(
        Server = app:GetService('HttpServerService'):CreateServer({Port=0, TLS=true, TimeoutMs=50})
        Server:Route('GET', '/', function() print('[LUI:TlsTest] Dispatch') return {Body='bad'} end)
        Server:Start()
        print('[LUI:TlsTest] Port ' .. tostring(Server.Port))
    )", "TlsStalledServer") == 1, Lui_GetLastError(Owner));
    asio::io_context Io;
    Tcp::socket Socket(Io);
    asio::error_code Error;
    Socket.connect({asio::ip::make_address("127.0.0.1"), Result.Port}, Error);
    Check(!Error, "stalled TLS connection failed");
    if (Close) Check(Lui_RunScript(Owner, "Server:Close()", "TlsServerClose") == 1, Lui_GetLastError(Owner));
    auto End = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < End) { Lui_Pump(Owner); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    Socket.non_blocking(true, Error);
    char Byte;
    Socket.read_some(asio::buffer(&Byte, 1), Error);
    Check(Error == asio::error::eof || Error == asio::error::connection_reset,
        "TLS handshake deadline/close did not release the peer");
    Check(Result.Dispatches == 0 && Result.Error.empty(), "unauthenticated TLS connection reached a route");
    Lui_Destroy(Owner);
}
void Cancellation() {
    Peer Server(PeerMode::StallHandshake);
    Logs Result;
    auto* Owner = Runtime(Result);
    std::string Script = "local Http = app:GetService('HttpService')\ntask.spawn(function()\n"
        "task.delay(0.02, function() Http:CancelAll() end)\nlocal Ok, Error = pcall(function() Http:GetAsync('https://127.0.0.1:" +
        std::to_string(Server.Port) + "/') end)\nassert(not Ok and string.find(Error, 'Canceled'), tostring(Error))\n"
        "print('[LUI:TlsTest] Done')\nend)";
    Check(Lui_RunScript(Owner, Script.c_str(), "TlsCancel") == 1, Lui_GetLastError(Owner));
    Pump(Owner, Result);
    Lui_Destroy(Owner);
    Peer DestroyPeer(PeerMode::StallHandshake);
    Result = {};
    Owner = Runtime(Result);
    Script = "task.spawn(function() app:GetService('HttpService'):GetAsync('https://127.0.0.1:" +
        std::to_string(DestroyPeer.Port) + "/') end)";
    Check(Lui_RunScript(Owner, Script.c_str(), "TlsDestroy") == 1, Lui_GetLastError(Owner));
    Lui_Pump(Owner);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Lui_Destroy(Owner);
}
}
int TlsTests() {
    Configuration();
    Client(PeerMode::Normal, "Valid.p12", nullptr);
    Client(PeerMode::Normal, "Valid.p12", nullptr, true, "localhost");
    Client(PeerMode::Tls12, "Valid.p12", nullptr);
    Client(PeerMode::Normal, "Valid.p12", "CertificateRejected", false);
    Client(PeerMode::Normal, "WrongName.p12", "CertificateRejected");
    Client(PeerMode::Normal, "Expired.p12", "CertificateRejected");
    Client(PeerMode::Legacy, "Valid.p12", "TlsHandshakeFailed");
    Client(PeerMode::StallHandshake, "Valid.p12", "TimedOut");
    Client(PeerMode::StallShutdown, "Valid.p12", nullptr);
    Client(PeerMode::Truncated, "Valid.p12", "UnexpectedEof");
    Hosted("IPv4", "127.0.0.1");
    Hosted("IPv6", "[::1]");
    HandshakeLifecycle(false);
    HandshakeLifecycle(true);
    Cancellation();
    Logs Result;
    // A credential-free runtime must never silently create a plaintext server.
    auto* Owner = Lui_Create();
    LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION, LUI_CAPABILITY_NETWORK_SERVER};
    Check(Lui_DeclareCapabilities(Owner, &Grant) == 1, "server-only TLS grant rejected");
    Check(Lui_RunScript(Owner, R"(
        assert(not pcall(function() app:GetService('HttpServerService'):CreateServer({Port=0,TLS='true'}) end))
        local Ok, Error = pcall(function() app:GetService('HttpServerService'):CreateServer({Port=0,TLS=true}) end)
        assert(not Ok and string.find(Error, 'TlsCredentialRequired'))
    )", "TlsServerDenied") == 1, Lui_GetLastError(Owner));
    Lui_Destroy(Owner);
    if (!Failures) std::puts("[LUI:TlsTest] HTTPS, DNS/IP identity, trust/expiry failures, TLS floor, binary hosted TLS, deadlines, truncation, shutdown, cancellation, configuration, owner delivery, and teardown passed");
    return Failures;
}
