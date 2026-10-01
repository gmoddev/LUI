#include "LuiExtension.h"

#include <stdlib.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

typedef struct SampleContext {
    void* HostContext;
    void (LUI_EXTENSION_CALL* SetError)(void*, const char*);
    int (LUI_EXTENSION_CALL* ScheduleUi)(void*, LuiUiCompletionV1, void*);
    int CompletionCount;
    int CompletedOnOwner;
    int WorkerStarted;
#if defined(_WIN32)
    DWORD OwnerThreadId;
    HANDLE Worker;
#else
    pthread_t OwnerThreadId;
    pthread_t Worker;
#endif
} SampleContext;

static void LUI_EXTENSION_CALL CompleteOnUi(void* CompletionContext) {
    SampleContext* Context = (SampleContext*)CompletionContext;
    Context->CompletionCount++;
#if defined(_WIN32)
    Context->CompletedOnOwner = GetCurrentThreadId() == Context->OwnerThreadId;
#else
    Context->CompletedOnOwner = pthread_equal(pthread_self(), Context->OwnerThreadId);
#endif
}

#if defined(_WIN32)
static DWORD WINAPI RunWorker(void* WorkerContext) {
#else
static void* RunWorker(void* WorkerContext) {
#endif
    SampleContext* Context = (SampleContext*)WorkerContext;
    Context->ScheduleUi(Context->HostContext, CompleteOnUi, Context);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int LUI_EXTENSION_CALL BeginCompletion(void* MethodContext, const LuiValueV1* Arguments,
    uint32_t ArgumentCount, LuiValueV1* Result) {
    SampleContext* Context = (SampleContext*)MethodContext;
    (void)Arguments;
    if (ArgumentCount || Context->WorkerStarted) {
        Context->SetError(Context->HostContext, "completion already started or arguments supplied");
        return 0;
    }
#if defined(_WIN32)
    Context->Worker = CreateThread(NULL, 0, RunWorker, Context, 0, NULL);
    if (!Context->Worker) return 0;
#else
    if (pthread_create(&Context->Worker, NULL, RunWorker, Context) != 0) return 0;
#endif
    Context->WorkerStarted = 1;
    Result->Type = LUI_VALUE_BOOLEAN;
    Result->Boolean = 1;
    return 1;
}

static int LUI_EXTENSION_CALL GetCompletionCount(void* MethodContext, const LuiValueV1* Arguments,
    uint32_t ArgumentCount, LuiValueV1* Result) {
    SampleContext* Context = (SampleContext*)MethodContext;
    (void)Arguments;
    if (ArgumentCount) return 0;
    Result->Type = LUI_VALUE_NUMBER;
    Result->Number = Context->CompletionCount;
    return 1;
}

static int LUI_EXTENSION_CALL WasCompletionOnOwner(void* MethodContext, const LuiValueV1* Arguments,
    uint32_t ArgumentCount, LuiValueV1* Result) {
    SampleContext* Context = (SampleContext*)MethodContext;
    (void)Arguments;
    if (ArgumentCount) return 0;
    Result->Type = LUI_VALUE_BOOLEAN;
    Result->Boolean = Context->CompletedOnOwner;
    return 1;
}

static int LUI_EXTENSION_CALL Add(void* MethodContext, const LuiValueV1* Arguments,
    uint32_t ArgumentCount, LuiValueV1* Result) {
    SampleContext* Context = (SampleContext*)MethodContext;
    if (ArgumentCount != 2 || Arguments[0].Type != LUI_VALUE_NUMBER ||
        Arguments[1].Type != LUI_VALUE_NUMBER) {
        Context->SetError(Context->HostContext, "Add requires two numbers");
        return 0;
    }
    Result->Type = LUI_VALUE_NUMBER;
    Result->Number = Arguments[0].Number + Arguments[1].Number;
    return 1;
}

static int LUI_EXTENSION_CALL Echo(void* MethodContext, const LuiValueV1* Arguments,
    uint32_t ArgumentCount, LuiValueV1* Result) {
    SampleContext* Context = (SampleContext*)MethodContext;
    if (ArgumentCount != 1 || Arguments[0].Type != LUI_VALUE_STRING) {
        Context->SetError(Context->HostContext, "Echo requires one string");
        return 0;
    }
    Result->Type = LUI_VALUE_STRING;
    Result->Text = Arguments[0].Text;
    Result->TextLength = Arguments[0].TextLength;
    return 1;
}

LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionQuery(LuiExtensionInfoV1* Info) {
    if (!Info || Info->StructSize < sizeof(LuiExtensionInfoV1)) return 0;
#if defined(LUI_SAMPLE_BAD_ABI)
    Info->AbiVersion = LUI_EXTENSION_ABI_VERSION + 1;
#else
    Info->AbiVersion = LUI_EXTENSION_ABI_VERSION;
#endif
#if defined(LUI_SAMPLE_FAIL_INIT)
    Info->Name = "SampleFailed";
#else
    Info->Name = "SampleExtension";
#endif
    Info->ExtensionVersion = 1;
    Info->RequiredCapabilities = LUI_CAPABILITY_NATIVE_EXTENSIONS;
#if defined(LUI_SAMPLE_EXTRA_CAPABILITY)
    Info->RequiredCapabilities |= UINT64_C(2);
#endif
    return 1;
}

LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionInit(const LuiHostApiV1* Host,
    void** ExtensionContext) {
    if (!Host || Host->StructSize < sizeof(LuiHostApiV1) ||
        Host->AbiVersion != LUI_EXTENSION_ABI_VERSION || !ExtensionContext) return 0;
    SampleContext* Context = (SampleContext*)calloc(1, sizeof(SampleContext));
    if (!Context) return 0;
    Context->HostContext = Host->HostContext;
    Context->SetError = Host->SetError;
    Context->ScheduleUi = Host->ScheduleUi;
#if defined(_WIN32)
    Context->OwnerThreadId = GetCurrentThreadId();
#else
    Context->OwnerThreadId = pthread_self();
#endif
    *ExtensionContext = Context;
    if (!Host->RegisterMethod(Host->HostContext, "NativeMath", "Add",
        "(A: number, B: number) -> number", Add, Context)) return 0;
#if defined(LUI_SAMPLE_FAIL_INIT)
    Host->SetError(Host->HostContext, "synthetic init failure");
    return 0;
#else
    if (!Host->RegisterMethod(Host->HostContext, "NativeMath", "Echo",
        "(Text: string) -> string", Echo, Context)) return 0;
    if (!Host->RegisterMethod(Host->HostContext, "NativeMath", "BeginCompletion",
        "() -> boolean", BeginCompletion, Context)) return 0;
    if (!Host->RegisterMethod(Host->HostContext, "NativeMath", "GetCompletionCount",
        "() -> number", GetCompletionCount, Context)) return 0;
    if (!Host->RegisterMethod(Host->HostContext, "NativeMath", "WasCompletionOnOwner",
        "() -> boolean", WasCompletionOnOwner, Context)) return 0;
    Host->Log(Host->HostContext, "service methods registered");
    return 1;
#endif
}

LUI_EXTENSION_EXPORT void LUI_EXTENSION_CALL LuiExtensionShutdown(void* ExtensionContext) {
    SampleContext* Context = (SampleContext*)ExtensionContext;
    if (!Context) return;
    if (Context->WorkerStarted) {
#if defined(_WIN32)
        WaitForSingleObject(Context->Worker, INFINITE);
        CloseHandle(Context->Worker);
#else
        pthread_join(Context->Worker, NULL);
#endif
    }
    free(Context);
}
