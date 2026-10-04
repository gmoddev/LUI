#pragma once

struct LuiRuntime;
struct lua_State;

void RegisterNetworkTypes(lua_State* State);
void PushNetworkService(lua_State* State);
void PushHttpService(lua_State* State);
void PushHttpServerService(lua_State* State);
bool NetworkTaskReady(LuiRuntime* Runtime, int Reference);
bool CompleteNetworkTask(LuiRuntime* Runtime, int Reference, lua_State* Thread, int Status);
int DrainNetworkCompletions(LuiRuntime* Runtime);
void CloseNetwork(LuiRuntime* Runtime);
