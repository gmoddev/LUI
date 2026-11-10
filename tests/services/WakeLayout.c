#include "LuiOwnerWake.h"
#include <stddef.h>
_Static_assert(sizeof(LuiNativeWakeV1)==32,"private wake v1 x64 layout");
_Static_assert(offsetof(LuiNativeWakeV1,Context)==8,"context offset");
_Static_assert(offsetof(LuiNativeWakeV1,Request)==16,"request offset");
_Static_assert(offsetof(LuiNativeWakeV1,Release)==24,"release offset");
