#ifndef LUI_APPLICATION_SERVICES_H
#define LUI_APPLICATION_SERVICES_H
#include "LuiNativeServices.h"
#ifdef __cplusplus
extern "C" {
#endif

#define LUI_CAPABILITY_STORAGE UINT64_C(128)
#define LUI_CAPABILITY_NOTIFICATIONS UINT64_C(256)
#define LUI_CAPABILITY_TRAY UINT64_C(512)

/* Trusted owner installation, before scripts or verified native project loading.
   Root is an existing, dedicated, absolute application directory. */
LUI_API uint32_t LUI_CALL Lui_InstallStorageV1(LuiRuntime* Runtime, const char* Root);

/* Private desktop bridge. Callbacks run only on the owner, from a later pump.
   Payloads are validated UTF-8. Return one fixed LUI_NATIVE_* status; no native
   diagnostic text crosses this boundary. Context lives through Lui_Destroy. */
typedef struct LuiDesktopServicesV1 {
    uint32_t StructSize, AbiVersion;
    void* Context;
    uint64_t SupportedCapabilities;
    uint32_t (LUI_CALL* Notify)(void* Context, const char* Title, const char* Body);
    uint32_t (LUI_CALL* CreateTray)(void* Context, LuiNativeIdV2 Icon, const char* Tooltip);
    void (LUI_CALL* RemoveTray)(void* Context, LuiNativeIdV2 Icon);
} LuiDesktopServicesV1;
LUI_API uint32_t LUI_CALL Lui_InstallDesktopServicesV1(LuiRuntime* Runtime, const LuiDesktopServicesV1* Services);
/* Supplied Windows shell adapter; Unsupported on other platforms. No native
   window is shown or focused. The host must pump its normal OS message loop. */
LUI_API uint32_t LUI_CALL Lui_InstallWindowsDesktopV1(LuiRuntime* Runtime);
/* Owner-only activation admission; never invokes Luau inline. Closed/foreign
   icons are rejected. Full progress queues return WouldBlock immediately. */
LUI_API uint32_t LUI_CALL Lui_TrayActivatedV1(LuiRuntime* Runtime, LuiNativeIdV2 Icon);

#ifdef __cplusplus
}
#endif
#endif
