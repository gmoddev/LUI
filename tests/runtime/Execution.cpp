#include "LuiExecution.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void Check(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
struct Probe { LuiRuntime* Runtime; int Busy = 0, Errors = 0; };
void LUI_CALL Log(void* Context, const char* Level, const char*) {
    auto& Value = *static_cast<Probe*>(Context);
    if (std::string(Level) == "Error") ++Value.Errors;
    else Value.Busy = Lui_HasPendingWorkV1(Value.Runtime);
}
}
int main() {
    try {
        Check(Lui_HasPendingWorkV1(nullptr) == -1, "null execution query");
        auto* Runtime = Lui_Create(); Check(Runtime != nullptr, "runtime creation");
        Probe Value{Runtime}; Lui_SetLogCallback(Runtime, &Value, Log);
        Check(Lui_HasPendingWorkV1(Runtime) == 0, "fresh runtime idle");
        std::string Before = Lui_GetLastError(Runtime); int Other = 0;
        std::thread Worker([&] { Other = Lui_HasPendingWorkV1(Runtime); }); Worker.join();
        Check(Other == -1 && Before == Lui_GetLastError(Runtime), "wrong-owner query is nonmutating");
        Check(Lui_RunScript(Runtime, "print('active'); task.defer(function() task.defer(function() print('nested') end) end)", "Execution") == 1,
            "scheduled execution");
        Check(Value.Busy == 1 && Lui_HasPendingWorkV1(Runtime) == 1, "active/scheduled work retained");
        Lui_Pump(Runtime); Check(Lui_HasPendingWorkV1(Runtime) == 1, "nested deferred turn retained");
        Lui_Pump(Runtime); Check(Lui_HasPendingWorkV1(Runtime) == 0, "completed tasks idle");
        Check(Lui_RunScript(Runtime, "task.delay(0.02, function() end)", "Deadline") == 1, "deadline admission");
        Lui_Pump(Runtime); Check(Lui_HasPendingWorkV1(Runtime) == 1, "future deadline keeps host alive");
        const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (Lui_HasPendingWorkV1(Runtime) == 1 && std::chrono::steady_clock::now() < End) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); Lui_Pump(Runtime);
        }
        Check(Lui_HasPendingWorkV1(Runtime) == 0, "deadline completes");
        Lui_Destroy(Runtime);
        std::puts("[LUI:ExecutionTest] PASS"); return 0;
    } catch (const std::exception& Error) { std::fprintf(stderr, "[LUI:ExecutionTest] %s\n", Error.what()); return 1; }
}
