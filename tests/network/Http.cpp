#include "Parser.h"
#include "LuiRuntime.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace Lui::Http;

static int Failures = 0;
static void Check(bool Condition, const std::string& Name) {
    if (!Condition) { ++Failures; std::fprintf(stderr, "[LUI:HttpTest] %s\n", Name.c_str()); }
}
static const std::string Get = "GET /next?x=%20 HTTP/1.1\r\nHost: localhost\r\n\r\n";

// Every possible two-part split, followed by single-byte delivery, must produce the same message boundary.
static void Valid(const std::string& Wire, MessageKind Kind, const std::string& Body, const std::string& Name,
    std::string_view Method = "GET", bool KeepAlive = true) {
    for (size_t Split = 0; Split <= Wire.size(); ++Split) {
        Parser Input(Kind, {}, Method);
        Check(Input.GetResult() == nullptr, Name + ": exposed incomplete message");
        auto First = Input.Feed(std::string_view(Wire).substr(0, Split));
        auto Second = Input.Feed(std::string_view(Wire).substr(Split));
        Check(First.Consumed + Second.Consumed == Wire.size() && Second.Status == ParseStatus::Complete,
            Name + ": split " + std::to_string(Split));
        const Message* Result = Input.GetResult();
        Check(Result && Result->Body == Body && Result->KeepAlive == KeepAlive, Name + ": body/persistence mismatch");
        auto Terminal = Input.Feed(Get, true);
        Check(Terminal.Status == ParseStatus::Complete && Terminal.Consumed == 0, Name + ": consumed after completion");
    }
    Parser Input(Kind, {}, Method);
    FeedResult Result{ParseStatus::NeedMore, 0, ErrorCode::None};
    for (char Byte : Wire) {
        Result = Input.Feed(std::string_view(&Byte, 1));
        Check(Result.Consumed == 1 && Result.Status != ParseStatus::Failed, Name + ": single-byte failure");
    }
    Check(Result.Status == ParseStatus::Complete && Input.GetResult() && Input.GetResult()->Body == Body,
        Name + ": single-byte result mismatch");
}

static void Invalid(const std::string& Wire, MessageKind Kind, ErrorCode Expected, const std::string& Name,
    Limits Bounds = {}, std::string_view Method = "GET") {
    // Error classification must not depend on transport segmentation.
    for (size_t Step : {size_t(1), size_t(3), Wire.size() + 1}) {
        Parser Input(Kind, Bounds, Method);
        FeedResult Result{ParseStatus::NeedMore, 0, ErrorCode::None};
        for (size_t Offset = 0; Offset < Wire.size() && Result.Status == ParseStatus::NeedMore; Offset += Step)
            Result = Input.Feed(std::string_view(Wire).substr(Offset, Step));
        if (Result.Status == ParseStatus::NeedMore) Result = Input.Feed({}, true);
        Check(Result.Status == ParseStatus::Failed && Result.Error == Expected && Input.GetResult() == nullptr,
            Name + ": expected " + GetErrorName(Expected) + ", got " + GetErrorName(Result.Error));
        auto Terminal = Input.Feed(Get);
        Check(Terminal.Status == ParseStatus::Failed && Terminal.Consumed == 0 && Terminal.Error == Expected,
            Name + ": failed parser recovered/consumed input");
    }
}

