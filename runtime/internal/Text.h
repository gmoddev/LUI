#pragma once
#include <cstdint>
#include <string>

namespace LuiText {
constexpr size_t MaxBytes = 65534;
constexpr int MaxCharacters = 65534;
// Strict UTF-8 scalar offsets, never byte or UTF-16 offsets. Embedded NUL,
// surrogates and noncanonical encodings are refused at the text boundary.
inline bool Count(const std::string& Text, int& Characters) {
    Characters = 0;
    if (Text.size() > MaxBytes) return false;
    for (size_t Index = 0; Index < Text.size();) {
        const auto First = static_cast<unsigned char>(Text[Index]);
        if (!First) return false;
        int Bytes = First < 0x80 ? 1 : First >= 0xC2 && First <= 0xDF ? 2 :
            First >= 0xE0 && First <= 0xEF ? 3 : First >= 0xF0 && First <= 0xF4 ? 4 : 0;
        if (!Bytes || Index + Bytes > Text.size()) return false;
        for (int Offset = 1; Offset < Bytes; ++Offset)
            if ((static_cast<unsigned char>(Text[Index + Offset]) & 0xC0) != 0x80) return false;
        if (Bytes >= 3) {
            const auto Second = static_cast<unsigned char>(Text[Index + 1]);
            if ((First == 0xE0 && Second < 0xA0) || (First == 0xED && Second >= 0xA0) ||
                (First == 0xF0 && Second < 0x90) || (First == 0xF4 && Second >= 0x90)) return false;
        }
        Index += Bytes;
        if (++Characters > MaxCharacters) return false;
    }
    return true;
}
inline bool Capture(const char* Source, std::string& Text, size_t Limit = MaxBytes) {
    if (!Source) return false;
    size_t Bytes = 0; while (Bytes <= Limit && Source[Bytes]) ++Bytes;
    if (Bytes > Limit) return false;
    Text.assign(Source, Bytes); int Characters; return Count(Text, Characters);
}
}
