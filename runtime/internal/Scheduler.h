#pragma once
#include <cstddef>
#include <chrono>

struct LuiRuntime;
struct lua_State;

// The owner thread resumes a scheduler-owned coroutine. Arguments must already
// be on its stack. A yielded task retains its registry reference only when an
// async operation has taken responsibility for completing it.
void ResumeScheduledTask(LuiRuntime* Runtime, int Reference, int ArgumentCount, bool AsError = false);

// Private opt-in zero-grant or built-in-storage host profile, not a public API.
// Configure before application entry. Legacy Lui_Pump is unchanged and must not
// be mixed with a partial bounded round. A deadline is checked BETWEEN callbacks;
// VM interrupt limits/watchdog quarantine still cover an individual dispatch.
bool ConfigureBoundedTasks(LuiRuntime* Runtime, size_t Maximum);
bool PumpBoundedTasks(LuiRuntime* Runtime, size_t MaximumEntries,
    std::chrono::steady_clock::time_point Deadline, size_t* Dispatched = nullptr);
