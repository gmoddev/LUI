#include "LuiRuntime.h"

#include <cstdio>
#include <string>

struct MockPlatform {
    std::string Clipboard;
    uint64_t ReadId = 0;
    uint64_t DialogId = 0;
    LuiRuntime* Runtime = nullptr;
    bool AutoComplete = false;
};

static int LUI_CALL WriteText(void* Context, const char* Text) {
    static_cast<MockPlatform*>(Context)->Clipboard = Text;
    return 1;
}
static int LUI_CALL ReadText(void* Context, uint64_t Id) {
    auto* Mock = static_cast<MockPlatform*>(Context);
    Mock->ReadId = Id;
    if (Mock->AutoComplete && !Lui_CompletePlatformRequest(Mock->Runtime, Id, "next", nullptr))
        return 0;
    return 1;
}
static int LUI_CALL OpenFile(void* Context, uint64_t Id) {
    static_cast<MockPlatform*>(Context)->DialogId = Id;
    return 1;
}
static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:PlatformTest] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    int Failures = 0;
    LuiRuntime* Denied = Lui_Create();
    Failures += Check(Lui_RunScript(Denied,
        "local P = app:GetService('PlatformService')\n"
        "assert(not P:Supports('Clipboard') and not P:Supports('Dialogs'))\n"
        "local Ok = pcall(function() app:GetService('ClipboardService') end)\n"
        "assert(not Ok)", "DeniedPlatform") == 1, Lui_GetLastError(Denied));
    Lui_Destroy(Denied);

    LuiRuntime* Runtime = Lui_Create();
    MockPlatform Mock;
    Mock.Runtime = Runtime;
    LuiPlatformCallbacksV1 Callbacks{sizeof(Callbacks), LUI_EXTENSION_ABI_VERSION,
        &Mock, WriteText, ReadText, OpenFile};
    LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION,
        LUI_CAPABILITY_CLIPBOARD | LUI_CAPABILITY_DIALOGS};
    Failures += Check(Lui_SetPlatformCallbacks(Runtime, &Callbacks) == 1 &&
        Lui_DeclareCapabilities(Runtime, &Grant) == 1, "platform setup failed");
    Failures += Check(Lui_RunScript(Runtime,
        "local P = app:GetService('PlatformService')\n"
        "assert(P:Supports('Clipboard') and P:Supports('Dialogs'))\n"
        "local C = app:GetService('ClipboardService')\n"
        "assert(C:WriteText('hello'))\n"
        "ReadResult = nil\n"
        "C:ReadText(function(Text, Error) assert(Error == nil); ReadResult = Text end)\n"
        "DialogResult = 'pending'\n"
        "app:GetService('DialogService'):OpenFile(function(Path, Error)\n"
        "  assert(Path == nil and Error == nil); DialogResult = 'cancelled'\n"
        "end)", "PlatformStart") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Mock.Clipboard == "hello" && Mock.ReadId && Mock.DialogId,
        "platform callbacks were not invoked");
    Failures += Check(Lui_CompletePlatformRequest(Runtime, Mock.ReadId, "hello", nullptr) == 1 &&
        Lui_CompletePlatformRequest(Runtime, Mock.ReadId, "duplicate", nullptr) == 0 &&
        Lui_CompletePlatformRequest(Runtime, Mock.DialogId, nullptr, nullptr) == 1,
        "platform completion queue rejected valid result or accepted duplicate");
    Failures += Check(Lui_Pump(Runtime) == 2, "platform completions did not drain");
    Failures += Check(Lui_RunScript(Runtime,
        "assert(ReadResult == 'hello' and DialogResult == 'cancelled')", "PlatformResults") == 1,
        Lui_GetLastError(Runtime));
    Failures += Check(Lui_CompletePlatformRequest(Runtime, Mock.ReadId, "late", nullptr) == 0,
        "late platform completion was accepted");
    Mock.AutoComplete = true;
    Failures += Check(Lui_RunScript(Runtime,
        "Requeues = 0\n"
        "local C = app:GetService('ClipboardService')\n"
        "local Callback\n"
        "Callback = function(Text, Error)\n"
        "  assert(Text == 'next' and Error == nil)\n"
        "  Requeues += 1\n"
        "  if Requeues < 130 then C:ReadText(Callback) end\n"
        "end\n"
        "C:ReadText(Callback)", "PlatformRequeue") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_Pump(Runtime) == 64 && Lui_Pump(Runtime) == 64 &&
        Lui_Pump(Runtime) == 2, "recursive platform completions did not honor pump budget");
    Failures += Check(Lui_RunScript(Runtime,
        "assert(Requeues == 130)", "PlatformRequeueResult") == 1, Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    if (!Failures) std::puts("[LUI:PlatformTest] Capability-gated async platform services passed");
    return Failures ? 1 : 0;
}
