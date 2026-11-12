#pragma once
#include "LuiRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* INTERNAL host interface, independent of the stable extension ABI. No VM entry,
   application execution, extension loading, filesystem lookup during Resolve,
   or diagnostic policy enforcement. All returned content is trusted-host data;
   callers must not publish it as a redacted diagnostic/preview projection. */
#define LUI_PROJECT_ABI_VERSION 1u
typedef struct LuiProject LuiProject;

/* Desktop owner only. Zero on success; LUI_NATIVE_* status otherwise. Call on
   the runtime owner thread after explicit Storage grant, before native binding
   or execution. Project must declare Storage.ApplicationId. DataRoot is an
   existing trusted per-user directory (LocalAppData on Windows, home on Linux);
   ApplicationRoot is the trusted source/package directory used for preparation.
   Both are absolute NUL-terminated UTF-8, max 4096 bytes. No application path is
   accepted. Binds the scope to ApplicationId and native directory identity.
   Neither path nor the resulting scope is exposed to Luau. Desktop exports only. */
LUI_API uint32_t LUI_CALL Lui_InstallProjectStorageV1(LuiRuntime* Runtime, const LuiProject* Project,
    const char* DataRoot, const char* ApplicationRoot);
typedef int (LUI_CALL* LuiProjectCanceledCallback)(void* Context);

typedef struct LuiProjectOptionsV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    uint64_t Reserved; /* Must be zero. */
    void* CancellationContext;
    LuiProjectCanceledCallback Canceled;
    /* Zero selects the documented default; positive values may only narrow it. */
    uint64_t ManifestBytes, LockBytes, Owners, Files, FileBytes, SourceBytes;
    uint64_t ArtifactBytes, BytecodeBytes, RetainedBytes;
} LuiProjectOptionsV1;

typedef struct LuiProjectSummaryV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    uint32_t PreparationVersion; /* 2 = captured sources plus Luau bytecode. */
    uint32_t SourceCount;
    const char* EntryId;
    const char* SourceDigest;
    const char* CompiledDigest;
    const char* Descriptor;
    uint64_t DescriptorBytes, BytecodeBytes, RetainedBytes;
} LuiProjectSummaryV1;

typedef struct LuiProjectSourceV1 {
    uint32_t StructSize;
    uint32_t AbiVersion;
    const char* Id;
    const char* Owner;
    const char* Path;
    const char* StagedPath;
    const char* SourceHash;
    const char* ArtifactHash;
    const char* Source;
    uint64_t SourceBytes;
    const char* Bytecode;
    uint64_t BytecodeBytes;
} LuiProjectSourceV1;

/* Length-delimited UTF-8 path, max 4096 bytes, no embedded NUL/BOM. Prepare is
   synchronous and may run on a worker; Canceled runs on that same caller thread,
   must not enter a VM, and is checked between files/compiler calls, not within
   compilation. One caller owns each preparation operation. Output is null on
   failure. Errors are bounded, NUL terminated when capacity > 0; no raw payload.
   Null Error with zero ErrorCapacity is supported. */
LUI_API int LUI_CALL Lui_ProjectPrepare(const LuiProjectOptionsV1* Options,
    const char* ManifestPath, uint64_t ManifestPathBytes, LuiProject** Output,
    char* Error, uint64_t ErrorCapacity);
/* One reference on success. Retain an OWNED LIVE reference before transferring
   to another thread. Release each reference once. No release/query race against
   the final reference; no calls on stale/foreign handles. */
LUI_API void LUI_CALL Lui_ProjectRetain(LuiProject* Project);
LUI_API void LUI_CALL Lui_ProjectRelease(LuiProject* Project);
/* Immutable queries may run concurrently under retained references. Views are
   borrowed until the handle's final Release. Source order is sorted identity.
   Initialize StructSize/AbiVersion in outputs; exact v1 sizes are required.
   Query failure leaves the output untouched. No query performs source IO. */
LUI_API int LUI_CALL Lui_ProjectGetSummary(const LuiProject* Project, LuiProjectSummaryV1* Output);
LUI_API int LUI_CALL Lui_ProjectGetSource(const LuiProject* Project, uint32_t Index, LuiProjectSourceV1* Output);
LUI_API int LUI_CALL Lui_ProjectResolve(const LuiProject* Project,
    const char* Importer, uint64_t ImporterBytes, const char* Request, uint64_t RequestBytes,
    LuiProjectSourceV1* Output, char* Error, uint64_t ErrorCapacity);

/* Internal owner-thread execution, separate from preparation. Policy-enabled
   runtimes use Flags=0; legacy development hosts require explicit opt-in and
   cannot run Restricted projects. Requires a fresh runtime; a failed startup
   retires its VM and leaves an empty tree. Destroy it and create a fresh runtime
   for reload, even on failure. Runtime retains immutable inputs independently
   of the caller's handle. Native providers require verified owner-trusted loading
   first; their code is privileged even when VM resource quotas are enabled. */
#define LUI_PROJECT_RUN_ABI_VERSION 1u
#define LUI_PROJECT_RUN_DEVELOPMENT_DIAGNOSTICS 1u
typedef struct LuiProjectRunOptionsV1 {
    uint32_t StructSize, AbiVersion;
    uint32_t Flags;
    uint32_t MaxImportDepth; /* Zero = 64; may narrow, never raise. */
    uint64_t Reserved; /* Must be zero. */
} LuiProjectRunOptionsV1;
LUI_API int LUI_CALL Lui_RunProject(LuiRuntime* Runtime, const LuiProject* Project, const LuiProjectRunOptionsV1* Options);
#define LUI_PROJECT_NATIVE_ABI_VERSION 1u
#define LUI_PROJECT_NATIVE_TRUSTED 1u
typedef struct LuiProjectNativeOptionsV1 { uint32_t StructSize, AbiVersion, Flags, Reserved; } LuiProjectNativeOptionsV1;
/* Explicit owner trust, policy-enabled fresh runtime, captured binaries only.
   Native code is privileged and is not sandboxed by VM quotas. */
LUI_API int LUI_CALL Lui_LoadProjectNative(LuiRuntime* Runtime,const LuiProject* Project,const LuiProjectNativeOptionsV1* Options);

#ifdef __cplusplus
}
#endif
