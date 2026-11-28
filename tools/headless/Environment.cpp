#include "Environment.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace Lui::Headless {
bool Environment::ValidKey(const std::string& Key) {
    const auto Letter=[](char C){return (C>='A' && C<='Z') || C=='_';};
    return !Key.empty() && Key.size()<=64 && Letter(Key.front()) &&
        std::all_of(Key.begin(),Key.end(),[&](char C){return Letter(C) || (C>='0' && C<='9');});
}
Environment::Environment(LuiRuntime* Runtime,const std::set<std::string>& Keys) {
    for (const auto& Key:Keys) {
#if defined(_WIN32)
        const auto Name=std::wstring(Key.begin(),Key.end());
        SetLastError(ERROR_SUCCESS);
        const auto Size=GetEnvironmentVariableW(Name.c_str(),nullptr,0);
        if (!Size) {if (GetLastError()==ERROR_ENVVAR_NOT_FOUND) continue; if (!GetLastError()) {Values[Key]="";continue;} throw std::runtime_error("ContextUnavailable");}
        if (Size>4097) throw std::runtime_error("ContextUnavailable");
        std::wstring W(Size,0); auto Count=GetEnvironmentVariableW(Name.c_str(),W.data(),Size);
        if (Count>=Size) throw std::runtime_error("ContextUnavailable");
        if (Count==0) {Values[Key]="";continue;}
        const int Bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,W.data(),Count,nullptr,0,nullptr,nullptr);
        if (Bytes<=0 || Bytes>4096) throw std::runtime_error("ContextUnavailable");
        std::string V(Bytes,0); if (WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,W.data(),Count,V.data(),Bytes,nullptr,nullptr)!=Bytes) throw std::runtime_error("ContextUnavailable");
        Values.emplace(Key,std::move(V));
#else
        const auto* V=std::getenv(Key.c_str()); if (!V) continue;
        if (strnlen(V,4097)>4096) throw std::runtime_error("ContextUnavailable");
        Values.emplace(Key,V);
#endif
    }
    if (!Lui_RegisterHostMethod(Runtime,"Context","Has","(Key: string) -> boolean",Has,this) ||
        !Lui_RegisterHostMethod(Runtime,"Context","Read","(Key: string) -> string",Read,this) ||
        !Lui_RegisterHostMethod(Runtime,"Context","Platform","() -> string",Platform,this) ||
        !Lui_RegisterHostMethod(Runtime,"Context","Elapsed","() -> number",Elapsed,this)) throw std::runtime_error("ContextUnavailable");
}
int Environment::Has(void* C,const LuiValueV1* A,uint32_t N,LuiValueV1* R) {
    if (N!=1 || A->Type!=LUI_VALUE_STRING || A->TextLength>64) return 0;
    const auto& Values=static_cast<Environment*>(C)->Values;
    R->Type=LUI_VALUE_BOOLEAN;R->Boolean=Values.count(std::string(A->Text?A->Text:"",static_cast<size_t>(A->TextLength)))!=0;return 1;
}
int Environment::Read(void* C,const LuiValueV1* A,uint32_t N,LuiValueV1* R) {
    if (N!=1 || A->Type!=LUI_VALUE_STRING || A->TextLength>64) return 0;
    const auto& Values=static_cast<Environment*>(C)->Values;
    const auto I=Values.find(std::string(A->Text?A->Text:"",static_cast<size_t>(A->TextLength)));
    if (I==Values.end()) return 0;
    R->Type=LUI_VALUE_STRING;R->Text=I->second.data();R->TextLength=I->second.size();return 1;
}
int Environment::Platform(void*,const LuiValueV1*,uint32_t N,LuiValueV1* R) {
    if (N) return 0;
    R->Type=LUI_VALUE_STRING;
#if defined(_WIN32)
    R->Text="Windows";
#else
    R->Text="Linux";
#endif
    R->TextLength=std::strlen(R->Text);return 1;
}
int Environment::Elapsed(void* C,const LuiValueV1*,uint32_t N,LuiValueV1* R) {
    if (N) return 0;
    R->Type=LUI_VALUE_NUMBER;
    R->Number=std::chrono::duration<double>(std::chrono::steady_clock::now()-static_cast<Environment*>(C)->Origin).count();
    return 1;
}
}
