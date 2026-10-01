#ifndef LUI_EXTENSION_H
#define LUI_EXTENSION_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(LUI_EXTENSION_BUILD)
#    define LUI_EXTENSION_EXPORT __declspec(dllexport)
#  else
#    define LUI_EXTENSION_EXPORT
#  endif
#  define LUI_EXTENSION_CALL __cdecl
#else
#  if defined(LUI_EXTENSION_BUILD)
#    define LUI_EXTENSION_EXPORT __attribute__((visibility("default")))
#  else
#    define LUI_EXTENSION_EXPORT
#  endif
#  define LUI_EXTENSION_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_EXTENSION_ABI_VERSION 1u
#define LUI_CAPABILITY_NATIVE_EXTENSIONS UINT64_C(1)
#define LUI_CAPABILITY_HOST_SERVICES UINT64_C(2)
#define LUI_CAPABILITY_CLIPBOARD UINT64_C(4)
#define LUI_CAPABILITY_DIALOGS UINT64_C(8)

typedef enum LuiValueTypeV1 {
    LUI_VALUE_NIL = 0,
    LUI_VALUE_BOOLEAN = 1,
    LUI_VALUE_NUMBER = 2,
    LUI_VALUE_STRING = 3
} LuiValueTypeV1;

/* Strings are borrowed only for the duration of the call. The host copies results immediately. */
typedef struct LuiValueV1 {
    uint32_t StructSize;
    uint32_t Type;
    double Number;
    const char* Text;
    uint64_t TextLength;
    int32_t Boolean;
    uint32_t Reserved;
} LuiValueV1;

typedef int (LUI_EXTENSION_CALL* LuiExtensionMethodV1)(void* MethodContext,
    const LuiValueV1* Arguments, uint32_t ArgumentCount, LuiValueV1* Result);
typedef void (LUI_EXTENSION_CALL* LuiUiCompletionV1)(void* CompletionContext);

typedef struct LuiHostApiV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    void* HostContext;
    /* Registration is valid only during LuiExtensionInit. Type is a Luau method signature. */
    int (LUI_EXTENSION_CALL* RegisterMethod)(void* HostContext, const char* ServiceName,
        const char* MethodName, const char* Type, LuiExtensionMethodV1 Method, void* MethodContext);
    void (LUI_EXTENSION_CALL* Log)(void* HostContext, const char* Message);
    void (LUI_EXTENSION_CALL* SetError)(void* HostContext, const char* Message);
    /* Callable from a worker thread. The completion runs in Lui_Pump on the owner thread. */
    int (LUI_EXTENSION_CALL* ScheduleUi)(void* HostContext, LuiUiCompletionV1 Completion,
        void* CompletionContext);
    /* Additive v1 entries: test StructSize before using from a preexisting host. */
    int (LUI_EXTENSION_CALL* RegisterSignal)(void* HostContext, const char* ServiceName,
        const char* SignalName, const char* Type);
    /* Owner thread only; worker threads use ScheduleUi first. */
    int (LUI_EXTENSION_CALL* EmitSignal)(void* HostContext, const char* ServiceName,
        const char* SignalName, const LuiValueV1* Arguments, uint32_t ArgumentCount);
} LuiHostApiV1;

typedef struct LuiExtensionInfoV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    const char* Name;
    uint32_t ExtensionVersion;
    uint64_t RequiredCapabilities;
} LuiExtensionInfoV1;

typedef int (LUI_EXTENSION_CALL* LuiExtensionQueryV1)(LuiExtensionInfoV1* Info);
typedef int (LUI_EXTENSION_CALL* LuiExtensionInitV1)(const LuiHostApiV1* Host, void** ExtensionContext);
typedef void (LUI_EXTENSION_CALL* LuiExtensionShutdownV1)(void* ExtensionContext);

/* Every extension exports these exact C names. Query must not register methods or start work. */
LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionQuery(LuiExtensionInfoV1* Info);
LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionInit(const LuiHostApiV1* Host, void** ExtensionContext);
LUI_EXTENSION_EXPORT void LUI_EXTENSION_CALL LuiExtensionShutdown(void* ExtensionContext);

#ifdef __cplusplus
}
#endif

#endif