static void Requests() {
    Valid(Get, MessageKind::Request, "", "origin-form GET");
    Valid("OPTIONS * HTTP/1.1\r\nHost: [::1]:80\r\n\r\n", MessageKind::Request, "", "IPv6 OPTIONS");
    Valid("POST / HTTP/1.1\r\nhOsT: EXAMPLE.test:8080\r\nContent-Length:\t0005 \t\r\nConnection: keep-alive, Close\r\n\r\nhello",
        MessageKind::Request, "hello", "case/OWS/close", "GET", false);
    std::string Binary("a\0b\xff", 4);
    Valid("POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\n" + Binary,
        MessageKind::Request, Binary, "binary fixed body");
    Valid("POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: ChUnKeD\r\nTrailer: Content-Digest\r\n\r\n"
        "2;flag; note = \"a\\\";b\"\r\nHi\r\n3\r\n!!!\r\n000;end=yes\r\nContent-Digest: sha-256=:abc:\r\n\r\n",
        MessageKind::Request, "Hi!!!", "chunk extensions and separate trailers");
    Valid("POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n" + Binary + "\r\n0\r\n\r\n",
        MessageKind::Request, Binary, "binary chunk body");
    const std::vector<std::string> BadLines = {
        "GET\t/ HTTP/1.1", "GET  / HTTP/1.1", "GET / HTTP/1.1 ", " GET / HTTP/1.1", "GET / HTTP/1.0",
        "GET /#fragment HTTP/1.1", "GET /%xy HTTP/1.1", "GET /% HTTP/1.1", "GET /a\\b HTTP/1.1",
        "GET /\"quoted\" HTTP/1.1", "GET /\x01 HTTP/1.1", "GET /\xff HTTP/1.1", "G(ET / HTTP/1.1",
    };
    for (const auto& Line : BadLines) Invalid(Line + "\r\nHost: localhost\r\n\r\n", MessageKind::Request,
        ErrorCode::InvalidStartLine, "invalid start line");
    Invalid("GET http://example.test/ HTTP/1.1\r\nHost: example.test\r\n\r\n", MessageKind::Request,
        ErrorCode::UnsupportedFeature, "absolute-form unsupported");
    Invalid("CONNECT localhost:80 HTTP/1.1\r\nHost: localhost\r\n\r\n", MessageKind::Request,
        ErrorCode::UnsupportedFeature, "CONNECT unsupported");
    for (const auto& Headers : std::vector<std::string>{"", "Host:\r\n", "Host: one\r\nHost: two\r\n",
        "Host: one\r\nhOsT: one\r\n", "Host: bad host\r\n", "Host: one,two\r\n", "Host: user@host\r\n",
        "Host: example:65536\r\n", "Host: example:\r\n", "Host: ::1\r\n", "Host: [::::]\r\n",
        "Host: [::1]junk\r\n", "Host: [fe80::1%25scope]\r\n"})
        Invalid("GET / HTTP/1.1\r\n" + Headers + "\r\n", MessageKind::Request, ErrorCode::InvalidHost, "invalid Host");
    for (const auto& Field : std::vector<std::string>{"Host : localhost", " Host: localhost", "Bad(Name: x", "X: a\x7f"})
        Invalid("GET / HTTP/1.1\r\n" + Field + "\r\n\r\n", MessageKind::Request, ErrorCode::InvalidHeader, "invalid field");
    Invalid("GET / HTTP/1.1\r\nHost: localhost\r\nX: a\r\n folded\r\n\r\n", MessageKind::Request,
        ErrorCode::InvalidHeader, "obs-fold rejected");
    Invalid(std::string("GET / HTTP/1.1\r\nHost: localhost\r\nX: a") + '\0' + "b\r\n\r\n", MessageKind::Request,
        ErrorCode::InvalidHeader, "NUL in field rejected");
    Invalid("GET / HTTP/1.1\nHost: localhost\n\n", MessageKind::Request, ErrorCode::InvalidFraming, "bare LF rejected");
    Invalid("GET / HTTP/1.1\rX", MessageKind::Request, ErrorCode::InvalidFraming, "bare CR rejected");
    for (const auto& Headers : std::vector<std::string>{
        "Content-Length: 1\r\nTransfer-Encoding: chunked\r\n", "Transfer-Encoding: chunked\r\nContent-Length: 1\r\n",
        "Content-Length: 1\r\nContent-Length: 1\r\n", "Content-Length: 1\r\ncOnTeNt-LeNgTh: 2\r\n",
        "Content-Length: 1, 1\r\n", "Content-Length: +1\r\n", "Content-Length: -1\r\n", "Content-Length:\r\n",
        "Content-Length: 1 0\r\n", "Content-Length: 18446744073709551616\r\n", "Transfer-Encoding: gzip\r\n",
        "Transfer-Encoding: gzip, chunked\r\n", "Transfer-Encoding: chunked, chunked\r\n", "Transfer-Encoding: chunked;x=1\r\n",
        "Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n", "Connection: Content-Length\r\n"})
        Invalid("POST / HTTP/1.1\r\nHost: localhost\r\n" + Headers + "\r\n", MessageKind::Request,
            ErrorCode::InvalidFraming, "ambiguous/invalid framing");
    Invalid("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close,,keep-alive\r\n\r\n", MessageKind::Request,
        ErrorCode::InvalidHeader, "empty connection token");
    for (const auto& Field : {"Expect: 100-continue", "Upgrade: websocket", "Connection: Upgrade"})
        Invalid("GET / HTTP/1.1\r\nHost: localhost\r\n" + std::string(Field) + "\r\n\r\n", MessageKind::Request,
            ErrorCode::UnsupportedFeature, "unsupported expectation/upgrade");
}

