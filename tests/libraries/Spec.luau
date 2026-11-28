--!strict
local Spec = require("@Spec/Spec")
local Trace: {number} = {}
local Saved: Spec.Defer? = nil
local Cases: {Spec.Case} = {
    {Name = "first", Run = function(Defer)
        Saved = Defer
        Defer(function() table.insert(Trace, 1) end)
        Defer(function() table.insert(Trace, 2); error("private detail") end)
        table.insert(Trace, 0)
        error("private detail")
    end},
    {Name = "second", Run = function() table.insert(Trace, 3) end},
}
local Results = Spec.Run(Cases)
assert(#Results == 2 and Results[1].Name == "first" and not Results[1].Passed)
assert(Results[1].BodyFailed and Results[1].CleanupFailed and Results[2].Passed)
assert(table.concat(Trace, ",") == "0,2,1,3")
if Saved then assert(not pcall(Saved, function() end)) else error("missing defer") end
local Calls = 0
local Single = {Name = "case", Run = function() Calls += 1 end}
assert(not pcall(Spec.Run, {Single, Single}) and Calls == 0)
assert(not pcall(Spec.Run, {[2] = Single}) and Calls == 0)
assert(#Spec.Run({}) == 0)
local Boundary = Spec.Run({{Name = "capacity", Run = function(Defer)
    for _ = 1, 32 do Defer(function() Calls += 1 end) end
    Defer(function() error("overflow") end)
end}})
assert(Boundary[1].BodyFailed and not Boundary[1].CleanupFailed and Calls == 32)

local SelectedTrace: {string} = {}
local SelectedCases: {Spec.Case} = {}
for _, Name in {"A.dot", "Anydot", "B.dot"} do
    table.insert(SelectedCases, {Name = Name, Run = function(Defer)
        table.insert(SelectedTrace, Name)
        Defer(function() table.insert(SelectedTrace, "cleanup " .. Name) end)
    end})
end
assert(table.concat(Spec.List(SelectedCases, "."), ",") == "A.dot,B.dot" and #SelectedTrace == 0)
assert(#Spec.List(SelectedCases, "DOT") == 0 and #Spec.Run(SelectedCases, "missing") == 0)
assert(table.concat(Spec.List(SelectedCases), ",") == "A.dot,Anydot,B.dot")
assert(not pcall(Spec.Run, {Single, Single}, "unmatched"))
assert(not pcall(Spec.List, SelectedCases, string.rep("a", 65)))
local Filtered = Spec.Run(SelectedCases, ".")
assert(#Filtered == 2 and Filtered[1].Passed and Filtered[2].Passed)
assert(table.concat(SelectedTrace, ",") == "A.dot,cleanup A.dot,B.dot,cleanup B.dot")

local Invocations = 0
local function Measure()
    Invocations += 1
end
local Bench = Spec.Benchmark(Measure, {Warmup = 2, Samples = 5, Iterations = 3})
assert(Invocations == 21 and Bench.Warmup == 2 and Bench.Samples == 5 and Bench.Iterations == 3)
assert(Bench.MeasuredSeconds >= 0 and Bench.MinSeconds >= 0 and Bench.MinSeconds <= Bench.P50Seconds)
assert(Bench.P50Seconds <= Bench.P95Seconds and Bench.P95Seconds <= Bench.P99Seconds and Bench.P99Seconds <= Bench.MaxSeconds)
local SingleSample = Spec.Benchmark(Measure, {Warmup = 0, Samples = 1})
assert(Invocations == 22 and SingleSample.MinSeconds == SingleSample.MaxSeconds)
assert(not pcall(function() Spec.Benchmark(Measure, {Iterations = 0}) end))
assert(not pcall(function() Spec.Benchmark(Measure, {Samples = 257}) end))
assert(not pcall(function() Spec.Benchmark(Measure, {Warmup = 0/0}) end))
assert(not pcall(function() Spec.Benchmark(Measure, {Ignore = true} :: any) end))
assert(Invocations == 22)
local function FailMeasure(): ()
    error("private detail")
end
local Succeeded, Failure = pcall(function() return Spec.Benchmark(FailMeasure) end)
assert(not Succeeded and tostring(Failure) == "[LUI:Spec] BenchmarkFailed")
