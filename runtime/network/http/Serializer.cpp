#include "Serializer.h"
#include <algorithm>

namespace Lui::Http {
namespace {
std::string Lower(std::string_view Text) {
    std::string Value(Text);
    for (char& Byte : Value) if (Byte >= 'A' && Byte <= 'Z') Byte += 'a' - 'A';
    return Value;
}
bool Managed(std::string_view Name) {
    return Name == "host" || Name == "content-length" || Name == "transfer-encoding" ||
        Name == "connection" || Name == "trailer" || Name == "te" || Name == "upgrade" ||
        Name == "expect" || Name == "keep-alive" || Name == "proxy-connection";
}
bool Token(std::string_view Text) {
    return !Text.empty() && std::all_of(Text.begin(), Text.end(), [](unsigned char Byte) {
        return (Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') || (Byte >= '0' && Byte <= '9') ||
            std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(Byte)) != std::string_view::npos;
    });
}
bool Value(std::string_view Text) {
    return std::all_of(Text.begin(), Text.end(), [](unsigned char Byte) { return Byte == '\t' || (Byte >= 32 && Byte != 127); });
}
ErrorCode Fields(const Message& Input, std::string& Header, const Limits& Bounds) {
    if (!Input.Trailers.empty()) return ErrorCode::UnsupportedFeature;
    if (Input.Headers.size() > Bounds.HeaderCount) return ErrorCode::LimitExceeded;
    for (const auto& Field : Input.Headers) {
        if (!Token(Field.Name) || !Value(Field.Value)) return ErrorCode::InvalidHeader;
        if (Managed(Lower(Field.Name))) return ErrorCode::InvalidFraming;
        if (Field.Name.size() > Bounds.LineBytes || Field.Value.size() > Bounds.LineBytes ||
            Field.Name.size() + Field.Value.size() + 4 > Bounds.LineBytes ||
            Header.size() + Field.Name.size() + Field.Value.size() + 4 > Bounds.HeaderBytes)
            return ErrorCode::LimitExceeded;
        Header += Field.Name + ": " + Field.Value + "\r\n";
    }
    return ErrorCode::None;
}
ErrorCode Validate(MessageKind Kind, const std::string& Header, Limits Bounds, std::string_view Method) {
    Parser Check(Kind, Bounds, Method);
    auto Result = Check.Feed(Header);
    return Result.Status == ParseStatus::Failed ? Result.Error : ErrorCode::None;
}
}

ErrorCode SerializeRequest(const Message& Input, std::string_view Authority, std::string& Wire, Limits Bounds) {
    Wire.clear();
    if (Input.Body.size() > Bounds.BodyBytes) return ErrorCode::LimitExceeded;
    if (!Token(Input.Method) || !Value(Input.Target) || !Value(Authority)) return ErrorCode::InvalidStartLine;
    if ((Input.Method == "GET" || Input.Method == "HEAD") && !Input.Body.empty()) return ErrorCode::UnsupportedFeature;
    if (Input.Method.size() + Input.Target.size() + 12 > Bounds.LineBytes || Authority.size() + 8 > Bounds.LineBytes)
        return ErrorCode::LimitExceeded;
    std::string Header = Input.Method + " " + Input.Target + " HTTP/1.1\r\nHost: " + std::string(Authority) + "\r\n";
    auto Error = Fields(Input, Header, Bounds);
    if (Error != ErrorCode::None) return Error;
    if (!Input.Body.empty() || Input.Method == "POST" || Input.Method == "PUT" || Input.Method == "PATCH")
        Header += "Content-Length: " + std::to_string(Input.Body.size()) + "\r\n";
    Header += "Connection: close\r\n\r\n";
    Error = Validate(MessageKind::Request, Header, Bounds, Input.Method);
    if (Error != ErrorCode::None) return Error;
    Wire = std::move(Header);
    Wire += Input.Body;
    return ErrorCode::None;
}

ErrorCode SerializeResponse(const Message& Input, std::string& Wire, bool Head, Limits Bounds) {
    Wire.clear();
    if (Input.StatusCode < 200 || Input.StatusCode > 599) return ErrorCode::InvalidStartLine;
    if (!Value(Input.Reason)) return ErrorCode::InvalidStartLine;
    if (Input.Body.size() > Bounds.BodyBytes || Input.Reason.size() + 15 > Bounds.LineBytes) return ErrorCode::LimitExceeded;
    const bool NoContent = Input.StatusCode == 204 || Input.StatusCode == 205 || Input.StatusCode == 304;
    if (NoContent && !Input.Body.empty()) return ErrorCode::InvalidFraming;
    std::string Header = "HTTP/1.1 " + std::to_string(Input.StatusCode) + " " + Input.Reason + "\r\n";
    auto Error = Fields(Input, Header, Bounds);
    if (Error != ErrorCode::None) return Error;
    if (Input.StatusCode != 204 && Input.StatusCode != 304)
        Header += "Content-Length: " + std::to_string(Input.Body.size()) + "\r\n";
    Header += Input.KeepAlive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    Error = Validate(MessageKind::Response, Header, Bounds, Head ? "HEAD" : "GET");
    if (Error != ErrorCode::None) return Error;
    Wire = std::move(Header);
    if (!Head && !NoContent) Wire += Input.Body;
    return ErrorCode::None;
}

ErrorCode ParseUrl(std::string_view Text, Url& Result) {
    Result = {};
    if (Text.size() > 8192) return ErrorCode::LimitExceeded;
    if (Text.substr(0, 8) == "https://") { Result.Secure = true; Result.Port = 443; Text.remove_prefix(8); }
    else if (Text.substr(0, 7) == "http://") Text.remove_prefix(7);
    else return ErrorCode::UnsupportedFeature;
    if (Text.find('#') != std::string_view::npos) return ErrorCode::UnsupportedFeature;
    size_t End = Text.find_first_of("/?");
    auto Authority = Text.substr(0, End);
    std::string_view Port;
    if (!Authority.empty() && Authority.front() == '[') {
        size_t Bracket = Authority.find(']');
        if (Bracket == std::string_view::npos) return ErrorCode::InvalidHost;
        Result.Host = Authority.substr(1, Bracket - 1);
        if (Bracket + 1 < Authority.size()) {
            if (Authority[Bracket + 1] != ':') return ErrorCode::InvalidHost;
            Port = Authority.substr(Bracket + 2);
            if (Port.empty()) return ErrorCode::InvalidHost;
        }
    } else {
        size_t Colon = Authority.find(':');
        Result.Host = Authority.substr(0, Colon);
        if (Colon != std::string_view::npos) {
            Port = Authority.substr(Colon + 1);
            if (Port.empty()) return ErrorCode::InvalidHost;
        }
    }
    if (Result.Host.empty() || Result.Host.size() > 253) return ErrorCode::InvalidHost;
    if (!Port.empty()) {
        unsigned Number = 0;
        for (unsigned char Byte : Port) {
            if (Byte < '0' || Byte > '9' || Number > 6553) return ErrorCode::InvalidHost;
            Number = Number * 10 + Byte - '0';
        }
        if (!Number || Number > 65535) return ErrorCode::InvalidHost;
        Result.Port = static_cast<unsigned short>(Number);
    }
    Result.Authority = Authority;
    Result.Target = End == std::string_view::npos ? "/" : std::string(Text.substr(End));
    if (Result.Target.front() == '?') Result.Target.insert(0, "/");
    Message Probe;
    Probe.Method = "GET";
    Probe.Target = Result.Target;
    std::string Wire;
    return SerializeRequest(Probe, Result.Authority, Wire);
}
}
