#pragma once
#include "State.h"
void StartImage(LuiRuntime* Runtime, Node* Value);
void CancelImage(LuiRuntime* Runtime, Node* Value);
void RegisterImageResource(lua_State* State);
void AddImageAssetMethods(lua_State* State, LuiRuntime* Runtime);
void SetImageResource(lua_State* State, Node* Value, int Index);
void PushImageResource(lua_State* State, const Node* Value);
size_t RuntimeImageBytes(const LuiRuntime* Runtime);
size_t RuntimeImageCount(const LuiRuntime* Runtime);
