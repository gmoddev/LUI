#pragma once
#include "LuiExtension.h"
#include <cstddef>

// Private Ffi/provider negotiation. The callback is borrowed only for the
// scoped method call and may be invoked synchronously on its calling thread.
using LuiFfiComputeV1 = int (LUI_EXTENSION_CALL*)(void*, const char*, size_t, char*, size_t, size_t*);
using LuiFfiScopedDispatchV1 = int (LUI_EXTENSION_CALL*)(void*, const char*, size_t,
    LuiFfiComputeV1, void*, LuiValueV1*);
