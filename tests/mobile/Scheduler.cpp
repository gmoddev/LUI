#include "../../native/abi/LuiRuntime.h"
#include "../../runtime/internal/State.h"
#include "../../runtime/internal/Scheduler.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

void Check(bool Value) { if (!Value) throw std::runtime_error("[LUI:TaskTest] Assertion"); }
int main() {
    try {
        LuiDiagnosticPolicyV1 Policy{sizeof(Policy), 1, LUI_DIAGNOSTICS_RESTRICTED, 0};
        auto* Runtime = Lui_CreateWithPolicy(&Policy); Check(Runtime);
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants), 1, 0};
        LuiSandboxLimitsV1 Limits{sizeof(Limits), 1, 32 * 1024 * 1024, 10000};
        Check(Lui_DeclareCapabilities(Runtime, &Grants) && Lui_ConfigureSandbox(Runtime, &Limits));
        Check(ConfigureBoundedTasks(Runtime, 4) && !ConfigureBoundedTasks(Runtime, 4));
        Check(Lui_RunScript(Runtime,
            "assert(not pcall(task.delay,31536001,function() end)); "
            "assert(not pcall(task.delay,math.huge,function() end))", "DelayLimit"));
        Check(Lui_RunScript(Runtime,
            "Order=''; task.defer(function() Order..='A'; task.defer(function() Order..='D' end) end); "
            "task.defer(function() Order..='B' end); task.delay(0,function() Order..='C' end); "
            "Future=false; task.delay(100,function() Future=true end); assert(not pcall(task.defer,function() end))", "Bounded"));
        auto Step = [&](size_t Entries) {
            size_t Count = 99;
            Check(PumpBoundedTasks(Runtime, Entries, std::chrono::steady_clock::now() + std::chrono::seconds(1), &Count));
            return Count;
        };
        Check(Lui_Pump(Runtime) == 0);
        Check(Step(1) == 1 && Runtime->TaskRoundActive);
        Check(Lui_RunScript(Runtime, "assert(Order=='A')", "A"));
        Check(Step(2) == 2 && Runtime->TaskRoundActive);
        Check(Lui_RunScript(Runtime, "assert(Order=='ABC')", "ABC"));
        Check(Step(1) == 0 && !Runtime->TaskRoundActive);
        Check(Step(4) == 1);
        Check(Lui_RunScript(Runtime, "assert(Order=='ABCD')", "ABCD"));
        size_t Count = 99;
        Check(PumpBoundedTasks(Runtime, 1, std::chrono::steady_clock::now(), &Count) && Count == 0);
        // Becoming due between slices must not make a future task eligible in
        // the captured round. The next round observes the new monotonic time.
        Check(Lui_RunScript(Runtime, "Cutoff=false; task.delay(100,function() Cutoff=true end)", "Cutoff"));
        Check(PumpBoundedTasks(Runtime, 1, std::chrono::steady_clock::now(), &Count) && Count == 0);
        Runtime->TaskRoundTime -= std::chrono::seconds(1);
        for (auto& Call : Runtime->TaskRound) Call.Due = Runtime->TaskRoundTime + std::chrono::milliseconds(1);
        Check(Step(4) == 0);
        for (auto& Call : Runtime->Tasks) Call.Due = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        Check(Step(4) == 2);
        Check(Lui_RunScript(Runtime, "assert(Cutoff and Future)", "CutoffResult"));
        Check(Lui_RunScript(Runtime, "task.defer(function() end)", "Retirement"));
        Check(PumpBoundedTasks(Runtime, 1, std::chrono::steady_clock::now(), &Count) && Count == 0 && Runtime->TaskRoundActive);
        Lui_Destroy(Runtime); // includes unprocessed round/future references
        // Legacy behavior remains unlimited and whole-round, nested next-round.
        Runtime = Lui_CreateWithPolicy(&Policy); Check(Runtime);
        Check(Lui_RunScript(Runtime, "N=0; for I=1,100 do task.defer(function() N+=1 end) end", "Legacy"));
        Check(Lui_Pump(Runtime) == 100);
        Check(Lui_RunScript(Runtime, "assert(N==100)", "LegacyResult"));
        Lui_Destroy(Runtime);
        std::cout << "[LUI:TaskTest] FIFO, sliced/nested rounds, future cutoff, pending limits, deadline, retirement and legacy pump passed\n";
        return 0;
    } catch (...) { std::cerr << "[LUI:TaskTest] Failed\n"; return 1; }
}
