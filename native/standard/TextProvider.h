#pragma once
#include "LuiExtension.h"
#include <memory>
#include <string>
#include <string_view>
#include <stdexcept>

namespace Lui::Standard {
struct TextState {
    std::string Result;
    virtual ~TextState()=default;
    int Return(LuiValueV1* Out) const {
        Out->Type=LUI_VALUE_STRING; Out->Text=Result.data(); Out->TextLength=Result.size(); return 1;
    }
};
inline std::string_view Text(const LuiValueV1& Value,size_t Limit) {
    if (Value.Type!=LUI_VALUE_STRING || Value.TextLength>Limit || (!Value.Text && Value.TextLength))
        throw std::invalid_argument("InvalidInput");
    return {Value.Text?Value.Text:"",static_cast<size_t>(Value.TextLength)};
}
extern const char* const Name;
std::unique_ptr<TextState> Register(const LuiHostApiV1& Host);
}