static void ChunksAndLimits() {
    const std::string Head = "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n";
    for (const auto& Chunk : std::vector<std::string>{"+1\r\n", "-1\r\n", "0x1\r\n", " 1\r\n", "1 \r\n", "\r\n",
        "10000000000000000\r\n", "1;\r\n", "1;=a\r\n", "1;x=\r\n", "1;x=\"unterminated\r\n",
        "1;x=\"a\"junk\r\n", "1;x=\"a\" \r\n", "1\r\naX", "1\r\na\rX"})
        Invalid(Head + Chunk, MessageKind::Request, ErrorCode::InvalidChunk, "invalid chunk syntax");
    for (const auto& Field : {"Content-Length", "Transfer-Encoding", "Host", "Authorization", "Content-Type", "X-Unknown"})
        Invalid(Head + "0\r\n" + Field + ": x\r\n\r\n", MessageKind::Request,
            ErrorCode::ForbiddenTrailer, "unsafe/unknown trailer");
    Invalid("POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nTrailer: Host\r\n\r\n",
        MessageKind::Request, ErrorCode::ForbiddenTrailer, "unsafe trailer declaration");
    Invalid("POST / HTTP/1.1\r\nHost: localhost\r\nConnection: Content-Digest\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Digest: x\r\n\r\n",
        MessageKind::Request, ErrorCode::ForbiddenTrailer, "connection-nominated trailer");
    for (const auto& Partial : std::vector<std::string>{"G", "GET / HTTP/1.1\r", "GET / HTTP/1.1\r\nHost: localhost\r\n",
        "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\na", Head + "1\r\n", Head + "1\r\na\r",
        Head + "0\r\nContent-Digest: x\r\n"})
        Invalid(Partial, MessageKind::Request, ErrorCode::UnexpectedEof, "incomplete message");
    Limits Bounds;
    Bounds.BodyBytes = 3;
    Invalid("POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\n", MessageKind::Request,
        ErrorCode::LimitExceeded, "fixed body limit before data", Bounds);
    Invalid(Head + "2\r\nab\r\n2\r\n", MessageKind::Request, ErrorCode::LimitExceeded, "cumulative chunk body limit", Bounds);
    Invalid("HTTP/1.1 200 OK\r\n\r\nabcd", MessageKind::Response, ErrorCode::LimitExceeded, "close body limit", Bounds);
    Bounds = {}; Bounds.LineBytes = 16;
    Invalid(Get, MessageKind::Request, ErrorCode::LimitExceeded, "line byte limit", Bounds);
    Bounds = {}; Bounds.HeaderBytes = 18; // Host: localhost CRLF plus final CRLF requires 19 bytes.
    Invalid(Get, MessageKind::Request, ErrorCode::LimitExceeded, "header byte limit includes final CRLF", Bounds);
    Bounds = {}; Bounds.HeaderCount = 1;
    Invalid("GET / HTTP/1.1\r\nHost: localhost\r\nX: a\r\n\r\n", MessageKind::Request,
        ErrorCode::LimitExceeded, "header count limit", Bounds);
    Bounds = {}; Bounds.HeaderCount = 2;
    Invalid(Head + "0\r\nContent-Digest: x\r\n\r\n", MessageKind::Request, ErrorCode::LimitExceeded, "trailer count shares limit", Bounds);
    Bounds = {}; Bounds.ChunkMetadataBytes = 4;
    Invalid(Head + "1\r\na\r\n0\r\n\r\n", MessageKind::Request, ErrorCode::LimitExceeded, "chunk overhead limit", Bounds);
    Bounds = {}; Bounds.ChunkCount = 2;
    Invalid(Head + "1\r\na\r\n1\r\nb\r\n0\r\n\r\n", MessageKind::Request, ErrorCode::LimitExceeded, "chunk count includes zero", Bounds);
    Bounds = {}; Bounds.HeaderCount = 0;
    Invalid(Get, MessageKind::Request, ErrorCode::InvalidLimits, "invalid parser limits", Bounds);
    Bounds = {}; Bounds.BodyBytes = 0;
    Parser Empty(MessageKind::Request, Bounds);
    Check(Empty.Feed(Get).Status == ParseStatus::Complete, "zero body limit rejected empty request");
    Bounds = {}; Bounds.HeaderBytes = 19;
    Parser Exact(MessageKind::Request, Bounds);
    Check(Exact.Feed(Get).Status == ParseStatus::Complete, "exact header byte limit failed");
    Bounds = {}; Bounds.BodyBytes = 3;
    Parser ExactBody(MessageKind::Request, Bounds);
    Check(ExactBody.Feed(Head + "3\r\nabc\r\n0\r\n\r\n").Status == ParseStatus::Complete, "exact chunk body limit failed");
}

