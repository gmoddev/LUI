#include "Protocol.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <limits>

namespace Lui::WebSocket {
namespace {
constexpr size_t MessageCeiling = 1024 * 1024;
constexpr size_t HeaderCeiling = 32768;
bool Token(std::string_view Text) {
    return !Text.empty() && std::all_of(Text.begin(), Text.end(), [](unsigned char Byte) {
        return (Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') ||
            (Byte >= '0' && Byte <= '9') || std::string_view("!#$%&'*+-.^_`|~").find(char(Byte)) != std::string_view::npos;
    });
}
std::string Lower(std::string_view Text) {
    std::string Result(Text);
    for (char& Byte : Result) if (Byte >= 'A' && Byte <= 'Z') Byte += 'a' - 'A';
    return Result;
}
std::string_view Trim(std::string_view Text) {
    while (!Text.empty() && (Text.front() == ' ' || Text.front() == '\t')) Text.remove_prefix(1);
    while (!Text.empty() && (Text.back() == ' ' || Text.back() == '\t')) Text.remove_suffix(1);
    return Text;
}
bool FieldValue(std::string_view Text) {
    return std::all_of(Text.begin(), Text.end(), [](unsigned char Byte) { return Byte == '\t' || (Byte >= 32 && Byte != 127); });
}
bool Managed(std::string_view Name) {
    return Name == "host" || Name == "connection" || Name == "upgrade" || Name == "origin" ||
        Name == "content-length" || Name == "transfer-encoding" || Name == "trailer" ||
        Name == "te" || Name == "expect" || Name == "keep-alive" || Name == "proxy-connection" ||
        Name.substr(0, 14) == "sec-websocket-";
}
bool CloseCode(uint16_t Code) {
    return (Code >= 1000 && Code <= 1014 && Code != 1004 && Code != 1005 && Code != 1006) ||
        (Code >= 3000 && Code <= 4999);
}
}

Error ParseUrl(std::string_view Text, Lui::Http::Url& Result) {
    if (Text.size() > 8190) return Error::InvalidUrl;
    std::string Mapped;
    // RFC 6455 section 3 makes the scheme ASCII case-insensitive. Normalize
    // only that prefix; the shared HTTP parser and encoded target stay intact.
    if (Lower(Text.substr(0, 5)) == "ws://") Mapped = "http://" + std::string(Text.substr(5));
    else if (Lower(Text.substr(0, 6)) == "wss://") Mapped = "https://" + std::string(Text.substr(6));
    else return Error::InvalidUrl;
    Lui::Http::Url Next;
    if (Lui::Http::ParseUrl(Mapped, Next) != Lui::Http::ErrorCode::None) return Error::InvalidUrl;
    Result = std::move(Next);
    return Error::None;
}

Error BuildHandshake(const Lui::Http::Url& Url, const std::vector<Lui::Http::Field>& Headers,
    const std::vector<std::string>& Protocols, std::string_view Origin,
    std::string& Wire, std::string& ExpectedAccept) {
    // Revalidate internal caller-provided URL objects at the serialization seam.
    Lui::Http::Url Checked;
    if (Url.Authority.size() > 8192 || Url.Target.size() > 8192 || Url.Host.size() > 253)
        return Error::InvalidUrl;
    if (Lui::Http::ParseUrl(std::string(Url.Secure ? "https://" : "http://") + Url.Authority + Url.Target,
            Checked) != Lui::Http::ErrorCode::None || Checked.Host != Url.Host ||
        Checked.Port != Url.Port || Checked.Secure != Url.Secure || Checked.Target != Url.Target)
        return Error::InvalidUrl;
    if (Protocols.size() > 16 || Headers.size() > 32) return Error::InvalidOptions;
    for (size_t Index = 0; Index < Protocols.size(); ++Index) {
        if (Protocols[Index].size() > 128 || !Token(Protocols[Index]) ||
            std::find(Protocols.begin(), Protocols.begin() + Index, Protocols[Index]) != Protocols.begin() + Index)
            return Error::InvalidOptions;
    }
    size_t HeaderBytes = 0;
    for (const auto& Header : Headers) {
        if (!Token(Header.Name) || !FieldValue(Header.Value) || Managed(Lower(Header.Name)) ||
            Header.Name.size() > 8192 || Header.Value.size() > 8192 ||
            Header.Name.size() + Header.Value.size() + 4 > 8192)
            return Error::InvalidOptions;
        const size_t Bytes = Header.Name.size() + Header.Value.size() + 4;
        if (Bytes > 16384 - HeaderBytes) return Error::InvalidOptions;
        HeaderBytes += Bytes;
    }
    if (!Origin.empty()) {
        Lui::Http::Url Parsed;
        if (Origin.size() > 2048 || Lui::Http::ParseUrl(Origin, Parsed) != Lui::Http::ErrorCode::None ||
            Parsed.Target != "/" || Origin != std::string(Parsed.Secure ? "https://" : "http://") + Parsed.Authority)
            return Error::InvalidOptions;
    }
    std::array<unsigned char, 16> Nonce{};
    if (RAND_bytes(Nonce.data(), int(Nonce.size())) != 1) return Error::RandomFailed;
    std::array<unsigned char, 25> Key{};
    if (EVP_EncodeBlock(Key.data(), Nonce.data(), int(Nonce.size())) != 24) return Error::RandomFailed;
    const std::string Challenge(reinterpret_cast<const char*>(Key.data()), 24);
    const std::string HashInput = Challenge + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::array<unsigned char, EVP_MAX_MD_SIZE> Digest{};
    unsigned DigestBytes = 0;
    if (EVP_Digest(HashInput.data(), HashInput.size(), Digest.data(), &DigestBytes, EVP_sha1(), nullptr) != 1 || DigestBytes != 20)
        return Error::RandomFailed;
    std::array<unsigned char, 29> Accept{};
    if (EVP_EncodeBlock(Accept.data(), Digest.data(), 20) != 28) return Error::RandomFailed;
    std::string Next = "GET " + Url.Target + " HTTP/1.1\r\nHost: " + Url.Authority +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + Challenge +
        "\r\nSec-WebSocket-Version: 13\r\n";
    if (!Origin.empty()) Next += "Origin: " + std::string(Origin) + "\r\n";
    if (!Protocols.empty()) {
        Next += "Sec-WebSocket-Protocol: ";
        for (size_t Index = 0; Index < Protocols.size(); ++Index) {
            if (Index) Next += ", ";
            Next += Protocols[Index];
        }
        Next += "\r\n";
    }
    for (const auto& Header : Headers) Next += Header.Name + ": " + Header.Value + "\r\n";
    Next += "\r\n";
    if (Next.size() > HeaderCeiling) return Error::InvalidOptions;
    Wire = std::move(Next);
    ExpectedAccept.assign(reinterpret_cast<const char*>(Accept.data()), 28);
    return Error::None;
}

bool ValidUtf8(std::string_view Text) {
    size_t Index = 0;
    while (Index < Text.size()) {
        const auto Lead = static_cast<uint8_t>(Text[Index++]);
        if (Lead < 0x80) continue;
        unsigned Count;
        uint32_t Value, Minimum;
        if (Lead >= 0xc2 && Lead <= 0xdf) { Count = 1; Value = Lead & 0x1f; Minimum = 0x80; }
        else if (Lead >= 0xe0 && Lead <= 0xef) { Count = 2; Value = Lead & 0xf; Minimum = 0x800; }
        else if (Lead >= 0xf0 && Lead <= 0xf4) { Count = 3; Value = Lead & 7; Minimum = 0x10000; }
        else return false;
        if (Count > Text.size() - Index) return false;
        while (Count--) {
            const auto Byte = static_cast<uint8_t>(Text[Index++]);
            if ((Byte & 0xc0) != 0x80) return false;
            Value = (Value << 6) | (Byte & 0x3f);
        }
        if (Value < Minimum || Value > 0x10ffff || (Value >= 0xd800 && Value <= 0xdfff)) return false;
    }
    return true;
}

Error ValidateClose(uint16_t Code, std::string_view Reason) {
    if (!CloseCode(Code) || Reason.size() > 123) return Error::ProtocolError;
    return ValidUtf8(Reason) ? Error::None : Error::InvalidUtf8;
}

static Error EncodeFrame(Opcode Code, std::string_view Payload, std::string& Wire, bool Fin, bool Masked) {
    const auto Number = static_cast<uint8_t>(Code);
    const bool Control = Number >= 8;
    if (!(Number == 0 || Number == 1 || Number == 2 || Number == 8 || Number == 9 || Number == 10) ||
        (Control && (!Fin || Payload.size() > 125))) return Error::ProtocolError;
    if (Payload.size() > MessageCeiling) return Error::MessageTooLarge;
    if (Code == Opcode::Text && Fin && !ValidUtf8(Payload)) return Error::InvalidUtf8;
    if (Code == Opcode::Close) {
        if (Payload.size() == 1) return Error::ProtocolError;
        if (Payload.size() >= 2) {
            const auto Status = uint16_t((uint16_t(static_cast<uint8_t>(Payload[0])) << 8) | static_cast<uint8_t>(Payload[1]));
            const auto Checked = ValidateClose(Status, Payload.substr(2));
            if (Checked != Error::None) return Checked;
        }
    }
    std::array<unsigned char, 4> Mask{};
    if (Masked && RAND_bytes(Mask.data(), int(Mask.size())) != 1) return Error::RandomFailed;
    std::string Next;
    Next.reserve(Payload.size() + 14);
    Next.push_back(char((Fin ? 0x80 : 0) | Number));
    const unsigned MaskBit = Masked ? 0x80 : 0;
    if (Payload.size() < 126) Next.push_back(char(MaskBit | Payload.size()));
    else if (Payload.size() <= 65535) {
        Next.push_back(char(MaskBit | 126));
        Next.push_back(char((Payload.size() >> 8) & 255));
        Next.push_back(char(Payload.size() & 255));
    } else {
        Next.push_back(char(MaskBit | 127));
        const auto Bytes = uint64_t(Payload.size());
        for (int Shift = 56; Shift >= 0; Shift -= 8) Next.push_back(char((Bytes >> Shift) & 255));
    }
    if (Masked) Next.append(reinterpret_cast<const char*>(Mask.data()), Mask.size());
    const size_t Start = Next.size();
    Next.append(Payload.data(), Payload.size());
    if (Masked) for (size_t Index = 0; Index < Payload.size(); ++Index) Next[Start + Index] = char(static_cast<uint8_t>(Next[Start + Index]) ^ Mask[Index % 4]);
    Wire = std::move(Next);
    return Error::None;
}
Error EncodeClientFrame(Opcode Code, std::string_view Payload, std::string& Wire, bool Fin) { return EncodeFrame(Code, Payload, Wire, Fin, true); }
Error EncodeServerFrame(Opcode Code, std::string_view Payload, std::string& Wire, bool Fin) { return EncodeFrame(Code, Payload, Wire, Fin, false); }
bool ValidProtocol(std::string_view Text) { return Text.size() <= 128 && Token(Text); }
static bool ValidExtensions(std::string_view Text) {
    size_t Position=0;
    auto Space=[&] {while(Position<Text.size() && (Text[Position]==' ' || Text[Position]=='\t'))++Position;};
    auto ReadToken=[&] {
        const size_t Start=Position;
        while(Position<Text.size() && Token(Text.substr(Position,1)))++Position;
        return Position>Start;
    };
    Space();
    for(;;) {
        if(!ReadToken())return false;
        Space();
        while(Position<Text.size() && Text[Position]==';') {
            ++Position;Space();if(!ReadToken())return false;Space();
            if(Position<Text.size() && Text[Position]=='=') {
                ++Position;Space();
                if(Position<Text.size() && Text[Position]=='"') {
                    ++Position;size_t Count=0;
                    while(Position<Text.size() && Text[Position]!='"') {
                        if(Text[Position]=='\\')++Position;
                        if(Position==Text.size() || !Token(Text.substr(Position,1)))return false;
                        ++Position;++Count;
                    }
                    if(!Count || Position==Text.size())return false;
                    ++Position;
                }else if(!ReadToken())return false;
                Space();
            }
        }
        if(Position==Text.size())return true;
        if(Text[Position++]!=',')return false;
        Space();
    }
}

Error BuildServerHandshake(const Lui::Http::Message& Request, const std::vector<std::string>& Protocols,
    std::string& Wire, std::string& Selected, std::string& Origin) {
    if (Request.Method != "GET" || !Request.WebSocketUpgrade || !Request.Body.empty() || !Request.KeepAlive) return Error::InvalidHandshake;
    bool Upgrade = false, Connection = false, Version = false, KeySeen = false, OriginSeen = false, SupportedVersion=false;
    std::string Key; std::vector<std::string> Offered;
    Origin.clear(); Selected.clear();
    for (const auto& Field : Request.Headers) {
        const auto Name = Lower(Field.Name); const auto Value = Trim(Field.Value);
        if (Name == "upgrade") { if (Upgrade || Lower(Value) != "websocket") return Error::InvalidHandshake; Upgrade = true; }
        else if (Name == "connection") {
            size_t Start = 0;
            for (;;) {
                auto Comma = Value.find(',', Start); auto Part = Trim(Value.substr(Start, Comma == std::string_view::npos ? Comma : Comma - Start));
                if (!Token(Part) || Lower(Part) == "close") return Error::InvalidHandshake;
                if (Lower(Part) == "upgrade") Connection = true;
                if (Comma == std::string_view::npos) break; Start = Comma + 1;
            }
        } else if (Name == "sec-websocket-key") {
            if (KeySeen || Value.size() != 24 || Value.substr(22) != "==") return Error::InvalidHandshake;
            std::array<unsigned char,24> Decoded{}; std::array<unsigned char,25> Canonical{};
            if (EVP_DecodeBlock(Decoded.data(),reinterpret_cast<const unsigned char*>(Value.data()),24) != 18 ||
                EVP_EncodeBlock(Canonical.data(),Decoded.data(),16) != 24 ||
                Value != std::string_view(reinterpret_cast<const char*>(Canonical.data()),24)) return Error::InvalidHandshake;
            Key.assign(Value); KeySeen = true;
        } else if (Name == "sec-websocket-version") {
            if (Version || Value.empty() || Value.size()>3 || (Value.size()>1 && Value[0]=='0') ||
                !std::all_of(Value.begin(),Value.end(),[](char Byte){return Byte>='0' && Byte<='9';}))return Error::InvalidHandshake;
            unsigned Number=0;for(char Byte:Value)Number=Number*10+unsigned(Byte-'0');if(Number>255)return Error::InvalidHandshake;
            Version = true;SupportedVersion=Value=="13";
        }
        else if (Name == "origin") {
            Lui::Http::Url Parsed;
            if (OriginSeen || Value.size() > 2048 || Lui::Http::ParseUrl(Value,Parsed) != Lui::Http::ErrorCode::None ||
                Value != std::string(Parsed.Secure ? "https://" : "http://") + Parsed.Authority) return Error::InvalidHandshake;
            Origin.assign(Value); OriginSeen = true;
        } else if (Name == "sec-websocket-protocol") {
            size_t Start = 0;
            for (;;) {
                auto Comma=Value.find(',',Start);auto Part=Trim(Value.substr(Start,Comma==std::string_view::npos?Comma:Comma-Start));
                if (!ValidProtocol(Part) || Offered.size()>=16 || std::find(Offered.begin(),Offered.end(),Part)!=Offered.end()) return Error::InvalidHandshake;
                Offered.emplace_back(Part);if(Comma==std::string_view::npos)break;Start=Comma+1;
            }
        } else if (Name == "sec-websocket-extensions") {if(!ValidExtensions(Value))return Error::InvalidHandshake;}
        else if (Name == "content-length" || Name == "transfer-encoding" || Name == "trailer" || Name == "expect") return Error::InvalidHandshake;
    }
    if (!Upgrade || !Connection || !Version || !KeySeen) return Error::InvalidHandshake;
    if(!SupportedVersion)return Error::UnsupportedVersion;
    for (const auto& Protocol : Protocols) {
        if (!ValidProtocol(Protocol)) return Error::InvalidOptions;
        if (Selected.empty() && std::find(Offered.begin(),Offered.end(),Protocol)!=Offered.end()) Selected=Protocol;
    }
    const std::string Input=Key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::array<unsigned char,EVP_MAX_MD_SIZE> Digest{};unsigned Count=0;std::array<unsigned char,29> Accept{};
    if(EVP_Digest(Input.data(),Input.size(),Digest.data(),&Count,EVP_sha1(),nullptr)!=1 || Count!=20 || EVP_EncodeBlock(Accept.data(),Digest.data(),20)!=28) return Error::RandomFailed;
    Wire="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+std::string(reinterpret_cast<const char*>(Accept.data()),28)+"\r\n";
    if(!Selected.empty())Wire+="Sec-WebSocket-Protocol: "+Selected+"\r\n";
    Wire+="\r\n";return Error::None;
}

Handshake::Handshake(std::string ExpectedAccept, std::vector<std::string> Protocols)
    : Expected(std::move(ExpectedAccept)), Protocols(std::move(Protocols)) {}

void Handshake::ReadLine() {
    if (First) {
        First = false;
        if (Line.size() < 13 || Line.substr(0, 13) != "HTTP/1.1 101 " || !FieldValue(std::string_view(Line).substr(13)))
            Failure = Error::InvalidHandshake;
        return;
    }
    if (Line.empty()) {
        if (!Upgrade || !Connection || !Accept) Failure = Error::InvalidHandshake;
        else Complete = true;
        return;
    }
    if (++HeaderCount > 100) { Failure = Error::InvalidHandshake; return; }
    const auto Colon = Line.find(':');
    if (Colon == std::string::npos || !Token(std::string_view(Line).substr(0, Colon))) { Failure = Error::InvalidHandshake; return; }
    const auto Name = Lower(std::string_view(Line).substr(0, Colon));
    const auto Value = Trim(std::string_view(Line).substr(Colon + 1));
    if (!FieldValue(Value)) { Failure = Error::InvalidHandshake; return; }
    if (Name == "upgrade") {
        if (Upgrade || Lower(Value) != "websocket") Failure = Error::InvalidHandshake;
        else Upgrade = true;
    } else if (Name == "connection") {
        size_t Start = 0;
        for (;;) {
            const auto Comma = Value.find(',', Start);
            const auto Part = Trim(Value.substr(Start, Comma == std::string_view::npos ? Comma : Comma - Start));
            if (!Token(Part) || Lower(Part) == "close") { Failure = Error::InvalidHandshake; return; }
            if (Lower(Part) == "upgrade") Connection = true;
            if (Comma == std::string_view::npos) break;
            Start = Comma + 1;
        }
    } else if (Name == "sec-websocket-accept") {
        if (Accept || Value != Expected) Failure = Error::InvalidHandshake;
        else Accept = true;
    } else if (Name == "sec-websocket-protocol") {
        if (Protocol || !Token(Value) || std::find(Protocols.begin(), Protocols.end(), Value) == Protocols.end()) Failure = Error::InvalidHandshake;
        else { Protocol = true; Selected.assign(Value); }
    } else if (Name == "sec-websocket-extensions" || Name == "content-length" || Name == "transfer-encoding") {
        Failure = Error::InvalidHandshake;
    }
}

FeedResult Handshake::Feed(std::string_view Bytes) {
    size_t Consumed = 0;
    while (Consumed < Bytes.size() && !Complete && Failure == Error::None) {
        const auto Byte = static_cast<unsigned char>(Bytes[Consumed++]);
        if (++TotalBytes > HeaderCeiling) { Failure = Error::InvalidHandshake; break; }
        if (PendingCr) {
            PendingCr = false;
            if (Byte != '\n') { Failure = Error::InvalidHandshake; break; }
            ReadLine();
            Line.clear();
        } else if (Byte == '\r') PendingCr = true;
        else if (Byte == '\n' || (Byte < 32 && Byte != '\t') || Byte == 127 || Line.size() >= 8190) Failure = Error::InvalidHandshake;
        else Line.push_back(char(Byte));
    }
    return {Failure != Error::None ? ParseStatus::Failed : Complete ? ParseStatus::Event : ParseStatus::NeedMore, Consumed, Failure};
}

Parser::Parser(size_t MaxMessageBytes, size_t MaxFrameBytes, bool ClientFrames) : MaxMessage(MaxMessageBytes), MaxFrame(MaxFrameBytes), ClientFrames(ClientFrames) {
    if (!MaxMessage || MaxMessage > MessageCeiling || !MaxFrame || MaxFrame > MaxMessage) Failure = Error::InvalidOptions;
}

void Parser::BeginPayload() {
    const bool ControlFrame = FrameCode >= 8;
    uint64_t Length = Header[1] & 127;
    if (Length >= 126) {
        const size_t Extended = Length == 126 ? 2 : 8;
        Length = 0;
        for (size_t Index = 0; Index < Extended; ++Index) Length = (Length << 8) | Header[Index + 2];
        if ((Extended == 2 && Length < 126) || (Extended == 8 && (Length < 65536 || (Header[2] & 128)))) {
            Failure = Error::ProtocolError; return;
        }
    }
    if (ControlFrame && Length > 125) { Failure = Error::ProtocolError; return; }
    if (!ControlFrame && (Length > MaxFrame || Length > MaxMessage - Message.size())) {
        Failure = Error::MessageTooLarge; return;
    }
    // All comparisons precede narrowing and allocation.
    if (Length > std::numeric_limits<size_t>::max()) { Failure = Error::MessageTooLarge; return; }
    Remaining = size_t(Length);
    PayloadOffset = 0;
    Payload = true;
}

void Parser::EndFrame() {
    if (FrameCode >= 8) {
        Result = {};
        if (FrameCode == 8) {
            if (Control.size() == 1) { Failure = Error::ProtocolError; return; }
            Result.Kind = EventKind::Close;
            if (Control.size() >= 2) {
                Result.CloseCode = uint16_t((uint16_t(static_cast<uint8_t>(Control[0])) << 8) | static_cast<uint8_t>(Control[1]));
                Failure = ValidateClose(Result.CloseCode, std::string_view(Control).substr(2));
                if (Failure != Error::None) return;
                Control.erase(0, 2);
            }
        } else Result.Kind = FrameCode == 9 ? EventKind::Ping : EventKind::Pong;
        Result.Data = std::move(Control);
        Control.clear();
        Ready = true;
    } else if (Fin) {
        if (MessageCode == 1 && !ValidUtf8(Message)) { Failure = Error::InvalidUtf8; return; }
        Result = {};
        Result.Kind = MessageCode == 1 ? EventKind::Text : EventKind::Binary;
        Result.Data = std::move(Message);
        Message.clear();
        MessageCode = 0;
        Fragments = 0;
        Ready = true;
    }
    Payload = false;
    HeaderBytes = 0;
    HeaderNeeded = 2;
}

FeedResult Parser::Feed(std::string_view Bytes) {
    if (Closed && Failure == Error::None) Failure = Error::ProtocolError;
    size_t Consumed = 0;
    while (!Ready && Failure == Error::None && (Consumed < Bytes.size() || (Payload && !Remaining))) {
        if (!Payload) {
            Header[HeaderBytes++] = static_cast<uint8_t>(Bytes[Consumed++]);
            if (HeaderBytes == 2) {
                Fin = (Header[0] & 128) != 0;
                FrameCode = Header[0] & 15;
                if ((Header[0] & 0x70) || (bool(Header[1] & 0x80) != ClientFrames) ||
                    !(FrameCode == 0 || FrameCode == 1 || FrameCode == 2 || FrameCode == 8 || FrameCode == 9 || FrameCode == 10) ||
                    (FrameCode >= 8 && !Fin)) { Failure = Error::ProtocolError; break; }
                if (FrameCode < 8) {
                    if ((FrameCode == 0 && !MessageCode) || (FrameCode != 0 && MessageCode)) { Failure = Error::ProtocolError; break; }
                    if (FrameCode != 0) MessageCode = FrameCode;
                    if (++Fragments > 1024) { Failure = Error::MessageTooLarge; break; }
                }
                const auto Length = Header[1] & 127;
                if (FrameCode >= 8 && Length > 125) { Failure = Error::ProtocolError; break; }
                HeaderNeeded = (Length < 126 ? 2 : Length == 126 ? 4 : 10) + (ClientFrames ? 4 : 0);
            }
            if (HeaderBytes == HeaderNeeded) BeginPayload();
        } else {
            const size_t Count = std::min(Remaining, Bytes.size() - Consumed);
            auto& Destination = FrameCode >= 8 ? Control : Message;
            if (Count) {
                const size_t Start=Destination.size();Destination.append(Bytes.data()+Consumed,Count);
                if(ClientFrames) for(size_t Index=0;Index<Count;++Index) Destination[Start+Index]=char(static_cast<uint8_t>(Destination[Start+Index])^Header[HeaderNeeded-4+(PayloadOffset+Index)%4]);
                PayloadOffset+=Count;
            }
            Consumed += Count;
            Remaining -= Count;
            if (!Remaining) EndFrame();
        }
    }
    return {Failure != Error::None ? ParseStatus::Failed : Ready ? ParseStatus::Event : ParseStatus::NeedMore, Consumed, Failure};
}

Event Parser::TakeEvent() {
    if (!Ready) return {};
    Ready = false;
    if (Result.Kind == EventKind::Close) Closed = true;
    return std::move(Result);
}

const char* GetErrorName(Error Code) {
    switch (Code) {
    case Error::None: return "None";
    case Error::InvalidUrl: return "InvalidUrl";
    case Error::InvalidHandshake: return "InvalidHandshake";
    case Error::ProtocolError: return "ProtocolError";
    case Error::InvalidUtf8: return "InvalidUtf8";
    case Error::MessageTooLarge: return "MessageTooLarge";
    case Error::RandomFailed: return "RandomFailed";
    case Error::InvalidOptions: return "InvalidOptions";
    case Error::UnsupportedVersion: return "UnsupportedVersion";
    }
    return "ProtocolError";
}
}
