# Spec

Run an application-level suite in a normal LUI project. Use ordinary Luau assertions. Each case receives a function that registers cleanup before work begins.

~~~luau
local Spec = require("@Spec/Spec")
local Results = Spec.Run({{
    Name = "reset state",
    Run = function(Defer)
        local State = {Ready = true}
        Defer(function() State.Ready = false end)
        assert(State.Ready)
    end,
}})
assert(Results[1].Passed)
~~~

`Run(Cases)` accepts a dense array with up to 128 distinct named cases. A name is a 1–64 byte ASCII label starting with a letter and containing letters, digits, spaces, hyphens, dots or underscores. All names and functions are validated and captured before the first case executes. Cases run in order. Each may register up to 32 cleanup functions. Cleanup runs in reverse registration order after success or failure. Cleanup failure does not skip the remaining callbacks or cases.

The result array contains one record per case with `Name`, `Passed`, `BodyFailed` and `CleanupFailed`. Case failures produce results. Invalid suite declarations raise fixed `[LUI:Spec]` errors. Assert the results in your entry point to obtain a failing headless exit status. Error objects and source values are never printed by the runner. The returned records belong to the caller.

Cases and cleanup are intended to run synchronously. A VM-wide interruption, shutdown or process failure can preempt cleanup. The library imposes no separate timeout, grants no capabilities and starts no tasks. Browser and mobile execution have separate qualification.

`Spec.List(Cases, Filter?)` validates all declared test cases and returns the matching names in declaration order without invoking their functions. `Spec.Run(Cases, Filter?)` executes the same selection with the existing LIFO cleanup. Filter is a case-sensitive literal substring up to 64 bytes. Use the `spec-cli` SDK example for a list, a filtered run or a timed repeat.

## Benchmark an operation

```luau
local Report = Spec.Benchmark(function()
    assert(BuildIndex(Records) ~= nil)
end, {Warmup = 5, Samples = 100, Iterations = 10})
print("[Index:Benchmark]", Report.P95Seconds * 1000)
```

`Spec.Benchmark(Measure, Options?)` runs a synchronous callback and measures
it with `os.clock()`. Defaults are three warmup batches, 25 measured batches
and one invocation per batch. Valid ranges are 0–100 warmups, 1–256 samples
and 1–1,000 invocations per batch.

The result includes the three counts, measured batch time in
`MeasuredSeconds`, and minimum, p50, p95, p99 and maximum seconds per
invocation. Each percentile uses the nearest rank among the per-batch
averages. Use one invocation per batch for individual operation latency.
Use larger batches when individual calls approach the timer's resolution.
Run setup outside the callback to measure the operation alone. For
asynchronous operations, measure from admission through completion in
the application. The benchmark starts no tasks and uses the normal VM
execution budget. A failed callback raises `[LUI:Spec] BenchmarkFailed`.
