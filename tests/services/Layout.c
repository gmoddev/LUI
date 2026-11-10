#include "LuiNativeServices.h"
#include "LuiApplicationServices.h"
#include <stddef.h>

/* Initial ABI profile is x64; fail rather than silently select another layout. */
_Static_assert(sizeof(void*) == 8, "native v2 requires a qualified x64 ABI");
_Static_assert(sizeof(LuiNativeIdV2) == 16, "token layout");
_Static_assert(sizeof(LuiNativeMethodV2) == 40, "method layout");
_Static_assert(offsetof(LuiNativeMethodV2, MaxInputBytes) == 16, "method offset");
_Static_assert(sizeof(LuiNativeServiceV2) == 48, "service layout");
_Static_assert(offsetof(LuiNativeServiceV2, RequiredCapabilities) == 32, "capability offset");
_Static_assert(sizeof(LuiNativeProviderV2) == 56, "provider layout");
_Static_assert(sizeof(LuiNativeHostV2) == 104, "host layout");
_Static_assert(offsetof(LuiNativeHostV2, Complete) == 88, "completion offset");
_Static_assert(sizeof(LuiNativeDeliveryV2) == 72, "delivery layout");
_Static_assert(offsetof(LuiNativeDeliveryV2, Request) == 24, "delivery offset");
_Static_assert(sizeof(LuiNativeStatsV2) == 72, "stats layout");
_Static_assert(sizeof(LuiDesktopServicesV1) == 48, "desktop bridge layout");
_Static_assert(sizeof(LuiFrameViewV1) == 56, "private frame view layout");
_Static_assert(sizeof(LuiFrameCallbacksV1) == 48, "private frame callback layout");
_Static_assert(offsetof(LuiFrameCallbacksV1, Publish) == 24, "private owner publication offset");
_Static_assert(sizeof(LuiFrameStatsV1) == 104, "private numeric frame stats layout");
int LuiNativeLayoutProof(void) { return 1; }
