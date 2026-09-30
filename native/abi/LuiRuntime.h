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
typedef void (LUI_CALL* LuiLogCallback)(void* Context, const char* Level, const char* Message);

/* Internal host contract. This is not the stable extension ABI. */
typedef struct LuiBackendCallbacks {
    void* Context;
    /* Return 1 on success, 0 on failure. Failures may supply detail through Lui_ReportBackendError. */
    int (LUI_CALL* Create)(void* Context, int Id, const char* ClassName);
    int (LUI_CALL* Property)(void* Context, int Id, const char* Name, const char* Value);
    int (LUI_CALL* Parent)(void* Context, int Id, int ParentId);
    int (LUI_CALL* Arrange)(void* Context, int Id, double X, double Y, double Width, double Height);
    int (LUI_CALL* Destroy)(void* Context, int Id);
} LuiBackendCallbacks;

LUI_API LuiRuntime* LUI_CALL Lui_Create(void);
LUI_API void LUI_CALL Lui_SetBackend(LuiRuntime* Runtime, LuiBackendCallbacks Callbacks);
LUI_API void LUI_CALL Lui_ReportBackendError(LuiRuntime* Runtime, const char* Message);
LUI_API void LUI_CALL Lui_SetBackendName(LuiRuntime* Runtime, const char* Name);
LUI_API void LUI_CALL Lui_SetLogCallback(LuiRuntime* Runtime, void* Context, LuiLogCallback Callback);
LUI_API int LUI_CALL Lui_RunScript(LuiRuntime* Runtime, const char* Source, const char* ChunkName);
LUI_API int LUI_CALL Lui_Activate(LuiRuntime* Runtime, int Id);
LUI_API int LUI_CALL Lui_TextChanged(LuiRuntime* Runtime, int Id, const char* Text);
LUI_API int LUI_CALL Lui_CheckedChanged(LuiRuntime* Runtime, int Id, int Checked);
LUI_API int LUI_CALL Lui_ValueChanged(LuiRuntime* Runtime, int Id, double Value);
LUI_API int LUI_CALL Lui_FocusChanged(LuiRuntime* Runtime, int Id, int Focused);
LUI_API int LUI_CALL Lui_HoverChanged(LuiRuntime* Runtime, int Id, int Hovered);
/* Phase: 0 pressed, 1 moved, 2 released, 3 canceled. Device: 0 mouse, 1 pen, 2 touch, 3 touchpad. */
LUI_API int LUI_CALL Lui_PointerInput(LuiRuntime* Runtime, int Id, int Phase, int Device,
    unsigned int PointerId, double X, double Y);
LUI_API int LUI_CALL Lui_Pump(LuiRuntime* Runtime);
LUI_API const char* LUI_CALL Lui_GetLastError(LuiRuntime* Runtime);
LUI_API const char* LUI_CALL Lui_GetSchemaJson(void);
LUI_API void LUI_CALL Lui_Destroy(LuiRuntime* Runtime);

#ifdef __cplusplus
}
#endif