static void ResponsesAndBoundaries() {
    Valid("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nSet-Cookie: a=1\r\nSet-Cookie: b=2\r\n\r\nabc",
        MessageKind::Response, "abc", "response duplicate fields");
    Valid("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n",
        MessageKind::Response, "abc", "chunked response");
    Valid("HTTP/1.1 200 OK\r\nContent-Length: 99999999\r\n\r\n", MessageKind::Response, "", "HEAD representation length", "HEAD");
    Valid("HTTP/1.1 304 Not Modified\r\nContent-Length: 99999999\r\n\r\n", MessageKind::Response, "", "304 representation length");
    Valid("HTTP/1.1 204 No Content\r\n\r\n", MessageKind::Response, "", "204 no body");
    Valid("HTTP/1.1 100 Continue\r\n\r\n", MessageKind::Response, "", "informational no body");
    Valid("HTTP/1.1 205 Reset Content\r\nContent-Length: 0\r\n\r\n", MessageKind::Response, "", "205 empty framing");
    for (const auto& Wire : {"HTTP/1.0 200 OK\r\n\r\n", "HTTP/1.1 20 OK\r\n\r\n", "HTTP/1.1 200\r\n\r\n",
        "HTTP/1.1 600 Unsupported\r\n\r\n", "HTTP/1.1 200 Bad\x01\r\n\r\n"})
        Invalid(Wire, MessageKind::Response, ErrorCode::InvalidStartLine, "invalid status line");
    Invalid("HTTP/1.1 101 Switching Protocols\r\n\r\n", MessageKind::Response, ErrorCode::UnsupportedFeature, "response upgrade");
    Invalid("HTTP/1.1 200 OK\r\n\r\n", MessageKind::Response, ErrorCode::UnsupportedFeature, "CONNECT tunnel", {}, "CONNECT");
    for (const auto& Wire : {"HTTP/1.1 100 Continue\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 204 No Content\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 205 Reset Content\r\nContent-Length: 1\r\n\r\n",
        "HTTP/1.1 205 Reset Content\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n0\r\n\r\n",
        "HTTP/1.1 205 Reset Content\r\n\r\nx"})
        Invalid(Wire, MessageKind::Response, ErrorCode::InvalidFraming, "body-forbidden response");
    Parser Close(MessageKind::Response);
    Check(Close.Feed("HTTP/1.1 200 OK\r\n\r\nabc").Status == ParseStatus::NeedMore && !Close.GetResult(), "EOF body completed too early");
    Check(Close.Feed("def", true).Status == ParseStatus::Complete && Close.GetResult()->Body == "abcdef" &&
        !Close.GetResult()->KeepAlive, "EOF body/persistence incorrect");
    const std::string First = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\nHi";
    Parser One(MessageKind::Request), Two(MessageKind::Request);
    auto Parsed = One.Feed(First + Get);
    Check(Parsed.Status == ParseStatus::Complete && Parsed.Consumed == First.size(), "pipelined request swallowed next request");
    Check(Two.Feed(std::string_view(First + Get).substr(Parsed.Consumed)).Status == ParseStatus::Complete &&
        Two.GetResult()->Target == "/next?x=%20", "pipelined suffix did not parse");
    const std::string Info = "HTTP/1.1 100 Continue\r\n\r\n";
    const std::string Final = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    Parser Interim(MessageKind::Response);
    Check(Interim.Feed(Info + Final).Consumed == Info.size(), "informational response swallowed final response");
    Parser Cookies(MessageKind::Response);
    Cookies.Feed("HTTP/1.1 200 OK\r\nSet-Cookie: a=1\r\nSet-Cookie: b=2\r\nContent-Length: 0\r\n\r\n");
    Check(Cookies.GetResult() && Cookies.GetResult()->Headers.size() == 3 &&
        Cookies.GetResult()->Headers[0].Name == "set-cookie" && Cookies.GetResult()->Headers[1].Value == "b=2",
        "repeated fields were merged/lost");
    Parser Trailers(MessageKind::Request);
    Trailers.Feed("POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nServer-Timing: total;dur=1\r\n\r\n");
    Check(Trailers.GetResult() && Trailers.GetResult()->Headers.size() == 2 && Trailers.GetResult()->Trailers.size() == 1,
        "trailer merged into headers");
}

