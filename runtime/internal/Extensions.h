#pragma once

#include "State.h"

struct lua_State;

bool HasExtensionService(const LuiRuntime* Runtime, const std::string& Name);
int PushExtensionService(lua_State* State, LuiRuntime* Runtime, const std::string& Name);
void CloseExtensions(LuiRuntime* Runtime);
void CloseExtensionListeners(LuiRuntime* Runtime);
int DrainUiCompletions(LuiRuntime* Runtime);
