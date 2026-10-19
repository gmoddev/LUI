#pragma once
#include <string>
struct lua_State;
struct LuiRuntime;
struct Node;
namespace LuiStyle {
void Register(lua_State* State);
bool IsProperty(const std::string& Name);
bool Set(lua_State* State, Node* Value, const std::string& Name, int Index);
bool Push(lua_State* State, const Node* Value, const std::string& Name);
void Project(LuiRuntime* Runtime, Node* Value);
}