static void CorruptionAndEof() {
    const std::vector<std::string> Seeds{
        "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\nHi",
        "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n2;x=\"y\"\r\nHi\r\n0\r\n\r\n",
    };
    for (const auto& Seed : Seeds) {
        for (size_t Length = 0; Length < Seed.size(); ++Length) {
            Parser Prefix(MessageKind::Request);
            auto Result = Prefix.Feed(std::string_view(Seed).substr(0, Length), true);
            Check(Result.Status == ParseStatus::Failed && Result.Error == ErrorCode::UnexpectedEof && !Prefix.GetResult(),
                "truncated valid message completed or exposed a message");
        }
        for (size_t Index = 0; Index < Seed.size(); ++Index) {
            for (unsigned char Byte : {0, 10, 13, 32, 127, 255}) {
                std::string Wire = Seed;
                Wire[Index] = static_cast<char>(Byte);
                Parser Whole(MessageKind::Request), Fragmented(MessageKind::Request);
                auto Expected = Whole.Feed(Wire, true);
                FeedResult Actual{ParseStatus::NeedMore, 0, ErrorCode::None};
                size_t Consumed = 0;
                while (Consumed < Wire.size() && Actual.Status == ParseStatus::NeedMore) {
                    Actual = Fragmented.Feed(std::string_view(Wire).substr(Consumed, 1));
                    Consumed += Actual.Consumed;
                }
                if (Actual.Status == ParseStatus::NeedMore) Actual = Fragmented.Feed({}, true);
                Check(Expected.Status == Actual.Status && Expected.Error == Actual.Error, "corrupt input changed classification by segmentation");
                if (Expected.Status == ParseStatus::Complete) {
                    Check(Expected.Consumed == Consumed && Whole.GetResult() && Fragmented.GetResult() &&
                        Whole.GetResult()->Body == Fragmented.GetResult()->Body && Whole.GetResult()->Target == Fragmented.GetResult()->Target,
                        "corrupt input changed message boundary/body by segmentation");
                } else Check(!Whole.GetResult() && !Fragmented.GetResult(), "corrupt partial message escaped");
            }
        }
    }
}

