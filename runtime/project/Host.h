#pragma once
#include "Project.h"
struct LuiProject;
// Trusted internal adapter. The caller owns a live C handle during this query.
std::shared_ptr<const Lui::Project::CompiledSnapshot> GetProjectSnapshot(const LuiProject* Project);
LuiProject* WrapProject(std::shared_ptr<const Lui::Project::CompiledSnapshot> Snapshot);
