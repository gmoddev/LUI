#pragma once

struct LuiRuntime;
struct lua_State;

// The owner thread resumes a scheduler-owned coroutine. Arguments must already
// be on its stack. A yielded task retains its registry reference only when an
// async operation has taken responsibility for completing it.
void ResumeScheduledTask(LuiRuntime* Runtime, int Reference, int ArgumentCount, bool AsError = false);