struct TcpResults {
    Parser First{MessageKind::Request}, Second{MessageKind::Request};
    size_t Messages = 0;
    bool Done = false;
    std::string Error;
};
static void LUI_CALL OnLog(void* Context, const char* Level, const char* Text) {
    auto& Result = *static_cast<TcpResults*>(Context);
    std::string_view Value(Text);
    if (std::string_view(Level) == "Error") Result.Error = Text;
    const std::string_view Prefix = "[LUI:HttpTest] Bytes:";
    if (Value.substr(0, Prefix.size()) == Prefix) {
        Value.remove_prefix(Prefix.size());
        while (!Value.empty()) {
            if (Result.Messages >= 2) { Result.Error = "unexpected extra TCP message"; return; }
            auto Parsed = (Result.Messages == 0 ? Result.First : Result.Second).Feed(Value);
            if (Parsed.Status == ParseStatus::Failed) { Result.Error = GetErrorName(Parsed.Error); return; }
            Value.remove_prefix(Parsed.Consumed);
            if (Parsed.Status == ParseStatus::Complete) ++Result.Messages;
        }
    }
    if (Value == "[LUI:HttpTest] Done") Result.Done = true;
}
static void TcpTransport() {
    TcpResults Result;
    LuiRuntime* Runtime = Lui_Create();
    Lui_SetLogCallback(Runtime, &Result, OnLog);
    LuiCapabilityDeclarationV1 Grants{sizeof(Grants), LUI_EXTENSION_ABI_VERSION,
        LUI_CAPABILITY_NETWORK_CLIENT | LUI_CAPABILITY_NETWORK_SERVER | LUI_CAPABILITY_NETWORK_RAW};
    Check(Lui_DeclareCapabilities(Runtime, &Grants) == 1, "TCP fixture grants failed");
    const char* Script = R"(
        local Network = app:GetService("NetworkService")
        local Listener = Network:ListenTcp({Port = 0, Family = "IPv4"})
        task.spawn(function()
            local Connection = Listener:AcceptAsync()
            while true do
                local Bytes = Connection:ReadAsync(7)
                if Bytes == nil then break end
                print("[LUI:HttpTest] Bytes:" .. buffer.tostring(Bytes))
            end
            Connection:Close()
            Listener:Close()
            print("[LUI:HttpTest] Done")
        end)
        task.spawn(function()
            local Connection = Network:ConnectTcp({Address = "127.0.0.1", Port = Listener.Port})
            Connection:WriteAsync("POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\nHi" ..
                "GET /next HTTP/1.1\r\nHost: localhost\r\n\r\n")
            Connection:Shutdown("Write")
            Connection:Close()
        end)
    )";
    Check(Lui_RunScript(Runtime, Script, "HttpTcpFixture") == 1, Lui_GetLastError(Runtime));
    auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!Result.Done && Result.Error.empty() && std::chrono::steady_clock::now() < Deadline) {
        Lui_Pump(Runtime);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(Result.Done && Result.Error.empty() && Result.Messages == 2, "TCP parser fixture failed: " + Result.Error);
    Check(Result.First.GetResult() && Result.First.GetResult()->Body == "Hi" &&
        Result.Second.GetResult() && Result.Second.GetResult()->Target == "/next", "TCP message boundary/body changed");
    Lui_Destroy(Runtime);
}

int HttpClientTests();
int HttpServerTests();
int main() {
    Requests();
    ChunksAndLimits();
    ResponsesAndBoundaries();
    CorruptionAndEof();
    TcpTransport();
    Failures += HttpClientTests();
    Failures += HttpServerTests();
    if (!Failures) std::puts("[LUI:HttpTest] HTTP framing, fragmentation, limits, and TCP transport passed");
    return Failures ? 1 : 0;
}
