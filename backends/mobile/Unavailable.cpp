#include "../../runtime/internal/Dispatch.h"
#include "../../runtime/internal/Network.h"
#include "../../runtime/internal/NativeServices.h"
#include "../../runtime/internal/Extensions.h"
#include "../../runtime/internal/ApplicationServices.h"
#include "../../runtime/services/Domain.h"
#include "lua.h"
#include "lualib.h"

// Only the built-in app-private storage provider may receive a grant.
namespace { int Unsupported(lua_State* State) { luaL_error(State, "[LUI:Mobile] Unsupported"); return 0; } }
void RegisterNetworkTypes(lua_State*) {}
void PushNetworkService(lua_State* State) { Unsupported(State); }
void PushHttpService(lua_State* State) { Unsupported(State); }
void PushHttpServerService(lua_State* State) { Unsupported(State); }
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
void PumpDesktopServices(LuiRuntime*) {}
void RetireDesktopServices(LuiRuntime*) {}
extern "C" LUI_API int LUI_CALL Lui_DeclareCapabilities(LuiRuntime* Runtime, const LuiCapabilityDeclarationV1* Input) {
    if (!CheckOwner(Runtime) || !Input || Input->StructSize != sizeof(*Input) || Input->AbiVersion != 1 ||
        (Input->GrantedCapabilities & ~LUI_CAPABILITY_STORAGE) || Runtime->ApplicationStarted || Runtime->CapabilitiesDeclared) return 0;
    Runtime->GrantedCapabilities = Input->GrantedCapabilities;
    Runtime->CapabilitiesDeclared = true; return 1;
}
extern "C" LUI_API const char* LUI_CALL Lui_GetExtensionSchemaJson(LuiRuntime*) { return Lui_GetSchemaJson(); }
