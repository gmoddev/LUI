#pragma once

#include "State.h"

bool CheckOwner(LuiRuntime* Runtime);
int QueueBackendEventIfBusy(LuiRuntime* Runtime, BackendEvent Event);
void FlushLayout(LuiRuntime* Runtime);
void FireSignal(LuiRuntime* Runtime, Node* Value, const char* Signal,
    const PointerInputValue* Pointer = nullptr, const KeyboardInputValue* Keyboard = nullptr);
