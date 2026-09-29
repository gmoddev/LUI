#pragma once

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

/* Internal Foundation 0 host contract. This is not the stable extension ABI. */
typedef struct LuiBackendCallbacks {
    void* Context;
    void (LUI_CALL* Create)(void* Context, int Id, const char* ClassName);
    void (LUI_CALL* Property)(void* Context, int Id, const char* Name, const char* Value);
    void (LUI_CALL* Parent)(void* Context, int Id, int ParentId);
    void (LUI_CALL* Arrange)(void* Context, int Id, double X, double Y, double Width, double Height);
    void (LUI_CALL* Destroy)(void* Context, int Id);
} LuiBackendCallbacks;

LUI_API LuiRuntime* LUI_CALL Lui_Create(void);
LUI_API void LUI_CALL Lui_SetBackend(LuiRuntime* Runtime, LuiBackendCallbacks Callbacks);
LUI_API void LUI_CALL Lui_SetBackendName(LuiRuntime* Runtime, const char* Name);
LUI_API int LUI_CALL Lui_RunScript(LuiRuntime* Runtime, const char* Source, const char* ChunkName);
LUI_API int LUI_CALL Lui_Activate(LuiRuntime* Runtime, int Id);
LUI_API int LUI_CALL Lui_TextChanged(LuiRuntime* Runtime, int Id, const char* Text);
LUI_API int LUI_CALL Lui_CheckedChanged(LuiRuntime* Runtime, int Id, int Checked);
LUI_API int LUI_CALL Lui_ValueChanged(LuiRuntime* Runtime, int Id, double Value);
LUI_API int LUI_CALL Lui_FocusChanged(LuiRuntime* Runtime, int Id, int Focused);
LUI_API int LUI_CALL Lui_Pump(LuiRuntime* Runtime);
LUI_API const char* LUI_CALL Lui_GetLastError(LuiRuntime* Runtime);
LUI_API const char* LUI_CALL Lui_GetSchemaJson(void);
LUI_API void LUI_CALL Lui_Destroy(LuiRuntime* Runtime);

#ifdef __cplusplus
}
#endif
