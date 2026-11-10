#ifndef LUI_OWNER_WAKE_H
#define LUI_OWNER_WAKE_H
#include "LuiNativeServices.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Optional trusted embedding seam, independent of native service ABI v1/v2.
   Exactly one registration per existing owner domain. Request may run on a
   provider worker: only enqueue an owner turn, never enter a VM or toolkit.
   Return 1 if a wake is queued/coalesced, 0 on failure (polling remains valid).
   Successful registration transfers Context ownership. Release runs exactly
   once, possibly off-owner, after all in-flight Request calls return. It must
   only release context/code roots; it must not enter a VM or toolkit or wait.
   Unregistration never waits for a worker. Already queued host actions must
   check their own stopped registration before using any runtime/domain pointer. */
typedef struct LuiNativeWakeV1 {
    uint32_t StructSize, AbiVersion;
    void* Context;
    uint32_t (LUI_CALL* Request)(void* Context);
    void (LUI_CALL* Release)(void* Context);
} LuiNativeWakeV1;

/* Owner only. Returns an owned wrapper around the runtime's EXISTING domain;
   release using Lui_ReleaseNativeDomainV2. Does not create a new quota domain. */
LUI_API LuiNativeDomainV2* LUI_CALL Lui_GetOwnerDomainV1(LuiRuntime* Runtime);
/* Owner only. Null Callbacks unregisters, idempotently. Failure leaves the old
   registration unchanged and does not take Context ownership. Registration is
   a monotonically unique token, not a pointer. No stable ABI table is appended. */
LUI_API uint32_t LUI_CALL Lui_SetNativeWakeV1(LuiNativeDomainV2* Domain,
    const LuiNativeWakeV1* Callbacks, uint64_t* Registration);
/* Owner only, immediately before an authoritative owner pump. A stale token
   returns Closed and cannot acknowledge a replacement registration. Polling
   hosts need not install this seam; deadlines/retirement still require polling. */
LUI_API uint32_t LUI_CALL Lui_AcknowledgeNativeWakeV1(LuiNativeDomainV2* Domain, uint64_t Registration);
/* Nonmutating owner query: 1 ready; 0 invalid/wrong owner or VM/backend/delivery
   busy; -1 owner-observed terminal/failed runtime. Hosts defer nested wakes to
   fallback and skip terminal runtimes rather than starving healthy siblings. */
LUI_API int LUI_CALL Lui_IsOwnerPumpReadyV1(LuiRuntime* Runtime);
#ifdef __cplusplus
}
#endif
#endif
