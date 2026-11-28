--!strict
local Args = require("@Args/Args")
local Spec = require("@Spec/Spec")
local Random = require("@Random/Random")
local Stdio = require("@Stdio/Stdio")
local Json = require("@Json/Json")

local Cases: {Spec.Case} = {
    {Name = "seeded sequence", Run = function()
        local A, B = Random.New(42), Random.New(42)
        for _ = 1, 1000 do assert(A:Integer(-50, 50) == B:Integer(-50, 50)) end
    end},
    {Name = "integer range", Run = function()
        local A = Random.New(17)
        for _ = 1, 1000 do
            local Value = A:Integer(2, 9)
            assert(Value >= 2 and Value <= 9)
        end
    end},
}

Stdio.SetExitStatus(2)
local Parsed, Arguments = pcall(Args.Parse, Stdio.Arguments(), {list = "Flag", filter = "Value", ["repeat"] = "Value"})
local function Usage()
    Stdio.WriteError("[LUI:Spec] Usage: spec-cli [--list] [--filter TEXT] [--repeat 1..100]\n", function() end)
end
if not Parsed or #Arguments.Positionals > 0 then Usage(); return end
local Filter = Arguments.Options.filter
local Repetitions = Arguments.Options["repeat"] or "1"
if (Filter ~= nil and (type(Filter) ~= "string" or #Filter > 64)) or type(Repetitions) ~= "string"
    or not string.match(Repetitions, "^%d+$") then Usage(); return end
local Count = tonumber(Repetitions)
if not Count or Count < 1 or Count > 100 then Usage(); return end

local Names = Spec.List(Cases, Filter)
local Started = app:GetService("PerformanceService"):GetTime()
local Runs, Passed = 0, true
local function WriteReport()
    local Elapsed = app:GetService("PerformanceService"):GetTime() - Started
    local Report = {Cases = Json.Array(Names), Runs = Runs, Passed = Passed, Milliseconds = Elapsed}
    Stdio.Write(Json.Encode(Report) .. "\n", function(Status)
        if Status == "Ok" then Stdio.SetExitStatus(if Passed then 0 else 1) end
    end)
end
if Arguments.Options.list then WriteReport(); return end
if #Names == 0 then
    Stdio.WriteError("[LUI:Spec] No cases match the filter\n", function() end)
    return
end

local Remaining: number = Count
local function RunOnce()
    for _, Result in Spec.Run(Cases, Filter) do
        Runs += 1
        Passed = Passed and Result.Passed
    end
    Remaining -= 1
    if Remaining > 0 then task.defer(RunOnce) else WriteReport() end
end
RunOnce()
