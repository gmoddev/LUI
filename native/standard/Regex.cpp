#include "TextProvider.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <cmath>
#include <map>

namespace Lui::Standard {
const char* const Name="Regex";
namespace {
using Code=std::unique_ptr<pcre2_code,decltype(&pcre2_code_free)>;
struct State:TextState { std::map<unsigned,Code> Patterns; unsigned Next=0; };
unsigned Number(const LuiValueV1& V,unsigned Min,unsigned Max) {
    if (V.Type!=LUI_VALUE_NUMBER || !(V.Number>=Min && V.Number<=Max) || std::floor(V.Number)!=V.Number)
        throw std::invalid_argument("InvalidNumber");
    return static_cast<unsigned>(V.Number);
}
int LUI_EXTENSION_CALL Compile(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Out) {
    if (Count!=1) return 0;
    try {
        auto& S=*static_cast<State*>(Context);
        const auto Pattern=Text(Args[0],512);
        if (S.Patterns.size()>=32 || S.Next==0xffffffff || Pattern.find("(*")!=Pattern.npos || Pattern.find("(?C")!=Pattern.npos) return 0;
        std::unique_ptr<pcre2_compile_context,decltype(&pcre2_compile_context_free)> C(pcre2_compile_context_create(nullptr),pcre2_compile_context_free);
        if (!C) return 0;
        pcre2_set_parens_nest_limit(C.get(),32); pcre2_set_max_pattern_compiled_length(C.get(),32768);
        PCRE2_SIZE Offset=0; int Error=0;
        Code Compiled(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(Pattern.data()),Pattern.size(),
            PCRE2_AUTO_CALLOUT|PCRE2_NEVER_UTF|PCRE2_NEVER_UCP,&Error,&Offset,C.get()),pcre2_code_free);
        uint32_t Groups=0;
        if (!Compiled || pcre2_pattern_info(Compiled.get(),PCRE2_INFO_CAPTURECOUNT,&Groups) || Groups>8) return 0;
        S.Patterns.emplace(++S.Next,std::move(Compiled)); Out->Type=LUI_VALUE_NUMBER; Out->Number=S.Next; return 1;
    } catch (...) { return 0; }
}
int LUI_EXTENSION_CALL Release(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Out) {
    if (Count!=1) return 0;
    try {
        auto& S=*static_cast<State*>(Context);
        Out->Type=LUI_VALUE_BOOLEAN; Out->Boolean=S.Patterns.erase(Number(Args[0],1,0xffffffff))!=0; return 1;
    } catch (...) { return 0; }
}
int Step(pcre2_callout_block*,void* Data) {
    auto& Left=*static_cast<unsigned*>(Data);
    if (!Left) return PCRE2_ERROR_CALLOUT;
    --Left; return 0;
}
int LUI_EXTENSION_CALL Search(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Out) {
    if (Count!=4) return 0;
    try {
        auto& S=*static_cast<State*>(Context);
        auto& Pattern=S.Patterns.at(Number(Args[0],1,0xffffffff));
        const auto Input=Text(Args[1],32768);
        size_t Start=Number(Args[2],1,static_cast<unsigned>(Input.size()+1))-1;
        if (Args[3].Type!=LUI_VALUE_BOOLEAN) return 0;
        const bool All=Args[3].Boolean!=0;
        std::unique_ptr<pcre2_match_data,decltype(&pcre2_match_data_free)> Data(pcre2_match_data_create_from_pattern(Pattern.get(),nullptr),pcre2_match_data_free);
        std::unique_ptr<pcre2_match_context,decltype(&pcre2_match_context_free)> C(pcre2_match_context_create(nullptr),pcre2_match_context_free);
        if (!Data || !C) return 0;
        unsigned Budget=100000;
        pcre2_set_callout(C.get(),Step,&Budget); pcre2_set_match_limit(C.get(),50000);
        pcre2_set_depth_limit(C.get(),128); pcre2_set_heap_limit(C.get(),256);
        const auto Groups=pcre2_get_ovector_count(Data.get());
        S.Result.assign(1,static_cast<char>(Groups));
        unsigned Matches=0; uint32_t Options=0;
        while (Start<=Input.size()) {
            int Status=pcre2_match(Pattern.get(),reinterpret_cast<PCRE2_SPTR>(Input.data()),Input.size(),Start,Options,Data.get(),C.get());
            if (Status==PCRE2_ERROR_NOMATCH) break;
            if (Status<=0 || ++Matches>256) return 0;
            PCRE2_SIZE* Pairs=pcre2_get_ovector_pointer(Data.get());
            if (Pairs[0]<Start || Pairs[1]<Pairs[0] || Pairs[1]>Input.size()) return 0;
            for (unsigned I=0;I<Groups*2;++I) {
                const auto Value=(I>=static_cast<unsigned>(Status)*2 || Pairs[I]==PCRE2_UNSET)?65535u:static_cast<unsigned>(Pairs[I]);
                S.Result.push_back(static_cast<char>(Value)); S.Result.push_back(static_cast<char>(Value>>8));
            }
            if (!All) break;
            if (!pcre2_next_match(Data.get(),&Start,&Options)) break;
        }
        return S.Return(Out);
    } catch (...) { return 0; }
}
}
std::unique_ptr<TextState> Register(const LuiHostApiV1& Host) {
    auto S=std::make_unique<State>();
    if (!Host.RegisterMethod(Host.HostContext,Name,"Compile","(Pattern: string) -> number",Compile,S.get()) ||
        !Host.RegisterMethod(Host.HostContext,Name,"Search","(Id: number, Text: string, Start: number, All: boolean) -> string",Search,S.get()) ||
        !Host.RegisterMethod(Host.HostContext,Name,"Release","(Id: number) -> boolean",Release,S.get())) return nullptr;
    return S;
}
}
