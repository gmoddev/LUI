#include "TextProvider.h"
#include <toml.hpp>
#include <cmath>
#include <ostream>
#include <streambuf>

namespace Lui::Standard {
const char* const Name="Toml";
namespace {
class LimitedText:public std::streambuf {
public:
    explicit LimitedText(size_t Limit):Limit(Limit) {}
    std::string Value;
private:
    size_t Limit;
    std::streamsize xsputn(const char* Bytes,std::streamsize Size) override {
        if (Size<0 || static_cast<size_t>(Size)>Limit-Value.size()) throw std::length_error("OutputLimit");
        Value.append(Bytes,static_cast<size_t>(Size)); return Size;
    }
    int_type overflow(int_type Byte) override {
        if (traits_type::eq_int_type(Byte,traits_type::eof())) return traits_type::not_eof(Byte);
        const auto Ch=traits_type::to_char_type(Byte); xsputn(&Ch,1); return Byte;
    }
};
template<class Formatter>
int Render(void* Context,Formatter Format,size_t Limit,LuiValueV1* Out) {
    LimitedText Output(Limit); std::ostream Stream(&Output);
    Stream.exceptions(std::ios::failbit|std::ios::badbit); Stream << Format;
    auto& State=*static_cast<TextState*>(Context); State.Result=std::move(Output.Value); return State.Return(Out);
}
void Validate(const toml::node& Node,unsigned Depth,unsigned& Left) {
    if (!Left || Depth>16) throw std::length_error("DocumentLimit");
    --Left;
    if (const auto* Table=Node.as_table()) {
        for (const auto& Entry:*Table) Validate(Entry.second,Depth+1,Left);
    } else if (const auto* Array=Node.as_array()) {
        for (const auto& Item:*Array) Validate(Item,Depth+1,Left);
    } else if (const auto* Integer=Node.as_integer()) {
        if (Integer->get() < -9007199254740991LL || Integer->get()>9007199254740991LL) throw std::range_error("IntegerRange");
    } else if (const auto* Float=Node.as_floating_point()) {
        if (!std::isfinite(Float->get())) throw std::range_error("Nonfinite");
    } else if (!Node.is_boolean() && !Node.is_string()) throw std::invalid_argument("UnsupportedType");
}
int LUI_EXTENSION_CALL Decode(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Out) {
    if (Count!=1) return 0;
    try {
        auto Tree=toml::parse(Text(Args[0],32768));
        unsigned Left=2048; Validate(Tree,1,Left);
        return Render(Context,toml::json_formatter{Tree},65536,Out);
    } catch (...) { return 0; }
}
int LUI_EXTENSION_CALL Format(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Out) {
    if (Count!=1) return 0;
    try {
        auto Tree=toml::parse(Text(Args[0],32768));
        if (Tree.size()!=1 || !Tree["root"].is_table()) return 0;
        auto& Root=*Tree["root"].as_table();
        Root.is_inline(false);
        unsigned Left=2048; Validate(Root,1,Left);
        return Render(Context,toml::toml_formatter{Root},32768,Out);
    } catch (...) { return 0; }
}
}
std::unique_ptr<TextState> Register(const LuiHostApiV1& Host) {
    auto S=std::make_unique<TextState>();
    if (!Host.RegisterMethod(Host.HostContext,Name,"Decode","(Document: string) -> string",Decode,S.get()) ||
        !Host.RegisterMethod(Host.HostContext,Name,"Format","(Document: string) -> string",Format,S.get())) return nullptr;
    return S;
}
}
