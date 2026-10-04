#include "Parser.h"
#include <asio/ip/address_v6.hpp>

#include <algorithm>
#include <limits>

namespace Lui::Http {
namespace {
bool TokenByte(unsigned char Byte) {
    return (Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') || (Byte >= '0' && Byte <= '9') ||
        std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(Byte)) != std::string_view::npos;
}
bool Token(std::string_view Text) {
    return !Text.empty() && std::all_of(Text.begin(), Text.end(), TokenByte);
}
bool FieldByte(unsigned char Byte) { return Byte == '\t' || (Byte >= 32 && Byte != 127); }
bool Space(char Byte) { return Byte == ' ' || Byte == '\t'; }
std::string_view Trim(std::string_view Text) {
    while (!Text.empty() && Space(Text.front())) Text.remove_prefix(1);
    while (!Text.empty() && Space(Text.back())) Text.remove_suffix(1);
    return Text;
}
std::string Lower(std::string_view Text) {
    std::string Result(Text);
    for (char& Byte : Result) if (Byte >= 'A' && Byte <= 'Z') Byte += 'a' - 'A';
    return Result;
}
int Hex(unsigned char Byte) {
    if (Byte >= '0' && Byte <= '9') return Byte - '0';
    if (Byte >= 'a' && Byte <= 'f') return Byte - 'a' + 10;
    if (Byte >= 'A' && Byte <= 'F') return Byte - 'A' + 10;
    return -1;
}
bool Decimal(std::string_view Text, size_t& Value) {
    if (Text.empty()) return false;
    Value = 0;
    for (unsigned char Byte : Text) {
        if (Byte < '0' || Byte > '9' || Value > (std::numeric_limits<size_t>::max() - (Byte - '0')) / 10) return false;
        Value = Value * 10 + Byte - '0';
    }
    return true;
}
bool Authority(std::string_view Text) {
    if (Text.empty()) return false;
    std::string_view Host, Port;
    bool HasPort = false;
    if (Text.front() == '[') {
        size_t End = Text.find(']');
        if (End == std::string_view::npos) return false;
        Host = Text.substr(1, End - 1);
        if (Host.find('%') != std::string_view::npos) return false; // No scoped literals or IPvFuture in this profile.
        asio::error_code AddressError;
        asio::ip::make_address_v6(std::string(Host), AddressError);
        if (AddressError) return false;
        if (End + 1 < Text.size()) {
            if (Text[End + 1] != ':') return false;
            Port = Text.substr(End + 2);
            HasPort = true;
        }
    } else {
        size_t Colon = Text.find(':');
        Host = Text.substr(0, Colon);
        if (Host.empty() || !std::all_of(Host.begin(), Host.end(), [](unsigned char Byte) {
            return (Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') || (Byte >= '0' && Byte <= '9') ||
                Byte == '-' || Byte == '.';
        })) return false;
        if (Colon != std::string_view::npos) { Port = Text.substr(Colon + 1); HasPort = true; }
    }
    size_t Number = 0;
    return !HasPort || (Decimal(Port, Number) && Number <= 65535);
}
bool ForbiddenTrailer(std::string_view Name) {
    // Only understood metadata is admitted; unknown trailers cannot influence routing or content processing.
    return Name != "content-digest" && Name != "repr-digest" && Name != "server-timing";
}
bool FramingOption(std::string_view Name) {
    return Name == "content-length" || Name == "transfer-encoding" || Name == "host" || Name == "connection" ||
        Name == "trailer" || Name == "te";
}
bool List(std::string_view Value, std::vector<std::string>& Values) {
    while (true) {
        size_t Comma = Value.find(',');
        std::string_view Item = Trim(Value.substr(0, Comma));
        if (!Token(Item)) return false;
        Values.push_back(Lower(Item));
        if (Comma == std::string_view::npos) return true;
        Value.remove_prefix(Comma + 1);
    }
}
bool Extensions(std::string_view Text) {
    size_t Index = 0;
    auto Whitespace = [&] { while (Index < Text.size() && Space(Text[Index])) ++Index; };
    auto ReadToken = [&] {
        size_t Start = Index;
        while (Index < Text.size() && TokenByte(static_cast<unsigned char>(Text[Index]))) ++Index;
        return Index != Start;
    };
    while (Index < Text.size()) {
        Whitespace();
        if (Index == Text.size() || Text[Index++] != ';') return false;
        Whitespace();
        if (!ReadToken()) return false;
        size_t AfterName = Index;
        Whitespace();
        if (Index < Text.size() && Text[Index] == '=') {
            ++Index;
            Whitespace();
            if (Index == Text.size()) return false;
            if (Text[Index] == '"') {
                ++Index;
                bool Closed = false;
                while (Index < Text.size()) {
                    unsigned char Byte = static_cast<unsigned char>(Text[Index++]);
                    if (Byte == '"') { Closed = true; break; }
                    if (Byte == '\\') {
                        if (Index == Text.size() || !FieldByte(static_cast<unsigned char>(Text[Index++]))) return false;
                    } else if (!FieldByte(Byte)) return false;
                }
                if (!Closed) return false;
            } else if (!ReadToken()) return false;
        } else Index = AfterName; // Whitespace must belong to the next ';', not trail the line.
    }
    return true;
}
}

Parser::Parser(MessageKind Kind, Limits Bounds, std::string_view ResponseMethod)
    : Kind(Kind), Bounds(Bounds), ResponseMethod(ResponseMethod) {
    if (Bounds.LineBytes < 2 || Bounds.LineBytes > 65536 || Bounds.HeaderBytes < 2 || Bounds.HeaderBytes > 1024 * 1024 ||
        Bounds.HeaderCount == 0 || Bounds.HeaderCount > 1024 || Bounds.BodyBytes > 64 * 1024 * 1024 ||
        Bounds.ChunkMetadataBytes < 2 || Bounds.ChunkMetadataBytes > 1024 * 1024 ||
        Bounds.ChunkCount == 0 || Bounds.ChunkCount > 1024 * 1024 || !Token(ResponseMethod)) Fail(ErrorCode::InvalidLimits);
}

void Parser::Fail(ErrorCode Code) { Error = Code; Current = State::Failed; }
bool Parser::ChargeMetadata() {
    if (++MetadataBytes > Bounds.ChunkMetadataBytes) { Fail(ErrorCode::LimitExceeded); return false; }
    return true;
}

void Parser::ReadLine(char Byte) {
    if (++LineBytes > Bounds.LineBytes) { Fail(ErrorCode::LimitExceeded); return; }
    if (Current == State::Headers || Current == State::Trailers) {
        if (++HeaderBytes > Bounds.HeaderBytes) { Fail(ErrorCode::LimitExceeded); return; }
    }
    if ((Current == State::ChunkLine || Current == State::Trailers) && !ChargeMetadata()) return;
    if (PendingCr) {
        if (Byte != '\n') { Fail(ErrorCode::InvalidFraming); return; }
        PendingCr = false;
        LineBytes = 0;
        switch (Current) {
        case State::StartLine: StartLine(); break;
        case State::Headers:
            if (Line.empty()) Framing(); else Header(false);
            break;
        case State::ChunkLine: ChunkLine(); break;
        case State::Trailers:
            if (Line.empty()) Current = State::Complete; else Header(true);
            break;
        default: Fail(ErrorCode::InvalidFraming); break;
        }
        Line.clear();
    } else if (Byte == '\r') PendingCr = true;
    else if (Byte == '\n') Fail(ErrorCode::InvalidFraming);
    else Line.push_back(Byte);
}

void Parser::StartLine() {
    if (Kind == MessageKind::Request) {
        size_t First = Line.find(' '), Last = Line.rfind(' ');
        if (First == std::string::npos || First == Last || !Token(std::string_view(Line).substr(0, First)) ||
            Line.substr(Last + 1) != "HTTP/1.1") { Fail(ErrorCode::InvalidStartLine); return; }
        Parsed.Method = Line.substr(0, First);
        Parsed.Target = Line.substr(First + 1, Last - First - 1);
        if (Parsed.Method == "CONNECT") { Fail(ErrorCode::UnsupportedFeature); return; }
        if (Parsed.Target.empty()) { Fail(ErrorCode::InvalidStartLine); return; }
        for (size_t Index = 0; Index < Parsed.Target.size(); ++Index) {
            unsigned char Byte = static_cast<unsigned char>(Parsed.Target[Index]);
            bool UriByte = (Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') || (Byte >= '0' && Byte <= '9') ||
                std::string_view("-._~!$&'()*+,;=:@/?%").find(static_cast<char>(Byte)) != std::string_view::npos;
            if (!UriByte) { Fail(ErrorCode::InvalidStartLine); return; }
            if (Byte == '%') {
                if (Index + 2 >= Parsed.Target.size() || Hex(Parsed.Target[Index + 1]) < 0 || Hex(Parsed.Target[Index + 2]) < 0) {
                    Fail(ErrorCode::InvalidStartLine); return;
                }
                Index += 2;
            }
        }
        if (Parsed.Target.front() != '/' && !(Parsed.Target == "*" && Parsed.Method == "OPTIONS")) {
            Fail(ErrorCode::UnsupportedFeature); return;
        }
    } else {
        if (Line.size() < 13 || Line.compare(0, 9, "HTTP/1.1 ") != 0 || Line[12] != ' ' ||
            !std::all_of(Line.begin() + 9, Line.begin() + 12, [](char Byte) { return Byte >= '0' && Byte <= '9'; }) ||
            !std::all_of(Line.begin() + 13, Line.end(), FieldByte)) { Fail(ErrorCode::InvalidStartLine); return; }
        Parsed.StatusCode = unsigned((Line[9] - '0') * 100 + (Line[10] - '0') * 10 + Line[11] - '0');
        if (Parsed.StatusCode < 100 || Parsed.StatusCode > 599) { Fail(ErrorCode::InvalidStartLine); return; }
        Parsed.Reason = Line.substr(13);
        if (Parsed.StatusCode == 101 || (ResponseMethod == "CONNECT" && Parsed.StatusCode >= 200 && Parsed.StatusCode < 300)) {
            Fail(ErrorCode::UnsupportedFeature); return;
        }
    }
    Current = State::Headers;
}

void Parser::Header(bool IsTrailer) {
    if (++HeaderCount > Bounds.HeaderCount) { Fail(ErrorCode::LimitExceeded); return; }
    size_t Colon = Line.find(':');
    if (Colon == std::string::npos || !Token(std::string_view(Line).substr(0, Colon)) ||
        !std::all_of(Line.begin() + Colon + 1, Line.end(), FieldByte)) { Fail(ErrorCode::InvalidHeader); return; }
    Field Entry{Lower(std::string_view(Line).substr(0, Colon)), std::string(Trim(std::string_view(Line).substr(Colon + 1)))};
    if (IsTrailer) {
        if (ForbiddenTrailer(Entry.Name) || std::find(ConnectionOptions.begin(), ConnectionOptions.end(), Entry.Name) != ConnectionOptions.end()) {
            Fail(ErrorCode::ForbiddenTrailer); return;
        }
        Parsed.Trailers.push_back(std::move(Entry));
        return;
    }
    if (Entry.Name == "content-length") {
        if (HasLength || HasTransfer || !Decimal(Entry.Value, Remaining)) { Fail(ErrorCode::InvalidFraming); return; }
        HasLength = true;
    } else if (Entry.Name == "transfer-encoding") {
        if (HasTransfer || HasLength || Lower(Entry.Value) != "chunked") { Fail(ErrorCode::InvalidFraming); return; }
        HasTransfer = true;
    } else if (Entry.Name == "host") {
        if (HasHost || !Authority(Entry.Value)) { Fail(ErrorCode::InvalidHost); return; }
        HasHost = true;
    } else if (Entry.Name == "connection") {
        size_t Start = ConnectionOptions.size();
        if (!List(Entry.Value, ConnectionOptions)) { Fail(ErrorCode::InvalidHeader); return; }
        for (size_t Index = Start; Index < ConnectionOptions.size(); ++Index) {
            const auto& Option = ConnectionOptions[Index];
            if (Option == "upgrade") { Fail(ErrorCode::UnsupportedFeature); return; }
            if (FramingOption(Option)) { Fail(ErrorCode::InvalidFraming); return; }
            if (Option == "close") Parsed.KeepAlive = false;
        }
    } else if (Entry.Name == "upgrade" || (Kind == MessageKind::Request && Entry.Name == "expect")) {
        Fail(ErrorCode::UnsupportedFeature); return;
    } else if (Entry.Name == "trailer") {
        std::vector<std::string> Names;
        if (!List(Entry.Value, Names)) { Fail(ErrorCode::InvalidHeader); return; }
        if (std::any_of(Names.begin(), Names.end(), ForbiddenTrailer)) { Fail(ErrorCode::ForbiddenTrailer); return; }
    }
    Parsed.Headers.push_back(std::move(Entry));
}

void Parser::Framing() {
    if (Kind == MessageKind::Request && !HasHost) { Fail(ErrorCode::InvalidHost); return; }
    if (Kind == MessageKind::Response) {
        bool ProhibitedFraming = Parsed.StatusCode < 200 || Parsed.StatusCode == 204;
        if (ProhibitedFraming && (HasLength || HasTransfer)) { Fail(ErrorCode::InvalidFraming); return; }
        if (ProhibitedFraming || Parsed.StatusCode == 304 || ResponseMethod == "HEAD") { Current = State::Complete; return; }
    }
    if (HasTransfer) Current = State::ChunkLine;
    else if (HasLength) {
        if (Kind == MessageKind::Response && Parsed.StatusCode == 205 && Remaining) { Fail(ErrorCode::InvalidFraming); return; }
        if (Remaining > Bounds.BodyBytes) { Fail(ErrorCode::LimitExceeded); return; }
        Parsed.Body.reserve(Remaining);
        Current = Remaining ? State::FixedBody : State::Complete;
    } else if (Kind == MessageKind::Request) Current = State::Complete;
    else { Parsed.KeepAlive = false; Current = State::CloseBody; }
}

void Parser::ChunkLine() {
    if (++Chunks > Bounds.ChunkCount) { Fail(ErrorCode::LimitExceeded); return; }
    size_t Index = 0, Size = 0;
    while (Index < Line.size() && Hex(static_cast<unsigned char>(Line[Index])) >= 0) {
        size_t Digit = size_t(Hex(static_cast<unsigned char>(Line[Index++])));
        if (Size > (std::numeric_limits<size_t>::max() - Digit) / 16) { Fail(ErrorCode::InvalidChunk); return; }
        Size = Size * 16 + Digit;
    }
    if (Index == 0 || !Extensions(std::string_view(Line).substr(Index))) { Fail(ErrorCode::InvalidChunk); return; }
    if (Kind == MessageKind::Response && Parsed.StatusCode == 205 && Size) { Fail(ErrorCode::InvalidFraming); return; }
    if (Size > Bounds.BodyBytes - Parsed.Body.size()) { Fail(ErrorCode::LimitExceeded); return; }
    Remaining = Size;
    Current = Size ? State::ChunkBody : State::Trailers;
}

bool Parser::Append(std::string_view Bytes) {
    if (Kind == MessageKind::Response && Parsed.StatusCode == 205 && !Bytes.empty()) { Fail(ErrorCode::InvalidFraming); return false; }
    if (Bytes.size() > Bounds.BodyBytes - Parsed.Body.size()) { Fail(ErrorCode::LimitExceeded); return false; }
    Parsed.Body.append(Bytes.data(), Bytes.size());
    return true;
}

FeedResult Parser::Feed(std::string_view Bytes, bool EndOfStream) {
    size_t Consumed = 0;
    while (Consumed < Bytes.size() && Current != State::Complete && Current != State::Failed) {
        if (Current == State::FixedBody || Current == State::ChunkBody || Current == State::CloseBody) {
            size_t Count = Current == State::CloseBody ? Bytes.size() - Consumed : std::min(Remaining, Bytes.size() - Consumed);
            if (!Append(Bytes.substr(Consumed, Count))) break;
            Consumed += Count;
            if (Current != State::CloseBody) {
                Remaining -= Count;
                if (!Remaining) {
                    if (Current == State::FixedBody) Current = State::Complete;
                    else { Current = State::ChunkEnd; ChunkEndBytes = 0; }
                }
            }
        } else if (Current == State::ChunkEnd) {
            char Byte = Bytes[Consumed++];
            if (!ChargeMetadata()) break;
            if (Byte != (ChunkEndBytes == 0 ? '\r' : '\n')) { Fail(ErrorCode::InvalidChunk); break; }
            if (++ChunkEndBytes == 2) Current = State::ChunkLine;
        } else ReadLine(Bytes[Consumed++]);
    }
    if (EndOfStream && Current != State::Complete && Current != State::Failed) {
        if (Current == State::CloseBody) Current = State::Complete;
        else Fail(ErrorCode::UnexpectedEof);
    }
    ParseStatus Status = Current == State::Complete ? ParseStatus::Complete :
        Current == State::Failed ? ParseStatus::Failed : ParseStatus::NeedMore;
    return {Status, Consumed, Error};
}

const Message* Parser::GetResult() const { return Current == State::Complete ? &Parsed : nullptr; }
const char* GetErrorName(ErrorCode Code) {
    switch (Code) {
    case ErrorCode::None: return "None";
    case ErrorCode::InvalidLimits: return "InvalidLimits";
    case ErrorCode::InvalidStartLine: return "InvalidStartLine";
    case ErrorCode::InvalidHeader: return "InvalidHeader";
    case ErrorCode::InvalidHost: return "InvalidHost";
    case ErrorCode::InvalidFraming: return "InvalidFraming";
    case ErrorCode::InvalidChunk: return "InvalidChunk";
    case ErrorCode::ForbiddenTrailer: return "ForbiddenTrailer";
    case ErrorCode::LimitExceeded: return "LimitExceeded";
    case ErrorCode::UnexpectedEof: return "UnexpectedEof";
    case ErrorCode::UnsupportedFeature: return "UnsupportedFeature";
    }
    return "Unknown";
}
}
