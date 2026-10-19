#pragma once
#include "Parser.h"

namespace Lui::Http {
struct Url { std::string Host, Authority, Target; unsigned short Port = 80; bool Secure = false; };
// HTTP/HTTPS. No credentials, fragments, proxy forms, or implicit URL decoding.
ErrorCode ParseUrl(std::string_view Text, Url& Result);
// Own framing fields; caller fields may not override Host, length, or connection policy.
ErrorCode SerializeRequest(const Message& Input, std::string_view Authority, std::string& Wire, Limits Bounds = {});
ErrorCode SerializeResponse(const Message& Input, std::string& Wire, bool Head = false, Limits Bounds = {});
}
