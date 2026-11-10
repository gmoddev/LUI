#pragma once

#include "State.h"

bool FrameBackendAvailable(const LuiRuntime* Runtime);
bool RegisterFrameSurface(LuiRuntime* Runtime, Node* Value);
void ClearFrameSurface(LuiRuntime* Runtime, Node* Value, bool Destroy = false);
const char* SubmitFrameSurface(LuiRuntime* Runtime, Node* Value, const void* Pixels,
    size_t Length, double Width, double Height, double RowStride, const char* Format, size_t FormatLength);
int PumpFrameSurfaces(LuiRuntime* Runtime);
