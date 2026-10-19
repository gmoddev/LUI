#pragma once

#include "LuiExtension.h"

#if defined(_WIN32)
#  if defined(LUI_BUILD)
#    define LUI_API __declspec(dllexport)
#  else
#    define LUI_API __declspec(dllimport)
#  endif
#  define LUI_CALL __cdecl
#else
#  define LUI_API
#  define LUI_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LuiRuntime LuiRuntime;
typedef void (LUI_CALL* LuiLogCallback)(void* Context, const char* Level, const char* Message);

/* Host-only declaration. The script cannot grant capabilities to itself. */
typedef struct LuiCapabilityDeclarationV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    uint64_t GrantedCapabilities;
} LuiCapabilityDeclarationV1;

/* Restricts the Luau VM before any extension or script. Limits apply to each
   top-level script, signal callback, and scheduled callback dispatch. */
typedef struct LuiSandboxLimitsV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    uint64_t MaxMemoryBytes;
    uint64_t MaxInterrupts;
} LuiSandboxLimitsV1;

/* Host-only narrowing of network grants. Zero port bounds mean unrestricted;
   otherwise both bounds must be set and inclusive. Configure before scripts. */
#define LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY 1u
#define LUI_NETWORK_POLICY_SERVER_LOOPBACK_ONLY 2u
typedef struct LuiNetworkPolicyV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    uint32_t Flags;
    uint32_t ClientPortMin;
    uint32_t ClientPortMax;
    uint32_t ServerPortMin;
    uint32_t ServerPortMax;
} LuiNetworkPolicyV1;

/* Host-only TLS configuration, once before scripts/network use. Inputs are
   copied/parsed synchronously. Empty trust uses OS roots; supplied PEM CA roots
   replace them. Server credentials are a PKCS#12 certificate/key bundle.
   Sizes are capped at 256 KiB; PasswordBytes at 1024. No validation bypass. */
typedef struct LuiTlsOptionsV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    const char* TrustAnchorsPem;
    uint32_t TrustAnchorsBytes;
    const void* ServerPkcs12;
    uint32_t ServerPkcs12Bytes;
    const char* Password;
    uint32_t PasswordBytes;
} LuiTlsOptionsV1;

/* Internal host contract. This is not the stable extension ABI. */
typedef struct LuiBackendCallbacks {
    void* Context;
    /* Return 1 on success, 0 on failure. Failures may supply detail through Lui_ReportBackendError. */
    int (LUI_CALL* Create)(void* Context, int Id, const char* ClassName);
    int (LUI_CALL* Property)(void* Context, int Id, const char* Name, const char* Value);
    int (LUI_CALL* Parent)(void* Context, int Id, int ParentId);
    /* ResizeWindow is set for a script-requested window size, never for a native viewport update. */
    int (LUI_CALL* Arrange)(void* Context, int Id, double X, double Y, double Width, double Height, int ResizeWindow);
    int (LUI_CALL* Destroy)(void* Context, int Id);
} LuiBackendCallbacks;

/* Private presentation bridge, separate from the extension ABI and old callback
   table. Register once on the owner thread before objects/scripts. Clip is local
   to this node; HasClip bit 0 means a clip rectangle is present, bit 1 means
   this node clips descendants at its own bounds. Rank is the canonical sibling
   paint order (zero based). */
typedef struct LuiPresentationCallbacksV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    void* Context;
    int (LUI_CALL* Present)(void* Context, int Id, int HasClip, double X, double Y,
        double Width, double Height, int Rank, int InputVisible);
} LuiPresentationCallbacksV1;

/* Private owner image bridge. Source is a declared captured name, never a URL.
   Load must return promptly and complete on the owner via Lui_ImageCompleted.
   Cancellation retires the token; late/duplicate completions are rejected. */
typedef struct LuiImageCallbacksV1 {
    uint32_t StructSize, AbiVersion;
    void* Context;
    int (LUI_CALL* Load)(void* Context, int Id, uint64_t Token, const char* Source);
    void (LUI_CALL* Cancel)(void* Context, uint64_t Token);
} LuiImageCallbacksV1;

/* Internal platform bridge. Async completions must return to the owner thread. */
typedef struct LuiPlatformCallbacksV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    void* Context;
    int (LUI_CALL* WriteClipboardText)(void* Context, const char* Text);
    int (LUI_CALL* ReadClipboardText)(void* Context, uint64_t RequestId);
    int (LUI_CALL* OpenFile)(void* Context, uint64_t RequestId);
} LuiPlatformCallbacksV1;

