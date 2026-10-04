#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Lui::Http {
enum class MessageKind { Request, Response };
enum class ParseStatus { NeedMore, Complete, Failed };
enum class ErrorCode {
    None, InvalidLimits, InvalidStartLine, InvalidHeader, InvalidHost, InvalidFraming,
    InvalidChunk, ForbiddenTrailer, LimitExceeded, UnexpectedEof, UnsupportedFeature
};

struct Limits {
    size_t LineBytes = 8192; // Includes CRLF; applies to start, field, and chunk-size lines.
    size_t HeaderBytes = 32768; // Headers and trailers, including CRLF terminators.
    size_t HeaderCount = 100; // Headers and trailers together.
    size_t BodyBytes = 8 * 1024 * 1024;
    size_t ChunkMetadataBytes = 32768; // Size lines, extensions, data CRLF, and trailers.
    size_t ChunkCount = 65536; // Includes the terminating zero chunk.
};

struct Field { std::string Name; std::string Value; }; // Names normalized to ASCII lowercase.
struct Message {
    std::string Method;
    std::string Target; // Original encoded origin-form, or OPTIONS *; never decoded here.
    unsigned StatusCode = 0;
    std::string Reason;
    std::vector<Field> Headers; // Preserve order and duplicates; never comma-merge fields.
    std::vector<Field> Trailers; // Kept separate from headers.
    std::string Body; // Binary bytes, including NUL.
    bool KeepAlive = true;
};

struct FeedResult { ParseStatus Status; size_t Consumed; ErrorCode Error; };

// One parser per message. Completed/failed parsers consume no further input.
// On completion the caller retains the unconsumed suffix for the next message.
// On failure the connection MUST be closed; no recovery/resynchronization is supported.
class Parser {
public:
    explicit Parser(MessageKind Kind, Limits Bounds = {}, std::string_view ResponseMethod = "GET");
    FeedResult Feed(std::string_view Bytes, bool EndOfStream = false);
    const Message* GetResult() const;

private:
    enum class State { StartLine, Headers, FixedBody, ChunkLine, ChunkBody, ChunkEnd, Trailers, CloseBody, Complete, Failed };
    MessageKind Kind;
    Limits Bounds;
    std::string ResponseMethod;
    State Current = State::StartLine;
    ErrorCode Error = ErrorCode::None;
    Message Parsed;
    std::string Line;
    bool PendingCr = false;
    size_t LineBytes = 0;
    size_t HeaderBytes = 0;
    size_t HeaderCount = 0;
    size_t MetadataBytes = 0;
    size_t Chunks = 0;
    size_t Remaining = 0;
    unsigned ChunkEndBytes = 0;
    bool HasLength = false;
    bool HasTransfer = false;
    bool HasHost = false;
    std::vector<std::string> ConnectionOptions;

    void Fail(ErrorCode Code);
    bool ChargeMetadata();
    void ReadLine(char Byte);
    void StartLine();
    void Header(bool IsTrailer);
    void Framing();
    void ChunkLine();
    bool Append(std::string_view Bytes);
};

const char* GetErrorName(ErrorCode Code);
}
