#pragma once
#include "LuiRuntime.h"
#include <chrono>
#include <map>
#include <set>
#include <string>

namespace Lui::Headless {
class Environment {
public:
    Environment(LuiRuntime*,const std::set<std::string>& Keys);
    static bool ValidKey(const std::string&);
private:
    std::map<std::string,std::string> Values;
    std::chrono::steady_clock::time_point Origin=std::chrono::steady_clock::now();
    static int LUI_EXTENSION_CALL Has(void*,const LuiValueV1*,uint32_t,LuiValueV1*);
    static int LUI_EXTENSION_CALL Read(void*,const LuiValueV1*,uint32_t,LuiValueV1*);
    static int LUI_EXTENSION_CALL Platform(void*,const LuiValueV1*,uint32_t,LuiValueV1*);
    static int LUI_EXTENSION_CALL Elapsed(void*,const LuiValueV1*,uint32_t,LuiValueV1*);
};
}
