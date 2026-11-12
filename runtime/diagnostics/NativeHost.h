#pragma once
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>

namespace Lui::Diagnostics {
// Fixed, versioned host records. No application-controlled strings cross this
// transport. A host owns one emitter for its native process generation.
struct NativeHost {
    bool Running = false;
    bool StartupReported = false;
    unsigned Records = 0;
    static const char* GetCode(std::string_view Message) {
        for (const auto* Code : {"UnknownCapability", "DuplicateCapability", "NetworkClientRequired", "NetworkServerRequired",
                "NetworkRawRequired", "HostServicesRequired", "ClipboardUnavailable", "DialogsUnavailable", "ProviderUnavailable",
                "InspectorUnsupported", "CapabilityDenied", "StorageUnavailable", "InvalidManifest", "MissingFile", "CompileFailed",
                "OperationFailed", "StartupFailed"}) {
            for (const auto* System : {"Project", "Capabilities", "Preview", "Diagnostics"}) {
                const std::string Fixed = std::string("[LUI:") + System + "] " + Code;
                if (Message == Fixed || (std::strcmp(System, "Capabilities") == 0 && Message.size() >= Fixed.size() &&
                        Message.substr(Message.size() - Fixed.size()) == Fixed)) return Code;
            }
        }
        return nullptr;
    }
    void Report(std::string_view Message) {
        if (Records >= 256) return;
        ++Records;
        const char* Code = GetCode(Message);
        if (!Code) Code = Running ? "OperationFailed" : "StartupFailed";
        std::fprintf(stderr, "[LUI:Diagnostic] {\"Version\":1,\"Code\":\"%s\",\"Phase\":\"%s\"}\n", Code, Running ? "Runtime" : "Startup");
        std::fflush(stderr);
        if (!Running) StartupReported = true;
    }
};
}
