#ifndef LUI_NATIVE_SERVICES_H
#define LUI_NATIVE_SERVICES_H

#include "LuiRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_NATIVE_ABI_V2 2u
#define LUI_NATIVE_OK 0u
#define LUI_NATIVE_INVALID_ARGUMENT 1u
#define LUI_NATIVE_CAPABILITY_DENIED 2u
#define LUI_NATIVE_UNSUPPORTED 3u
#define LUI_NATIVE_LIMIT_EXCEEDED 4u
#define LUI_NATIVE_WOULD_BLOCK 5u
#define LUI_NATIVE_CANCELED 6u
#define LUI_NATIVE_DEADLINE_EXCEEDED 7u
#define LUI_NATIVE_CLOSED 8u
#define LUI_NATIVE_PROVIDER_FAILURE 9u
#define LUI_NATIVE_ALREADY_SETTLED 10u
#define LUI_NATIVE_TERMINAL 1u
#define LUI_NATIVE_PROGRESS 2u

typedef struct LuiNativeDomainV2 LuiNativeDomainV2;
typedef struct LuiNativeSessionV2 LuiNativeSessionV2;
/* Tokens are values, not addresses. Zero denotes no value. Never reuse an ID. */
typedef struct LuiNativeIdV2 { uint64_t Session, Value; } LuiNativeIdV2;

typedef struct LuiNativeMethodV2 {
    uint32_t StructSize, AbiVersion;
    const char* Name;
    uint32_t MaxInputBytes, MaxResultBytes;
    uint32_t InputKind, ResultKind;
    uint32_t DeadlineMilliseconds, Reserved;
} LuiNativeMethodV2;

typedef struct LuiNativeServiceV2 {
    uint32_t StructSize, AbiVersion;
    const char* Name;
    const LuiNativeMethodV2* Methods;
    uint32_t MethodCount, ResourceKindCount;
    uint64_t RequiredCapabilities;
    uint32_t Version, Reserved;
} LuiNativeServiceV2;

typedef struct LuiNativeProviderV2 {
    uint32_t StructSize, AbiVersion;
    void (LUI_CALL* Start)(void* Context, LuiNativeIdV2 Request, uint32_t Method,
        LuiNativeIdV2 Input, LuiNativeIdV2 Handle);
    void (LUI_CALL* Cancel)(void* Context, LuiNativeIdV2 Request);
    void (LUI_CALL* CloseHandle)(void* Context, LuiNativeIdV2 Handle, uint32_t Kind);
    void (LUI_CALL* BeginShutdown)(void* Context);
    /* Owner only. Promptly test/join already stopped workers. True is a promise
       that no provider code can run again outside subsequent owner callbacks. */
    uint32_t (LUI_CALL* PollShutdown)(void* Context);
    /* Final owner cleanup after quiescence and all owned references drain. */
    void (LUI_CALL* Destroy)(void* Context);
} LuiNativeProviderV2;

typedef struct LuiNativeHostV2 {
    uint32_t StructSize, AbiVersion;
    LuiNativeSessionV2* Session;
    uint32_t (LUI_CALL* RetainSession)(LuiNativeSessionV2* Session);
    uint32_t (LUI_CALL* ReleaseSession)(LuiNativeSessionV2* Session);
    /* Request zero uses ordinary quota. A request token consumes its reserved
       result allowance; it never bypasses session or domain limits. */
    uint32_t (LUI_CALL* CopyBytes)(LuiNativeSessionV2* Session, LuiNativeIdV2 Request,
        const void* Bytes, uint64_t Length, LuiNativeIdV2* Result);
    uint32_t (LUI_CALL* RetainBytes)(LuiNativeSessionV2* Session, LuiNativeIdV2 Buffer);
    uint32_t (LUI_CALL* ReleaseBytes)(LuiNativeSessionV2* Session, LuiNativeIdV2 Buffer);
    uint32_t (LUI_CALL* ViewBytes)(LuiNativeSessionV2* Session, LuiNativeIdV2 Buffer,
        const void** Bytes, uint64_t* Length);
    uint32_t (LUI_CALL* CreateHandle)(LuiNativeSessionV2* Session, uint32_t Kind, LuiNativeIdV2* Result);
    uint32_t (LUI_CALL* RetainHandle)(LuiNativeSessionV2* Session, LuiNativeIdV2 Handle);
    uint32_t (LUI_CALL* ReleaseHandle)(LuiNativeSessionV2* Session, LuiNativeIdV2 Handle);
    uint32_t (LUI_CALL* Complete)(LuiNativeSessionV2* Session, LuiNativeIdV2 Request,
        uint32_t Status, LuiNativeIdV2 Buffer, LuiNativeIdV2 Handle);
    uint32_t (LUI_CALL* Emit)(LuiNativeSessionV2* Session, LuiNativeIdV2 Request, LuiNativeIdV2 Buffer);
} LuiNativeHostV2;

