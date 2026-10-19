#pragma once

#include "State.h"

struct lua_State;

int PushPlatformService(lua_State* State, LuiRuntime* Runtime, const std::string& Name);
bool PlatformServiceAvailable(const LuiRuntime* Runtime, const std::string& Name);
int DrainPlatformRequests(LuiRuntime* Runtime);
void ClosePlatformRequests(LuiRuntime* Runtime);
