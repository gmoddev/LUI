#pragma once

struct LuiRuntime;
struct lua_State;

void RegisterNetworkTypes(lua_State* State);
void PushNetworkService(lua_State* State);
void PushHttpService(lua_State* State);
int DrainNetworkCompletions(LuiRuntime* Runtime);
void CloseNetwork(LuiRuntime* Runtime);
