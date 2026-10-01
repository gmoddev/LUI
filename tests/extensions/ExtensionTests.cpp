#include "LuiRuntime.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <chrono>
#include <thread>

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:ExtensionTest] %s\n", Message);
    return Condition ? 0 : 1;
}

int main(int Count, char** Arguments) {
    if (Count != 6) return Check(false, "expected sample, three rejected, and legacy library paths");
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return Check(false, "runtime creation failed");
    int Failures = 0;

    Failures += Check(Lui_LoadExtension(Runtime, Arguments[1]) == 0 &&
        std::string(Lui_GetLastError(Runtime)).find("not granted") != std::string::npos,
        "native code loaded without a host grant");

    LuiCapabilityDeclarationV1 Declaration{sizeof(LuiCapabilityDeclarationV1),
        LUI_EXTENSION_ABI_VERSION, LUI_CAPABILITY_NATIVE_EXTENSIONS};
    LuiCapabilityDeclarationV1 Invalid = Declaration;
    Invalid.StructSize = 1;
    Failures += Check(Lui_DeclareCapabilities(Runtime, &Invalid) == 0,
        "short capability declaration was accepted");
    Invalid = Declaration;
    Invalid.AbiVersion++;
    Failures += Check(Lui_DeclareCapabilities(Runtime, &Invalid) == 0,
        "incompatible capability declaration was accepted");
    Invalid = Declaration;
    Invalid.GrantedCapabilities |= UINT64_C(16);
    Failures += Check(Lui_DeclareCapabilities(Runtime, &Invalid) == 0,
        "unknown capability bit was accepted");
    Failures += Check(Lui_DeclareCapabilities(Runtime, &Declaration) == 1,
        "valid capability declaration failed");
    Failures += Check(Lui_DeclareCapabilities(Runtime, &Declaration) == 0,
        "capabilities could be redeclared");

    Failures += Check(Lui_LoadExtension(Runtime, "relative-extension.dll") == 0,
        "relative extension path was accepted");
    const std::string Missing = (std::filesystem::path(Arguments[1]).parent_path() /
        "missing-lui-extension-library").string();
    Failures += Check(Lui_LoadExtension(Runtime, Missing.c_str()) == 0,
        "missing extension library was accepted");
    Failures += Check(Lui_LoadExtension(Runtime, Arguments[3]) == 0 &&
        std::string(Lui_GetLastError(Runtime)).find("incompatible") != std::string::npos,
        "incompatible ABI was accepted");
    Failures += Check(Lui_LoadExtension(Runtime, Arguments[4]) == 0 &&
        std::string(Lui_GetLastError(Runtime)).find("required capabilities") != std::string::npos,
        "extension received a capability the host did not grant");
    Failures += Check(Lui_LoadExtension(Runtime, Arguments[2]) == 0 &&
        std::string(Lui_GetLastError(Runtime)).find("synthetic init failure") != std::string::npos,
        "failed initializer was not reported");
    Failures += Check(std::string(Lui_GetExtensionSchemaJson(Runtime)).find("NativeMath") == std::string::npos,
        "failed initializer left a service registered");
    Failures += Check(std::string(Lui_GetExtensionSchemaJson(Runtime)).find("Completed") == std::string::npos,
        "failed initializer left a signal registered");

    Failures += Check(Lui_LoadExtension(Runtime, Arguments[1]) == 1,
        "compatible extension did not load");
    Failures += Check(Lui_LoadExtension(Runtime, Arguments[5]) == 1,
        "original ABI prefix extension did not load");
    const std::string Schema = Lui_GetExtensionSchemaJson(Runtime);
    Failures += Check(Schema.find("NativeMath") != std::string::npos &&
        Schema.find("Add") != std::string::npos && Schema.find("Echo") != std::string::npos &&
        Schema.find("Completed") != std::string::npos,
        "registered service was absent from runtime reflection");
    Failures += Check(Lui_LoadExtension(Runtime, Arguments[1]) == 0,
        "extension loaded twice under the same identity");

    const char* Source =
        "local Platform = app:GetService('PlatformService')\n"
        "assert(Platform:Supports('NativeExtensions'))\n"
        "local Service = app:GetService('NativeMath')\n"
        "assert(Service == app:GetService('NativeMath'))\n"
        "assert(Service:Add(2, 3) == 5)\n"
        "assert(Service:Echo('LUI') == 'LUI')\n"
        "assert(app:GetService('Legacy'):Value() == 123)\n"
        "local Ok, Error = pcall(function() Service:Add('wrong', 3) end)\n"
        "assert(not Ok and string.find(Error, 'Add requires two numbers'))\n"
        "Ok, Error = pcall(function() Service:Add({}, 3) end)\n"
        "assert(not Ok and string.find(Error, 'arguments must be'))\n"
        "Ok, Error = pcall(function() Service:Add(math.huge, 3) end)\n"
        "assert(not Ok and string.find(Error, 'finite numbers'))\n";
    Failures += Check(Lui_RunScript(Runtime, Source, "ExtensionService") == 1,
        Lui_GetLastError(Runtime));
    Failures += Check(Lui_RunScript(Runtime,
        "local Service = app:GetService('NativeMath'); Received = 0; CompletionConnection = Service.Completed:Connect(function(Value) Received = Value end); assert(Service:BeginCompletion()); assert(Service:GetCompletionCount() == 0)",
        "BeginNativeWorker") == 1, "native worker did not begin");
    int Completed = 0;
    for (int Attempt = 0; Attempt < 2000 && !Completed; ++Attempt) {
        Completed = Lui_Pump(Runtime);
        if (!Completed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Failures += Check(Completed == 1, "worker completion was not delivered through the UI pump");
    Failures += Check(Lui_RunScript(Runtime,
        "local Service = app:GetService('NativeMath'); assert(Service:GetCompletionCount() == 1 and Service:WasCompletionOnOwner() and Received == 1); CompletionConnection:Disconnect()",
        "NativeWorkerResult") == 1, "native completion ran off the runtime owner thread");
    std::ifstream ExampleFile(LUI_NATIVE_EXAMPLE_PATH);
    const std::string Example((std::istreambuf_iterator<char>(ExampleFile)), std::istreambuf_iterator<char>());
    Failures += Check(!Example.empty() && Lui_RunScript(Runtime, Example.c_str(), "NativeServiceExample") == 1,
        "native service example did not run headlessly");
    Failures += Check(Lui_LoadExtension(Runtime, Arguments[2]) == 0 &&
        std::string(Lui_GetLastError(Runtime)).find("before application scripts") != std::string::npos,
        "extension loaded after application execution began");
    Failures += Check(Lui_RunScript(Runtime,
        "assert(app:GetService('NativeMath'):Add(4, 5) == 9)", "StillUsable") == 1,
        "rejected extension load corrupted the runtime");
    Lui_Destroy(Runtime);

    LuiRuntime* Denied = Lui_Create();
    Failures += Check(Lui_RunScript(Denied,
        "assert(not app:GetService('PlatformService'):Supports('NativeExtensions'))", "DeniedCapability") == 1,
        "default capability was not denied");
    Lui_Destroy(Denied);
    if (!Failures) std::puts("[LUI:ExtensionTest] Capability and extension ABI checks passed");
    return Failures ? 1 : 0;
}
