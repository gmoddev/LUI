#ifndef LUI_FILES_H
#define LUI_FILES_H
#include "LuiNativeServices.h"
#ifdef __cplusplus
extern "C" {
#endif
#define LUI_FILE_PICK_OPEN 1u
#define LUI_FILE_PICK_SAVE 2u
#define LUI_FILE_PICK_FOLDER 3u
/* Trusted desktop adapter. All callbacks/completions run on the runtime owner.
   Start returns a native status (zero admits). Show only an explicit picker with
   a visible parent. Name is a suggested leaf, valid only during StartPicker.
   Retired UI must never complete against a destroyed runtime. */
typedef struct LuiFilePickerCallbacksV1 {
    uint32_t StructSize, AbiVersion;
    void* Context;
    uint32_t (LUI_CALL* StartPicker)(void* Context, uint64_t Request, uint32_t Kind, const char* Name);
    void (LUI_CALL* CancelPicker)(void* Context, uint64_t Request);
} LuiFilePickerCallbacksV1;
LUI_API uint32_t LUI_CALL Lui_InstallFilesV1(LuiRuntime* Runtime, const LuiFilePickerCallbacksV1* Callbacks);
/* UTF-8 Path comes from the trusted picker. Null with Status=0 means dismissal
   and settles the native request as Canceled (the Files wrapper returns nil).
   Duplicate/late results are refused. No path is returned to ordinary Luau. */
LUI_API uint32_t LUI_CALL Lui_CompleteFilePickerV1(LuiRuntime* Runtime, uint64_t Request, const char* Path, uint32_t Status);
/* Admit a save destination without granting document reads. Requires a pending SaveFile. */
LUI_API uint32_t LUI_CALL Lui_CompleteWriteOnlyPickerV1(LuiRuntime* Runtime, uint64_t Request, const char* Path);
/* Admit direct-child edits only after an explicit trusted writable-folder grant. */
LUI_API uint32_t LUI_CALL Lui_CompleteWritableFolderV1(LuiRuntime* Runtime, uint64_t Request, const char* Path);
#ifdef __cplusplus
}
#endif
#endif