LUI_API LuiRuntime* LUI_CALL Lui_Create(void);
#define LUI_DIAGNOSTICS_ABI_VERSION 1u
#define LUI_DIAGNOSTICS_DEVELOPMENT 1u
#define LUI_DIAGNOSTICS_RESTRICTED 2u
typedef struct LuiDiagnosticPolicyV1 { uint32_t StructSize, AbiVersion, Mode, Reserved; } LuiDiagnosticPolicyV1;
/* Owner policy before VM creation; invalid input has no legacy/raw fallback. */
LUI_API LuiRuntime* LUI_CALL Lui_CreateWithPolicy(const LuiDiagnosticPolicyV1* Policy);
LUI_API const char* LUI_CALL Lui_GetDiagnosticsJson(LuiRuntime* Runtime);
/* Explicit development owner grant, never a manifest/script/webview flag.
   This separate memory-only surface is not sent to diagnostic callbacks. */
LUI_API int LUI_CALL Lui_BeginInspection(LuiRuntime* Runtime);
LUI_API const char* LUI_CALL Lui_GetInspectionTreeJson(LuiRuntime* Runtime);
LUI_API void LUI_CALL Lui_SetBackend(LuiRuntime* Runtime, LuiBackendCallbacks Callbacks);
LUI_API int LUI_CALL Lui_SetPresentationCallbacks(LuiRuntime* Runtime, const LuiPresentationCallbacksV1* Callbacks);
LUI_API int LUI_CALL Lui_SetImageCallbacks(LuiRuntime* Runtime, const LuiImageCallbacksV1* Callbacks);
LUI_API int LUI_CALL Lui_ImageCompleted(LuiRuntime* Runtime, int Id, uint64_t Token, int Success);
/* Private owner telemetry: 1 input handler/queue admission through owner response;
   2 owner presentation through native submission. Bounded aggregate samples. */
LUI_API int LUI_CALL Lui_RecordPerformance(LuiRuntime* Runtime, int Kind, double Milliseconds);
/* Owner aggregate projection, no content/IDs/paths; valid until next query. */
LUI_API const char* LUI_CALL Lui_GetPerformanceJson(LuiRuntime* Runtime);
LUI_API int LUI_CALL Lui_SetPlatformCallbacks(LuiRuntime* Runtime, const LuiPlatformCallbacksV1* Callbacks);
/* Result is UTF-8 or null for cancellation. Error is UTF-8 or null on success. */
LUI_API int LUI_CALL Lui_CompletePlatformRequest(LuiRuntime* Runtime, uint64_t RequestId,
    const char* Result, const char* Error);
LUI_API void LUI_CALL Lui_ReportBackendError(LuiRuntime* Runtime, const char* Message);
LUI_API void LUI_CALL Lui_SetBackendName(LuiRuntime* Runtime, const char* Name);
LUI_API void LUI_CALL Lui_SetLogCallback(LuiRuntime* Runtime, void* Context, LuiLogCallback Callback);
LUI_API int LUI_CALL Lui_DeclareCapabilities(LuiRuntime* Runtime, const LuiCapabilityDeclarationV1* Declaration);
LUI_API int LUI_CALL Lui_ConfigureSandbox(LuiRuntime* Runtime, const LuiSandboxLimitsV1* Limits);
LUI_API int LUI_CALL Lui_SetNetworkPolicy(LuiRuntime* Runtime, const LuiNetworkPolicyV1* Policy);
LUI_API int LUI_CALL Lui_SetTlsOptions(LuiRuntime* Runtime, const LuiTlsOptionsV1* Options);
/* Enable development-only source locations before the first script. */
LUI_API int LUI_CALL Lui_EnableSourceProvenance(LuiRuntime* Runtime);
/* Registers a packaged asset name before scripts; source paths stay in the host. */
LUI_API int LUI_CALL Lui_RegisterAsset(LuiRuntime* Runtime, const char* Name);
/* Path must be absolute. Call before the first script; failures leave the runtime usable. */
LUI_API int LUI_CALL Lui_LoadExtension(LuiRuntime* Runtime, const char* Path);
/* Host bindings use the same primitive method contract and reflection as extensions.
   The host owns MethodContext until after Lui_Destroy and registers before scripts. */
LUI_API int LUI_CALL Lui_RegisterHostMethod(LuiRuntime* Runtime, const char* ServiceName,
    const char* MethodName, const char* Type, LuiExtensionMethodV1 Method, void* MethodContext);
LUI_API void LUI_CALL Lui_SetHostError(LuiRuntime* Runtime, const char* Message);
LUI_API int LUI_CALL Lui_RegisterHostSignal(LuiRuntime* Runtime, const char* ServiceName,
    const char* SignalName, const char* Type);
