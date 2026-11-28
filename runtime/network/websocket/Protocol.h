#pragma once

#include "../http/Serializer.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Lui::WebSocket {
enum class Error {
    None, InvalidUrl, InvalidHandshake, ProtocolError, InvalidUtf8,
    MessageTooLarge, RandomFailed, InvalidOptions, UnsupportedVersion
};
enum class ParseStatus { NeedMore, Event, Failed };
struct FeedResult { ParseStatus Status; size_t Consumed; Error Code; };
enum class Opcode : uint8_t { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };
enum class EventKind { Text, Binary, Ping, Pong, Close };
struct Event { EventKind Kind = EventKind::Text; std::string Data; uint16_t CloseCode = 1005; };

// URL parsing retains existing HTTP target/authority validation and maps only
// ws/wss to the corresponding HTTP policy/TLS transport profile.
Error ParseUrl(std::string_view Text, Lui::Http::Url& Result);
Error BuildHandshake(const Lui::Http::Url& Url, const std::vector<Lui::Http::Field>& Headers,
    const std::vector<std::string>& Protocols, std::string_view Origin,
    std::string& Wire, std::string& ExpectedAccept);
bool ValidUtf8(std::string_view Text);
Error ValidateClose(uint16_t Code, std::string_view Reason);
Error EncodeClientFrame(Opcode Code, std::string_view Payload, std::string& Wire, bool Fin = true);
Error EncodeServerFrame(Opcode Code, std::string_view Payload, std::string& Wire, bool Fin = true);
Error BuildServerHandshake(const Lui::Http::Message& Request, const std::vector<std::string>& Protocols,
    std::string& Wire, std::string& Selected, std::string& Origin);
bool ValidProtocol(std::string_view Text);
const char* GetErrorName(Error Code);

// Dedicated header-only Upgrade parser. The existing HTTP parser deliberately
// rejects protocol switching; its ordinary HTTP contracts remain unchanged.
// A complete handshake returns Event and consumes exactly through CRLF CRLF.
class Handshake {
public:
    Handshake(std::string ExpectedAccept, std::vector<std::string> Protocols = {});
    FeedResult Feed(std::string_view Bytes);
    const std::string& SelectedProtocol() const { return Selected; }
private:
    std::string Expected, Selected, Line;
    std::vector<std::string> Protocols;
    size_t TotalBytes = 0, HeaderCount = 0;
    bool PendingCr = false, First = true, Upgrade = false, Connection = false;
    bool Accept = false, Protocol = false, Complete = false;
    Error Failure = Error::None;
    void ReadLine();
};

// Incremental peer-role frame parser. It retains one fragmented application
// message plus at most one 125-byte control payload. Feed stops at each event;
// callers must take it before feeding more input. ClientFrames requires masks;
// the default role rejects masks. No extensions or reserved bits/opcodes.
class Parser {
public:
    explicit Parser(size_t MaxMessageBytes = 1024 * 1024, size_t MaxFrameBytes = 1024 * 1024, bool ClientFrames = false);
    FeedResult Feed(std::string_view Bytes);
    Event TakeEvent();
private:
    size_t MaxMessage, MaxFrame;
    std::array<uint8_t, 14> Header{};
    size_t HeaderBytes = 0, HeaderNeeded = 2;
    size_t Remaining = 0, Fragments = 0;
    uint8_t FrameCode = 0, MessageCode = 0;
    bool Fin = false, Payload = false, Ready = false, Closed = false;
    bool ClientFrames = false;
    size_t PayloadOffset = 0;
    std::string Message, Control;
    Event Result;
    Error Failure = Error::None;
    void BeginPayload();
    void EndFrame();
};
}
