#include "StandardProviders.h"
#if defined(LUI_STANDARD_PINS)
#include "LuiStandardPins.h"
#endif
namespace Lui::Headless {
namespace {
const char* Pin(const std::string& Name) {
#if defined(LUI_PIN_Compression)
    if (Name=="Compression") return LUI_PIN_Compression;
#endif
#if defined(LUI_PIN_Crypto)
    if (Name=="Crypto") return LUI_PIN_Crypto;
#endif
#if defined(LUI_PIN_SQLite)
    if (Name=="SQLite") return LUI_PIN_SQLite;
#endif
#if defined(LUI_PIN_Regex)
    if (Name=="Regex") return LUI_PIN_Regex;
#endif
#if defined(LUI_PIN_Toml)
    if (Name=="Toml") return LUI_PIN_Toml;
#endif
#if defined(LUI_PIN_Ffi)
    if (Name=="Ffi") return LUI_PIN_Ffi;
#endif
    return nullptr;
}
}
bool CheckProviders(const nlohmann::json& Descriptor,const std::set<std::string>& Selected) {
#if defined(_WIN32)
    const char* Platform="WindowsX64";
#else
    const char* Platform="LinuxX64";
#endif
    if (Selected.empty() || !Descriptor.contains("NativeBindings") || !Descriptor.contains("ExtensionRegistration")) return false;
    std::set<std::string> Images, Contracts;
    for (const auto& Binding : Descriptor.at("NativeBindings")) {
        const auto Name=Binding.at("Provider").get<std::string>();
        const char* Expected=Pin(Name);
        constexpr unsigned Capabilities=3;
        if (!Expected || !Selected.count(Name) || !Images.insert(Name).second || Binding.at("Platform")!=Platform ||
            Binding.at("Version")!=1 || Binding.at("RequiredCapabilities")!=Capabilities || Binding.at("Sha256")!=Expected) return false;
    }
    for (const auto& Contract : Descriptor.at("ExtensionRegistration").at("Providers")) {
        if (Contract.at("Provider")=="Host") continue;
        const auto Name=Contract.at("Name").get<std::string>();
        const int AbiVersion=(Name=="Compression" || Name=="SQLite")?2:1;
        if (!Images.count(Name) || !Contracts.insert(Name).second || Contract.at("AbiVersion")!=AbiVersion) return false;
    }
    return Images==Selected && Contracts==Selected;
}
}
