#include "LuiExtension.h"

#include <stddef.h>

/* The prefix from the original version 1 host table, before signals were added. */
typedef struct LegacyHostApiV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    void* HostContext;
    int (LUI_EXTENSION_CALL* RegisterMethod)(void*, const char*, const char*, const char*,
        LuiExtensionMethodV1, void*);
    void (LUI_EXTENSION_CALL* Log)(void*, const char*);
    void (LUI_EXTENSION_CALL* SetError)(void*, const char*);
    int (LUI_EXTENSION_CALL* ScheduleUi)(void*, LuiUiCompletionV1, void*);
} LegacyHostApiV1;

static int LUI_EXTENSION_CALL Value(void* Context, const LuiValueV1* Arguments,
    uint32_t Count, LuiValueV1* Result) {
    (void)Context;
    (void)Arguments;
    if (Count) return 0;
    Result->Type = LUI_VALUE_NUMBER;
    Result->Number = 123;
    return 1;
}

LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionQuery(LuiExtensionInfoV1* Info) {
    if (!Info || Info->StructSize < sizeof(*Info)) return 0;
    Info->AbiVersion = LUI_EXTENSION_ABI_VERSION;
    Info->Name = "LegacyExtension";
    Info->ExtensionVersion = 1;
    Info->RequiredCapabilities = LUI_CAPABILITY_NATIVE_EXTENSIONS;
    return 1;
}

LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionInit(const LuiHostApiV1* Current,
    void** ExtensionContext) {
    const LegacyHostApiV1* Host = (const LegacyHostApiV1*)Current;
    if (!Host || Host->StructSize < sizeof(*Host) ||
        Host->AbiVersion != LUI_EXTENSION_ABI_VERSION || !ExtensionContext) return 0;
    *ExtensionContext = NULL;
    return Host->RegisterMethod(Host->HostContext, "Legacy", "Value", "() -> number", Value, NULL);
}

LUI_EXTENSION_EXPORT void LUI_EXTENSION_CALL LuiExtensionShutdown(void* ExtensionContext) {
    (void)ExtensionContext;
}