LUI_API int LUI_CALL Lui_EmitHostSignal(LuiRuntime* Runtime, const char* ServiceName,
    const char* SignalName, const LuiValueV1* Arguments, uint32_t ArgumentCount);
/* Runtime-specific reflection for dynamically registered extension services. */
LUI_API const char* LUI_CALL Lui_GetExtensionSchemaJson(LuiRuntime* Runtime);
/* Compiles without executing source or mutating the object tree. Diagnostics are in Lui_GetLastError. */
LUI_API int LUI_CALL Lui_CheckScript(LuiRuntime* Runtime, const char* Source);
LUI_API int LUI_CALL Lui_RunScript(LuiRuntime* Runtime, const char* Source, const char* ChunkName);
LUI_API int LUI_CALL Lui_Activate(LuiRuntime* Runtime, int Id);
LUI_API int LUI_CALL Lui_TextChanged(LuiRuntime* Runtime, int Id, const char* Text);
/* Atomic committed UTF-8 text/selection report. Selection uses scalar indices;
   text is bounded to 65534 scalars/UTF-8 bytes. Secret widgets report zero selection. */
LUI_API int LUI_CALL Lui_TextInput(LuiRuntime* Runtime, int Id, const char* Text, int SelectionStart, int SelectionLength);
/* Private adapter read-only owner query, permitted during dispatch. Capture when
   queuing text/preedit and compare before admission; script writes retire it. */
LUI_API int LUI_CALL Lui_GetTextInputRevision(LuiRuntime* Runtime, int Id, uint64_t* Revision);
/* Phase: 0 start, 1 preedit change, 2 end/cancel. Preedit is capped at 4KiB;
   committed text is reported separately. All callbacks run on the owner. */
LUI_API int LUI_CALL Lui_TextComposition(LuiRuntime* Runtime, int Id, int Phase, const char* Preedit);
LUI_API int LUI_CALL Lui_CheckedChanged(LuiRuntime* Runtime, int Id, int Checked);
LUI_API int LUI_CALL Lui_ValueChanged(LuiRuntime* Runtime, int Id, double Value);
/* Reports the native window content size in logical units. A later script Size assignment replaces this override. */
LUI_API int LUI_CALL Lui_WindowResized(LuiRuntime* Runtime, int Id, double Width, double Height);
/* Canonical viewport query (headless or presentation-capable host), window-local logical coordinates. 1 means a valid
   query (HitId may be 0); 0 means invalid/busy. No input is dispatched. */
LUI_API int LUI_CALL Lui_HitTest(LuiRuntime* Runtime, int WindowId, double X, double Y, int* HitId);
/* Logical deltas, positive right/down, each axis bounded to +/-1e6. Unconsumed
   motion chains to ancestor scrollers. Native callbacks are deferred when busy. */
LUI_API int LUI_CALL Lui_ScrollInput(LuiRuntime* Runtime, int OriginId, double DeltaX, double DeltaY);
LUI_API int LUI_CALL Lui_FocusChanged(LuiRuntime* Runtime, int Id, int Focused);
LUI_API int LUI_CALL Lui_HoverChanged(LuiRuntime* Runtime, int Id, int Hovered);
/* Phase: 0 pressed, 1 moved, 2 released, 3 canceled. Device: 0 mouse, 1 pen, 2 touch, 3 touchpad. */
LUI_API int LUI_CALL Lui_PointerInput(LuiRuntime* Runtime, int Id, int Phase, int Device,
    unsigned int PointerId, double X, double Y);
/* Phase: 0 pressed, 1 repeated, 2 released. Key is a canonical portable key name. */
LUI_API int LUI_CALL Lui_KeyInput(LuiRuntime* Runtime, int Id, int Phase, const char* Key);
LUI_API int LUI_CALL Lui_Pump(LuiRuntime* Runtime);
/* Host notification. Delivered to ThemeService listeners by the next owner-thread pump. */
LUI_API int LUI_CALL Lui_SystemThemeChanged(LuiRuntime* Runtime, const char* Theme);
LUI_API const char* LUI_CALL Lui_GetLastError(LuiRuntime* Runtime);
LUI_API const char* LUI_CALL Lui_GetSchemaJson(void);
/* Host-only preview snapshot. UTF-8 JSON remains valid until the next snapshot or destruction.
   Returns null and sets Lui_GetLastError when serialization fails. */
LUI_API const char* LUI_CALL Lui_GetPreviewTreeJson(LuiRuntime* Runtime);
LUI_API void LUI_CALL Lui_Destroy(LuiRuntime* Runtime);

#ifdef __cplusplus
}
#endif
