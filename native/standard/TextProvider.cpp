#include "TextProvider.h"

extern "C" LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionQuery(LuiExtensionInfoV1* Info) {
    if (!Info || Info->StructSize<sizeof(*Info)) return 0;
    *Info={sizeof(*Info),1,Lui::Standard::Name,1,LUI_CAPABILITY_NATIVE_EXTENSIONS|LUI_CAPABILITY_HOST_SERVICES};
    return 1;
}
extern "C" LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionInit(const LuiHostApiV1* Host,void** Context) {
    if (!Host || Host->AbiVersion!=1 || Host->StructSize<sizeof(*Host) || !Context) return 0;
    try { *Context=Lui::Standard::Register(*Host).release(); return *Context!=nullptr; }
    catch (...) { return 0; }
}
extern "C" LUI_EXTENSION_EXPORT void LUI_EXTENSION_CALL LuiExtensionShutdown(void* Context) {
    delete static_cast<Lui::Standard::TextState*>(Context);
}
