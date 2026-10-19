#pragma once
#include "State.h"
#include "../project/Project.h"
#include "LuiProject.h"
#include <map>

struct ModuleRecord {
    enum class State { Unloaded, Evaluating, Loaded, Failed } Status = State::Unloaded;
    std::string Id;
    const Lui::Project::Artifact* Artifact = nullptr;
    const char* Failure = "LoadFailed";
    int ExportReference = 0;
};
struct ModuleContext {
    std::shared_ptr<const Lui::Project::CompiledSnapshot> Snapshot;
    std::map<std::string, ModuleRecord> Records;
    std::vector<ModuleRecord*> Stack;
    int CoreReference = 0;
    unsigned MaxDepth = 64;
};
void RegisterModules(lua_State* State);
void CheckModuleEffect(lua_State* State);
void MarkModuleLimit(LuiRuntime* Runtime);
bool ConfigureProject(LuiRuntime* Runtime, std::shared_ptr<const Lui::Project::CompiledSnapshot> Snapshot,
    const LuiProjectRunOptionsV1* Options);
// Called inside a protected VM call. Creates private chunk globals and runs entry.
int ProjectEntry(lua_State* State);
