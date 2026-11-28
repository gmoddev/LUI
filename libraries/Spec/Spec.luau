--!strict
local Spec = {Version = "0.1.0", RequiredCapabilities = table.freeze({})}

export type Defer = (Cleanup: () -> ()) -> ()
export type Case = {Name: string, Run: (Defer) -> ()}
export type Result = {Name: string, Passed: boolean, CleanupFailed: boolean, BodyFailed: boolean}
export type BenchmarkOptions = {Warmup: number?, Samples: number?, Iterations: number?}
export type BenchmarkResult = {
    Warmup: number, Samples: number, Iterations: number, MeasuredSeconds: number,
    MinSeconds: number, P50Seconds: number, P95Seconds: number, P99Seconds: number, MaxSeconds: number,
}

local function Select(Cases: {Case}, Filter: string?): {Case}
    if Filter~=nil and (type(Filter)~="string" or #Filter>64) then error("[LUI:Spec] InvalidFilter",0) end
    if type(Cases) ~= "table" or getmetatable(Cases) ~= nil then error("[LUI:Spec] InvalidCases", 0) end
    local Count, Last = 0, 0
    local Plan: {Case} = {}
    local Seen: {[string]: boolean} = {}
    for Index, Case in Cases do
        Count += 1
        if Count > 128 or type(Index) ~= "number" or Index % 1 ~= 0 or Index < 1 or Index > 128 then
            error("[LUI:Spec] InvalidCases", 0)
        end
        if type(Case) ~= "table" or getmetatable(Case) ~= nil or type(Case.Run) ~= "function"
            or type(Case.Name) ~= "string" or #Case.Name > 64 or not string.match(Case.Name, "^[A-Za-z][A-Za-z0-9_. %-]*$")
            or Seen[Case.Name] then error("[LUI:Spec] InvalidCase", 0) end
        Last = math.max(Last, Index)
        Seen[Case.Name] = true
        Plan[Index] = {Name = Case.Name, Run = Case.Run}
    end
    if Count ~= Last then error("[LUI:Spec] InvalidCases", 0) end
    if Filter==nil or Filter=="" then return Plan end
    local Selected: {Case} = {}
    for _,Case in Plan do
        if string.find(Case.Name,Filter,1,true) then table.insert(Selected,Case) end
    end
    return Selected
end
function Spec.List(Cases: {Case}, Filter: string?): {string}
    local Names: {string} = {}
    for _,Case in Select(Cases,Filter) do table.insert(Names,Case.Name) end
    return Names
end
function Spec.Run(Cases: {Case}, Filter: string?): {Result}
    local Plan=Select(Cases,Filter)
    local Results: {Result} = {}
    for _, Case in Plan do
        local Cleanups: {() -> ()} = {}
        local Open = true
        local function Defer(Cleanup: () -> ())
            if not Open or type(Cleanup) ~= "function" or #Cleanups >= 32 then error("[LUI:Spec] CannotRegister", 0) end
            table.insert(Cleanups, Cleanup)
        end
        local BodyOk = pcall(Case.Run, Defer)
        Open = false
        local CleanupFailed = false
        for Index = #Cleanups, 1, -1 do
            if not pcall(Cleanups[Index]) then CleanupFailed = true end
        end
        table.clear(Cleanups)
        table.insert(Results, {Name = Case.Name, Passed = BodyOk and not CleanupFailed,
            BodyFailed = not BodyOk, CleanupFailed = CleanupFailed})
    end
    return Results
end

local function BenchmarkCount(Value: number?, Default: number, Minimum: number, Maximum: number): number
    if Value == nil then return Default end
    if type(Value) ~= "number" or Value % 1 ~= 0 or Value < Minimum or Value > Maximum then
        error("[LUI:Spec] InvalidBenchmark", 0)
    end
    return Value
end

function Spec.Benchmark(Measure: () -> (), Options: BenchmarkOptions?): BenchmarkResult
    if type(Measure) ~= "function" or (Options ~= nil and (type(Options) ~= "table" or getmetatable(Options) ~= nil)) then
        error("[LUI:Spec] InvalidBenchmark", 0)
    end
    local Plan: BenchmarkOptions = Options or {}
    for Key in Plan :: any do
        if Key ~= "Warmup" and Key ~= "Samples" and Key ~= "Iterations" then
            error("[LUI:Spec] InvalidBenchmark", 0)
        end
    end
    local Warmup = BenchmarkCount(Plan.Warmup, 3, 0, 100)
    local Samples = BenchmarkCount(Plan.Samples, 25, 1, 256)
    local Iterations = BenchmarkCount(Plan.Iterations, 1, 1, 1000)
    local Timings: {number} = table.create(Samples, 0)
    local MeasuredSeconds = 0
    local Succeeded = pcall(function()
        for _ = 1, Warmup do
            for _ = 1, Iterations do Measure() end
        end
        for Index = 1, Samples do
            local Started = os.clock()
            for _ = 1, Iterations do Measure() end
            local Elapsed = os.clock() - Started
            Timings[Index] = Elapsed / Iterations
            MeasuredSeconds += Elapsed
        end
    end)
    if not Succeeded then error("[LUI:Spec] BenchmarkFailed", 0) end
    table.sort(Timings)
    return {
        Warmup = Warmup, Samples = Samples, Iterations = Iterations, MeasuredSeconds = MeasuredSeconds,
        MinSeconds = Timings[1], P50Seconds = Timings[math.ceil(Samples * 0.50)],
        P95Seconds = Timings[math.ceil(Samples * 0.95)], P99Seconds = Timings[math.ceil(Samples * 0.99)],
        MaxSeconds = Timings[Samples],
    }
end

return table.freeze(Spec)
