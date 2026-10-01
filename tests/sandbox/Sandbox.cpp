#include "Application.hpp"

#include <cstdio>
#include <string>

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:SandboxTest] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    int Failures = 0;
    {
        lui::Application App;
        App.Service("Secret").Function("Value", [] { return 42; });
        LuiSandboxLimitsV1 Limits{sizeof(Limits), LUI_EXTENSION_ABI_VERSION, 1024 * 1024, 1000};
        Failures += Check(Lui_ConfigureSandbox(App.NativeRuntime(), &Limits) == 1,
            "valid sandbox configuration failed");
        Failures += Check(Lui_ConfigureSandbox(App.NativeRuntime(), &Limits) == 0,
            "sandbox configured twice");
        try {
            App.RunSource("assert(not app:GetService('PlatformService'):Supports('HostServices'))\n"
                "local Ok, Error = pcall(function() app:GetService('Secret') end)\n"
                "assert(not Ok and string.find(Error, 'not granted'))", "SandboxDenied");
        } catch (const std::exception& Error) {
            Failures += Check(false, Error.what());
        }
        Failures += Check(Lui_RunScript(App.NativeRuntime(), "while true do end", "InfiniteLoop") == 0 &&
            std::string(Lui_GetLastError(App.NativeRuntime())).find("execution limit") != std::string::npos,
            "infinite loop exceeded execution limit without stopping");
        Failures += Check(Lui_RunScript(App.NativeRuntime(),
            "local X = string.rep('x', 2 * 1024 * 1024)", "MemoryLimit") == 0,
            "VM allocation exceeded memory limit");
        Failures += Check(Lui_RunScript(App.NativeRuntime(), "assert(2 + 2 == 4)", "AfterLimits") == 1,
            "runtime unusable after sandbox rejection");
        Failures += Check(Lui_RunScript(App.NativeRuntime(),
            "local B = Instance.new('TextButton')\n"
            "B.Activated:Connect(function() while true do end end)\n"
            "Button = B", "EventLimitSetup") == 1, "event limit setup failed");
        Failures += Check(Lui_Activate(App.NativeRuntime(), 1) == 1 &&
            std::string(Lui_GetLastError(App.NativeRuntime())).find("execution limit") != std::string::npos,
            "signal callback bypassed execution limit");
    }
    {
        lui::Application App;
        App.Service("Allowed").Function("Value", [] { return 7; });
        LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION,
            LUI_CAPABILITY_HOST_SERVICES};
        LuiSandboxLimitsV1 Limits{sizeof(Limits), LUI_EXTENSION_ABI_VERSION, 2 * 1024 * 1024, 1000};
        Failures += Check(Lui_DeclareCapabilities(App.NativeRuntime(), &Grant) == 1 &&
            Lui_ConfigureSandbox(App.NativeRuntime(), &Limits) == 1,
            "sandbox host service grant failed");
        Failures += Check(Lui_RunScript(App.NativeRuntime(),
            "assert(app:GetService('PlatformService'):Supports('HostServices'))\n"
            "assert(app:GetService('Allowed'):Value() == 7)", "AllowedService") == 1,
            Lui_GetLastError(App.NativeRuntime()));
    }
    if (!Failures) std::puts("[LUI:SandboxTest] VM quotas and host grants passed");
    return Failures ? 1 : 0;
}
