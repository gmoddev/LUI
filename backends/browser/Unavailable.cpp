#include "../../runtime/internal/Dispatch.h"
#include "../../runtime/internal/Network.h"
#include "../../runtime/internal/NativeServices.h"
#include "../../runtime/internal/Extensions.h"
#include "../../runtime/internal/ApplicationServices.h"
#include "../../runtime/services/Domain.h"
#include "lua.h"
#include "lualib.h"

namespace {
int Unsupported(lua_State* State) { luaL_error(State, "[LUI:Browser] Unsupported"); return 0; }
}
void RegisterNetworkTypes(lua_State*) {}
void RegisterNativeTypes(lua_State*) {}
void PushNetworkService(lua_State* State) { Unsupported(State); }
void PushHttpService(lua_State* State) { Unsupported(State); }
void PushHttpServerService(lua_State* State) { Unsupported(State); }
// Ordinary task.spawn/defer/delay coroutines have no hosted-network owner.
bool NetworkTaskReady(LuiRuntime*, int) { return true; }
bool CompleteNetworkTask(LuiRuntime*, int, lua_State*, int) { return false; }
int DrainNetworkCompletions(LuiRuntime*) { return 0; }
void CloseNetwork(LuiRuntime*) {}
int ConfigureNetworkTls(LuiRuntime*, const LuiTlsOptionsV1*) { return 0; }
bool HasExtensionService(const LuiRuntime*, const std::string&) { return false; }
int PushExtensionService(lua_State* State, LuiRuntime*, const std::string&) { return Unsupported(State); }
void CloseExtensions(LuiRuntime*) {}
void CloseExtensionListeners(LuiRuntime*) {}
int DrainUiCompletions(LuiRuntime*) { return 0; }
bool HasNativeService(const LuiRuntime*, const std::string&) { return false; }
bool NativeServiceNeedsHostGrant(const LuiRuntime*, const std::string&) { return false; }
int PushNativeService(lua_State* State, LuiRuntime*, const std::string&) { return Unsupported(State); }
int DrainNativeServices(LuiRuntime*, size_t, std::chrono::steady_clock::time_point) { return 0; }
void CloseNativeServices(LuiRuntime*) {}
void ObserveNativeServices(const LuiRuntime*, Lui::Project::DeclarationSet&, bool) {}
void PumpDesktopServices(LuiRuntime*) {}
void RetireDesktopServices(LuiRuntime*) {}
namespace Lui::Native {
std::shared_ptr<Domain> GetDomain() { return {}; }
void Pump(const std::shared_ptr<Domain>&) noexcept {}
}
extern "C" LUI_API int LUI_CALL Lui_DeclareCapabilities(LuiRuntime* Runtime, const LuiCapabilityDeclarationV1* Input) {
    if (!CheckOwner(Runtime) || !Input || Input->StructSize != sizeof(*Input) ||
        Input->AbiVersion != 1 || Input->GrantedCapabilities || Runtime->ApplicationStarted || Runtime->CapabilitiesDeclared) return 0;
    Runtime->CapabilitiesDeclared = true;
    return 1;
}
extern "C" LUI_API const char* LUI_CALL Lui_GetExtensionSchemaJson(LuiRuntime*) { return Lui_GetSchemaJson(); }
