#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
namespace Lui::Mobile {
inline std::string Encode(std::string_view Bytes) {
    constexpr char Digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string Result;
    for (size_t Index = 0; Index < Bytes.size(); Index += 3) {
        const auto Left = static_cast<unsigned char>(Bytes[Index]);
        const auto Mid = Index + 1 < Bytes.size() ? static_cast<unsigned char>(Bytes[Index + 1]) : 0;
        const auto Right = Index + 2 < Bytes.size() ? static_cast<unsigned char>(Bytes[Index + 2]) : 0;
        Result += Digits[Left >> 2]; Result += Digits[((Left & 3) << 4) | (Mid >> 4)];
        Result += Index + 1 < Bytes.size() ? Digits[((Mid & 15) << 2) | (Right >> 6)] : '=';
        Result += Index + 2 < Bytes.size() ? Digits[Right & 63] : '=';
    }
    return Result;
}
inline std::string Decode(std::string_view Text, size_t Limit = 1024 * 1024) {
    if (Text.size() % 4 || Text.size() / 4 * 3 > Limit + 2) throw std::runtime_error("Base64Limit");
    std::string Result;
    auto Digit = [](char Value) -> unsigned {
        if (Value >= 'A' && Value <= 'Z') return Value - 'A';
        if (Value >= 'a' && Value <= 'z') return Value - 'a' + 26;
        if (Value >= '0' && Value <= '9') return Value - '0' + 52;
        if (Value == '+') return 62;
        if (Value == '/') return 63;
        throw std::runtime_error("Base64");
    };
    for (size_t Index = 0; Index < Text.size(); Index += 4) {
        const unsigned A = Digit(Text[Index]), B = Digit(Text[Index + 1]);
        const bool C = Text[Index + 2] != '=', D = Text[Index + 3] != '=';
        if ((!C && D) || ((!C || !D) && Index + 4 != Text.size())) throw std::runtime_error("Base64");
        const unsigned X = C ? Digit(Text[Index + 2]) : 0, Y = D ? Digit(Text[Index + 3]) : 0;
        if ((!C && (B & 15)) || (!D && (X & 3))) throw std::runtime_error("Base64");
        Result += static_cast<char>((A << 2) | (B >> 4));
        if (C) Result += static_cast<char>((B << 4) | (X >> 2));
        if (D) Result += static_cast<char>((X << 6) | Y);
    }
    if (Result.size() > Limit) throw std::runtime_error("Base64Limit");
    return Result;
}
}
