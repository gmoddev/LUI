#ifndef LUI_EXECUTION_H
#define LUI_EXECUTION_H
#include "LuiRuntime.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Optional host execution query; independent of extension ABI v1/v2. Owner
   only, nonmutating, call between owner turns. Returns 1 while scheduled tasks
   (including future deadlines), async operations, queued completions, or open
   network resources keep the application alive; 0 when idle; -1 for invalid,
   wrong-owner or terminal runtimes. An active dispatch returns 1. An idle VM
   can still have module exports and signal subscriptions; this is not an
   arbitrary module-unload or native-provider-quiescence guarantee. */
LUI_API int LUI_CALL Lui_HasPendingWorkV1(LuiRuntime* Runtime);

#ifdef __cplusplus
}
#endif
#endif
