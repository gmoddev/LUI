#include "Protocol.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace Lui::WebSocket;
namespace {
int Failures = 0;
size_t Checks = 0;
void Check(bool Good, const char* Name) {
    ++Checks;
    if (!Good) { ++Failures; std::fprintf(stderr, "[LUI:WebSocketProtocolTest] %s\n", Name); }
}
std::string Bytes(std::initializer_list<unsigned> Values) {
    std::string Result;
    for (auto Value : Values) Result.push_back(char(Value));
    return Result;
}
std::string Frame(Opcode Code, std::string_view Data = {}, bool Fin = true) {
    std::string Result(1, char((Fin ? 0x80 : 0) | static_cast<uint8_t>(Code)));
    if (Data.size() < 126) Result.push_back(char(Data.size()));
    else if (Data.size() <= 65535) {
        Result.push_back(126);
        Result.push_back(char(Data.size() >> 8));
        Result.push_back(char(Data.size()));
    } else {
        Result.push_back(127);
        for (int Shift = 56; Shift >= 0; Shift -= 8) Result.push_back(char((uint64_t(Data.size()) >> Shift) & 255));
    }
    Result.append(Data);
    return Result;
}
void InvalidFrame(const std::string& Wire, Error Expected, size_t Message = 1024 * 1024, size_t FrameBytes = 1024 * 1024) {
    for (size_t Step : {size_t(1), size_t(3), Wire.size() + 1}) {
        Parser Input(Message, FrameBytes);
        FeedResult Parsed{ParseStatus::NeedMore, 0, Error::None};
        size_t Offset = 0;
        while (Offset < Wire.size() && Parsed.Status == ParseStatus::NeedMore) {
            Parsed = Input.Feed(std::string_view(Wire).substr(Offset, Step));
            Offset += Parsed.Consumed;
        }
        Check(Parsed.Status == ParseStatus::Failed && Parsed.Code == Expected, "invalid frame rejected independently of segmentation");
        auto Terminal = Input.Feed(Frame(Opcode::Text, "new"));
        Check(Terminal.Status == ParseStatus::Failed && Terminal.Consumed == 0 && Terminal.Code == Expected, "failed frame parser is terminal");
    }
}
void ValidFrame(const std::string& Wire, const std::string& Expected, EventKind Kind) {
    std::vector<size_t> Splits{0, 1, 2, Wire.size() / 2, Wire.size() - 1, Wire.size()};
    if (Wire.size() < 256) {
        Splits.clear();
        for (size_t Index = 0; Index <= Wire.size(); ++Index) Splits.push_back(Index);
    }
    for (auto Split : Splits) {
        Parser Input;
        const auto First = Input.Feed(std::string_view(Wire).substr(0, Split));
        const auto Second = Input.Feed(std::string_view(Wire).substr(Split));
        Check(First.Consumed + Second.Consumed == Wire.size() && Second.Status == ParseStatus::Event, "valid frame exact boundary under split input");
        const auto Held = Input.Feed("extra");
        Check(Held.Status == ParseStatus::Event && Held.Consumed == 0, "untaken event retains input boundary");
        const auto Event = Input.TakeEvent();
        Check(Event.Kind == Kind && Event.Data == Expected, "valid event immutable payload");
        Check(Input.Feed(Frame(Opcode::Text, "next")).Status == ParseStatus::Event, "next frame follows taken event");
    }
}
void Frames() {
    ValidFrame(Frame(Opcode::Text, "Hello"), "Hello", EventKind::Text);
    ValidFrame(Frame(Opcode::Text), "", EventKind::Text);
    const std::string Binary("a\0b\xff", 4);
    ValidFrame(Frame(Opcode::Binary, Binary), Binary, EventKind::Binary);
    ValidFrame(Frame(Opcode::Ping, "ping"), "ping", EventKind::Ping);
    ValidFrame(Frame(Opcode::Pong, "pong"), "pong", EventKind::Pong);
    for (size_t Count : {size_t(125), size_t(126), size_t(65535), size_t(65536), size_t(1024 * 1024)}) {
        const std::string Data(Count, 'x');
        ValidFrame(Frame(Opcode::Binary, Data), Data, EventKind::Binary);
    }
    const std::string Fragmented = Frame(Opcode::Text, "Hello ", false) +
        Frame(Opcode::Continuation, "world", false) + Frame(Opcode::Continuation, "!");
    ValidFrame(Fragmented, "Hello world!", EventKind::Text);
    const std::string Euro = Bytes({0xe2, 0x82, 0xac});
    ValidFrame(Frame(Opcode::Text, Euro.substr(0, 1), false) + Frame(Opcode::Continuation, Euro.substr(1)), Euro, EventKind::Text);
    ValidFrame(Frame(Opcode::Binary, Binary.substr(0, 1), false) + Frame(Opcode::Continuation, Binary.substr(1)), Binary, EventKind::Binary);
    Parser Input;
    const auto Stream = Frame(Opcode::Text, "a", false) + Frame(Opcode::Ping, "p") +
        Frame(Opcode::Pong, "q") + Frame(Opcode::Continuation, "b");
    size_t Offset = 0;
    for (const auto Kind : {EventKind::Ping, EventKind::Pong, EventKind::Text}) {
        const auto Parsed = Input.Feed(std::string_view(Stream).substr(Offset));
        Check(Parsed.Status == ParseStatus::Event, "interleaved control event");
        Offset += Parsed.Consumed;
        const auto Result = Input.TakeEvent();
        Check(Result.Kind == Kind && Result.Data == (Kind == EventKind::Ping ? "p" : Kind == EventKind::Pong ? "q" : "ab"), "fragment preserved across controls");
    }
    Check(Offset == Stream.size(), "control stream exact boundary");
    for (const auto& Wire : std::vector<std::string>{
            Bytes({0x81, 0x80}), Bytes({0xc1, 0}), Bytes({0xa1, 0}), Bytes({0x91, 0}),
            Bytes({0x83, 0}), Bytes({0x8b, 0}), Bytes({0x80, 0}), Bytes({0x09, 0}),
            Bytes({0x89, 126}), Bytes({0x88, 1, 0}),
            Bytes({0x82, 126, 0, 125}), Bytes({0x82, 127, 0, 0, 0, 0, 0, 0, 0xff, 0xff}),
            Bytes({0x82, 127, 0x80, 0, 0, 0, 0, 1, 0, 0}),
            Frame(Opcode::Text, "a", false) + Frame(Opcode::Text, "b")})
        InvalidFrame(Wire, Error::ProtocolError);
    InvalidFrame(Bytes({0x82, 127, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}), Error::MessageTooLarge);
    InvalidFrame(Frame(Opcode::Binary, std::string(17, 'x')), Error::MessageTooLarge, 16, 16);
    InvalidFrame(Frame(Opcode::Binary, std::string(9, 'x')), Error::MessageTooLarge, 16, 8);
    InvalidFrame(Frame(Opcode::Binary, "12345678", false) + Frame(Opcode::Continuation, "123456789"), Error::MessageTooLarge, 16, 16);
    std::string EmptyFragments = Frame(Opcode::Text, "", false);
    for (size_t Index = 0; Index < 1024; ++Index) EmptyFragments += Frame(Opcode::Continuation, "", false);
    InvalidFrame(EmptyFragments, Error::MessageTooLarge);
    Parser Small(1, 1);
    Check(Small.Feed(Frame(Opcode::Ping, std::string(125, 'p'))).Status == ParseStatus::Event, "control limit independent of small message limit");
    Parser Bad(0, 0);
    Check(Bad.Feed("x").Code == Error::InvalidOptions, "zero parser limits rejected");
    Parser Large(1024 * 1024 + 1, 1);
    Check(Large.Feed("x").Code == Error::InvalidOptions, "excessive parser limits rejected");
}
void Utf8AndClose() {
    for (const auto& Text : std::vector<std::string>{"", "ascii", Bytes({0xc2, 0x80}), Bytes({0xdf, 0xbf}),
            Bytes({0xe0, 0xa0, 0x80}), Bytes({0xed, 0x9f, 0xbf}), Bytes({0xee, 0x80, 0x80}),
            Bytes({0xef, 0xbf, 0xbf}), Bytes({0xf0, 0x90, 0x80, 0x80}), Bytes({0xf4, 0x8f, 0xbf, 0xbf})})
        Check(ValidUtf8(Text), "valid UTF-8 scalar boundary");
    for (const auto& Text : std::vector<std::string>{Bytes({0x80}), Bytes({0xc0, 0x80}), Bytes({0xc1, 0xbf}),
            Bytes({0xc2}), Bytes({0xc2, 'A'}), Bytes({0xe0, 0x9f, 0xbf}), Bytes({0xed, 0xa0, 0x80}),
            Bytes({0xf0, 0x8f, 0xbf, 0xbf}), Bytes({0xf4, 0x90, 0x80, 0x80}), Bytes({0xf5, 0x80, 0x80, 0x80}),
            Bytes({0xff}), Bytes({0xe2, 0x82})}) {
        Check(!ValidUtf8(Text), "invalid UTF-8 rejected");
        InvalidFrame(Frame(Opcode::Text, Text), Error::InvalidUtf8);
        ValidFrame(Frame(Opcode::Binary, Text), Text, EventKind::Binary);
    }
    for (uint16_t Code : {1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 3000, 4999})
        Check(ValidateClose(Code, "ok") == Error::None, "permitted close code");
    for (uint16_t Code : {0, 999, 1004, 1005, 1006, 1015, 1016, 2000, 2999, 5000, 65535}) {
        Check(ValidateClose(Code, "") == Error::ProtocolError, "reserved close code rejected");
        InvalidFrame(Frame(Opcode::Close, Bytes({unsigned(Code >> 8), unsigned(Code & 255)})), Error::ProtocolError);
    }
    InvalidFrame(Frame(Opcode::Close, Bytes({3, 232, 0xff})), Error::InvalidUtf8);
    Check(ValidateClose(1000, std::string(124, 'a')) == Error::ProtocolError, "oversized close reason rejected");
    for (const auto& Data : std::vector<std::string>{"", Bytes({3, 232}) + "done"}) {
        Parser Input;
        const auto Parsed = Input.Feed(Frame(Opcode::Close, Data) + Frame(Opcode::Text, "ignored"));
        Check(Parsed.Status == ParseStatus::Event && Parsed.Consumed == Data.size() + 2, "close stops exact boundary");
        const auto Result = Input.TakeEvent();
        Check(Result.Kind == EventKind::Close && Result.CloseCode == (Data.empty() ? 1005 : 1000) && Result.Data == (Data.empty() ? "" : "done"), "close metadata");
        Check(Input.Feed(Frame(Opcode::Text, "late")).Status == ParseStatus::Failed, "data after close not accepted");
    }
}
const std::string Accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
const std::string Response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: " + Accept + "\r\n";
void Handshakes() {
    const std::string Complete = Response + "Sec-WebSocket-Protocol: chat\r\n\r\n";
    for (size_t Split = 0; Split <= Complete.size(); ++Split) {
        Handshake Input(Accept, {"chat", "other"});
        const auto First = Input.Feed(std::string_view(Complete).substr(0, Split));
        const auto Second = Input.Feed(std::string_view(Complete).substr(Split));
        Check(First.Consumed + Second.Consumed == Complete.size() && Second.Status == ParseStatus::Event && Input.SelectedProtocol() == "chat", "handshake split validation");
        Check(Input.Feed(Frame(Opcode::Text, "first")).Consumed == 0, "handshake leaves websocket suffix untouched");
    }
    Handshake Coalesced(Accept);
    const auto Parsed = Coalesced.Feed(Response + "\r\n" + Frame(Opcode::Text, "first"));
    Check(Parsed.Status == ParseStatus::Event && Parsed.Consumed == Response.size() + 2, "coalesced handshake/frame boundary");
    for (const auto& Bad : std::vector<std::string>{
            "HTTP/1.0 101 Switching\r\n\r\n", "HTTP/1.1 200 OK\r\n\r\n", "HTTP/1.1 301 Redirect\r\n\r\n",
            "HTTP/1.1 101 Switching\n\n", "HTTP/1.1 101 Switching\rX",
            Response + "Sec-WebSocket-Accept: " + Accept + "\r\n\r\n",
            Response + "Upgrade: websocket\r\n\r\n", Response + "Connection: close\r\n\r\n",
            Response + "Connection: ,upgrade\r\n\r\n", Response + "Connection: upgrade,\r\n\r\n",
            Response + "Sec-WebSocket-Protocol: other\r\n\r\n", Response + "Sec-WebSocket-Protocol: chat, other\r\n\r\n",
            Response + "Sec-WebSocket-Protocol: chat\r\nSec-WebSocket-Protocol: chat\r\n\r\n",
            Response + "Sec-WebSocket-Extensions: permessage-deflate\r\n\r\n", Response + "Sec-WebSocket-Extensions:\r\n\r\n",
            Response + "Content-Length: 0\r\n\r\n", Response + "Transfer-Encoding: chunked\r\n\r\n",
            Response + "X : bad\r\n\r\n", Response + " folded\r\n\r\n", Response + "X: " + std::string(8190, 'x') + "\r\n\r\n"}) {
        for (size_t Step : {size_t(1), size_t(7), Bad.size() + 1}) {
            Handshake Input(Accept, {"chat"});
            FeedResult Result{ParseStatus::NeedMore, 0, Error::None};
            for (size_t Offset = 0; Offset < Bad.size() && Result.Status == ParseStatus::NeedMore; Offset += Step)
                Result = Input.Feed(std::string_view(Bad).substr(Offset, Step));
            Check(Result.Status == ParseStatus::Failed && Result.Code == Error::InvalidHandshake, "hostile handshake rejected");
            Check(Input.Feed("extra").Consumed == 0, "failed handshake terminal");
        }
    }
    for (const auto& Bad : std::vector<std::string>{
            "HTTP/1.1 101 Switching\r\nUpgrade: nope\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + Accept + "\r\n\r\n",
            "HTTP/1.1 101 Switching\r\nUpgrade: websocket\r\nConnection: keep-alive\r\nSec-WebSocket-Accept: " + Accept + "\r\n\r\n",
            "HTTP/1.1 101 Switching\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: wrong\r\n\r\n",
            "HTTP/1.1 101 Switching\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n"}) {
        Handshake Input(Accept);
        Check(Input.Feed(Bad).Status == ParseStatus::Failed, "required handshake field validated");
    }
    std::string Many = Response;
    for (size_t Index = 0; Index < 101; ++Index) Many += "X: a\r\n";
    Handshake Counted(Accept);
    Check(Counted.Feed(Many + "\r\n").Status == ParseStatus::Failed, "handshake header count bounded");
    std::string Wide = Response;
    for (size_t Index = 0; Index < 5; ++Index) Wide += "X: " + std::string(8000, 'a') + "\r\n";
    Handshake Sized(Accept);
    Check(Sized.Feed(Wide + "\r\n").Status == ParseStatus::Failed, "handshake aggregate bytes bounded");
}
void Outbound() {
    Lui::Http::Url Url;
    Check(Lui::WebSocket::ParseUrl("ws://example.com/chat?x=1", Url) == Error::None && Url.Port == 80 && !Url.Secure && Url.Target == "/chat?x=1", "ws URL transport mapping");
    Check(Lui::WebSocket::ParseUrl("wss://[::1]:444/chat", Url) == Error::None && Url.Port == 444 && Url.Secure, "wss IPv6 URL mapping");
    for (const auto* Scheme : {"ws", "wS", "Ws", "WS"})
        Check(Lui::WebSocket::ParseUrl(std::string(Scheme) + "://Example.com/PathCase?KeepCase=1", Url) == Error::None &&
            !Url.Secure && Url.Port == 80 && Url.Host == "Example.com" && Url.Target == "/PathCase?KeepCase=1", "ws scheme case independent of host/target");
    for (const auto* Scheme : {"wss", "wsS", "wSs", "wSS", "Wss", "WsS", "WSs", "WSS"})
        Check(Lui::WebSocket::ParseUrl(std::string(Scheme) + "://Example.com/PathCase?KeepCase=1", Url) == Error::None &&
            Url.Secure && Url.Port == 443 && Url.Host == "Example.com" && Url.Target == "/PathCase?KeepCase=1", "wss scheme case preserves secure transport");
    for (const auto& Bad : std::vector<std::string>{"https://example.com", "wsss://example.com", "w s://example.com", "ws://user:pass@example.com/", "ws://example.com/#fragment", "ws://example.com/%zz", "ws://example.com/\r\ninjected", "ws://example.com:0/", "ws://example.com/" + std::string(8192, 'x')})
        Check(Lui::WebSocket::ParseUrl(Bad, Url) == Error::InvalidUrl, "invalid websocket URL rejected");
    Check(Lui::WebSocket::ParseUrl("ws://example.com/chat", Url) == Error::None, "fixture URL");
    std::string Wire, Expected;
    Check(BuildHandshake(Url, {{"Authorization", "Bearer secret"}}, {"chat"}, "https://app.example", Wire, Expected) == Error::None &&
        Wire.find("Upgrade: websocket\r\n") != std::string::npos && Wire.find("Sec-WebSocket-Version: 13\r\n") != std::string::npos &&
        Wire.find("Sec-WebSocket-Protocol: chat\r\n") != std::string::npos && Wire.find("Origin: https://app.example\r\n") != std::string::npos && Expected.size() == 28, "handshake request build");
    std::set<std::string> Nonces;
    for (size_t Index = 0; Index < 32; ++Index) {
        Check(BuildHandshake(Url, {}, {}, {}, Wire, Expected) == Error::None, "handshake cryptographic nonce");
        const auto Start = Wire.find("Sec-WebSocket-Key: ") + 19;
        Nonces.insert(Wire.substr(Start, 24));
    }
    Check(Nonces.size() > 1, "handshake nonce varies");
    for (const char* Name : {"Host", "Connection", "Upgrade", "Origin", "Content-Length", "Transfer-Encoding", "Sec-WebSocket-Key", "sec-websocket-future", "Te", "Expect"})
        Check(BuildHandshake(Url, {{Name, "x"}}, {}, {}, Wire, Expected) == Error::InvalidOptions, "managed handshake fields cannot be overridden");
    Check(BuildHandshake(Url, {{"X", "bad\r\nInjected: yes"}}, {}, {}, Wire, Expected) == Error::InvalidOptions, "header injection rejected");
    Check(BuildHandshake(Url, {}, {"chat", "chat"}, {}, Wire, Expected) == Error::InvalidOptions, "duplicate requested protocols rejected");
    Check(BuildHandshake(Url, {}, {"bad protocol"}, {}, Wire, Expected) == Error::InvalidOptions, "invalid protocol token rejected");
    Check(BuildHandshake(Url, {}, std::vector<std::string>(17, "a"), {}, Wire, Expected) == Error::InvalidOptions, "protocol count bounded");
    Check(BuildHandshake(Url, {}, {std::string(129, 'a')}, {}, Wire, Expected) == Error::InvalidOptions, "protocol length bounded");
    for (const auto& Origin : {"https://app.example/", "https://app.example/path", "https://app.example?x=1", "null", "https://user:pass@app.example", "https://app.example\r\nX: a"})
        Check(BuildHandshake(Url, {}, {}, Origin, Wire, Expected) == Error::InvalidOptions, "origin syntax validated");
    std::set<std::string> Masks;
    for (size_t Length : {size_t(0), size_t(5), size_t(125), size_t(126), size_t(65535), size_t(65536), size_t(1024 * 1024)}) {
        const std::string Data(Length, '\xff');
        Check(EncodeClientFrame(Opcode::Binary, Data, Wire) == Error::None, "client frame encoded");
        const auto Header = Length < 126 ? 2u : Length <= 65535 ? 4u : 10u;
        Check((static_cast<unsigned char>(Wire[1]) & 0x80) && Wire.size() == Header + 4 + Data.size(), "client frame masked size");
        std::string Decoded = Wire.substr(Header + 4);
        for (size_t Index = 0; Index < Decoded.size(); ++Index)
            Decoded[Index] = char(static_cast<unsigned char>(Decoded[Index]) ^ static_cast<unsigned char>(Wire[Header + Index % 4]));
        Check(Decoded == Data, "client mask roundtrip binary payload");
        Masks.insert(Wire.substr(Header, 4));
    }
    Check(Masks.size() > 1, "fresh mask per frame");
    Check(EncodeClientFrame(Opcode::Ping, std::string(126, 'p'), Wire) == Error::ProtocolError, "oversized outbound control rejected");
    Check(EncodeClientFrame(Opcode::Ping, "p", Wire, false) == Error::ProtocolError, "fragmented outbound control rejected");
    Check(EncodeClientFrame(Opcode::Text, Bytes({0xff}), Wire) == Error::InvalidUtf8, "outbound text validates UTF8");
    Check(EncodeClientFrame(Opcode::Binary, std::string(1024 * 1024 + 1, 'x'), Wire) == Error::MessageTooLarge, "outbound hard bound");
    Check(EncodeClientFrame(Opcode::Close, Bytes({3, 237}), Wire) == Error::ProtocolError, "outbound reserved close rejected");
    Check(EncodeClientFrame(Opcode::Close, {}, Wire) == Error::None, "empty close echo supported");
}
}
void ServerRole() {
    const std::string Request="GET /socket HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    Lui::Http::Parser Default(Lui::Http::MessageKind::Request);
    Check(Default.Feed(Request).Status==Lui::Http::ParseStatus::Failed,"ordinary HTTP still refuses Upgrade");
    Lui::Http::Parser Upgrade(Lui::Http::MessageKind::Request,{},"GET",true);
    auto Result=Upgrade.Feed(Request+"suffix");
    Check(Result.Status==Lui::Http::ParseStatus::Complete && Result.Consumed==Request.size(),"opt-in header boundary");
    std::string Wire,Selected,Origin;
    Check(BuildServerHandshake(*Upgrade.GetResult(),{},Wire,Selected,Origin)==Error::None && Wire.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")!=std::string::npos,"server accept RFC vector");
    auto Handshake=[&](const std::string& Text) {
        Lui::Http::Parser Input(Lui::Http::MessageKind::Request,{},"GET",true);
        for(size_t Index=0;Index<Text.size();++Index) {
            const auto Parsed=Input.Feed(std::string_view(Text).substr(Index,1));
            if(Parsed.Status==Lui::Http::ParseStatus::Failed)return Error::InvalidHandshake;
        }
        if(!Input.GetResult())return Error::InvalidHandshake;
        return BuildServerHandshake(*Input.GetResult(),{"second","first"},Wire,Selected,Origin);
    };
    const auto Add=[&](std::string Field){return Request.substr(0,Request.size()-2)+Field+"\r\n";};
    for(const auto& Field:{"Content-Length: 0\r\n","Content-Length: 1\r\n","Transfer-Encoding: chunked\r\n","Trailer: X\r\n","Expect: 100-continue\r\n","Connection: close\r\n","Sec-WebSocket-Version: 13\r\n","Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n","Origin: https://a.example\r\nOrigin: https://a.example\r\n","Sec-WebSocket-Protocol: a,a\r\n","Sec-WebSocket-Protocol: a,,b\r\n","Sec-WebSocket-Extensions: x; q=\r\n","Sec-WebSocket-Extensions: x; q=\"bad value\"\r\n","Sec-WebSocket-Extensions: x,\r\n"})
        Check(Handshake(Add(Field))!=Error::None,"malformed upgrade rejected one byte at a time");
    for(const auto& Field:{"Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n","Sec-WebSocket-Extensions: x; a=\"valid\\x\",y\r\n"})
        Check(Handshake(Add(Field))==Error::None && Wire.find("Extensions")==std::string::npos,"valid extension offer ignored without negotiation");
    Check(Handshake(Add("Sec-WebSocket-Protocol: first,second\r\n"))==Error::None && Selected=="second","server preference selects offered protocol");
    auto Old=Request;Old.replace(Old.find("Version: 13"),11,"Version: 12");
    Check(Handshake(Old)==Error::UnsupportedVersion,"unsupported version distinguished for 426");
    auto BadKey=Request;BadKey.replace(BadKey.find("ZQ=="),4,"ZR==");Check(Handshake(BadKey)==Error::InvalidHandshake,"noncanonical key padding rejected");
    auto Post=Request;Post.replace(0,3,"POST");Check(Handshake(Post)==Error::InvalidHandshake,"non-GET rejected before allocation");
    for(auto Length:{0,1,125,126,65535,65536,1048576}) {
        std::string Data(size_t(Length),'x'),Encoded;
        Check(EncodeClientFrame(Opcode::Binary,Data,Encoded)==Error::None,"masked test frame");
        Parser Decoder(1048576,1048576,true);FeedResult Parsed{ParseStatus::NeedMore,0,Error::None};
        size_t Position=0;while(Position<Encoded.size() && Parsed.Status==ParseStatus::NeedMore) {size_t Count=std::min(size_t(17),Encoded.size()-Position);Parsed=Decoder.Feed(std::string_view(Encoded).substr(Position,Count));Position+=Parsed.Consumed;}
        Check(Parsed.Status==ParseStatus::Event && Decoder.TakeEvent().Data==Data,"server masked split frame");
        Check(EncodeServerFrame(Opcode::Binary,Data,Encoded)==Error::None && !(Encoded[1]&0x80),"server never masks");
        Parser Reject(1048576,1048576,true);Check(Reject.Feed(Encoded).Status==ParseStatus::Failed,"server rejects unmasked client");
    }
}
int main() {
    Frames(); Utf8AndClose(); Handshakes(); Outbound();
    ServerRole();
    std::printf("[LUI:WebSocketProtocolTest] Groups=5 Checks=%zu Failures=%d\n", Checks, Failures);
    return Failures ? 1 : 0;
}
