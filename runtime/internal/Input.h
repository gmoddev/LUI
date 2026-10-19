#pragma once

#include "State.h"

bool CanReceiveInput(const LuiRuntime* Runtime, const Node* Value);
void ClearInvalidInput(LuiRuntime* Runtime);