typedef struct LuiNativeDeliveryV2 {
    uint32_t StructSize, AbiVersion, Kind, Status;
    uint64_t Sequence;
    LuiNativeIdV2 Request, Buffer, Handle;
} LuiNativeDeliveryV2;

typedef struct LuiNativeStatsV2 {
    uint32_t StructSize, AbiVersion;
    uint64_t Bytes, ReservedBytes, Requests, Handles, Sessions, RetiringSessions, Leases, Events;
} LuiNativeStatsV2;

/* Trusted embedding API. Domain owner is the creating thread. ReleaseDomain
   does not force unload; pump retirement until empty before final release. */
LUI_API LuiNativeDomainV2* LUI_CALL Lui_CreateNativeDomainV2(void);
LUI_API void LUI_CALL Lui_ReleaseNativeDomainV2(LuiNativeDomainV2* Domain);
LUI_API uint32_t LUI_CALL Lui_PumpNativeDomainV2(LuiNativeDomainV2* Domain);
LUI_API uint32_t LUI_CALL Lui_GetNativeStatsV2(LuiNativeDomainV2* Domain, LuiNativeStatsV2* Stats);
LUI_API uint32_t LUI_CALL Lui_OpenNativeSessionV2(LuiNativeDomainV2* Domain,
    const LuiNativeServiceV2* Service, const LuiNativeProviderV2* Provider, void* Context, LuiNativeHostV2* Host);
LUI_API uint32_t LUI_CALL Lui_RetireNativeSessionV2(LuiNativeSessionV2* Session);
LUI_API uint32_t LUI_CALL Lui_StartNativeRequestV2(LuiNativeSessionV2* Session, uint32_t Method,
    LuiNativeIdV2 Input, LuiNativeIdV2 Handle, LuiNativeIdV2* Request);
LUI_API uint32_t LUI_CALL Lui_CancelNativeRequestV2(LuiNativeSessionV2* Session, LuiNativeIdV2 Request);
LUI_API uint32_t LUI_CALL Lui_CloseNativeHandleV2(LuiNativeSessionV2* Session, LuiNativeIdV2 Handle);
/* Owner only. Transfers one host-owned reference for each nonzero result token
   to the consumer, which must ReleaseBytes/ReleaseHandle after use. */
LUI_API uint32_t LUI_CALL Lui_NextNativeDeliveryV2(LuiNativeSessionV2* Session, LuiNativeDeliveryV2* Delivery);

/* Runtime attachment/loader negotiation is separate from primitive ABI v1.
   A registered runtime binding owns a session lease until VM teardown. Owner
   retirement may precede teardown; subsequent calls fail Closed safely. */
LUI_API uint32_t LUI_CALL Lui_SetNativeDomainV2(LuiRuntime* Runtime, LuiNativeDomainV2* Domain);
LUI_API uint32_t LUI_CALL Lui_RegisterNativeServiceV2(LuiRuntime* Runtime,
    const LuiNativeServiceV2* Service, const LuiNativeProviderV2* Provider, void* Context, LuiNativeHostV2* Host);
LUI_API uint32_t LUI_CALL Lui_LoadNativeServiceV2(LuiRuntime* Runtime, const char* AbsolutePath);

typedef uint32_t (LUI_CALL* LuiNativeQueryV2)(LuiNativeServiceV2* Service, LuiNativeProviderV2* Provider);
typedef uint32_t (LUI_CALL* LuiNativeInitV2)(const LuiNativeHostV2* Host, void** Context);
/* A dynamic provider exports LuiNativeQuery and LuiNativeInit, distinct from v1.
   Query is side-effect free; all tables/strings remain valid until unload.
   Init must clean partial failure through BeginShutdown/PollShutdown as usual. */

#ifdef __cplusplus
}
#endif
#endif
