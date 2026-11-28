--!strict
local Config = require("@Config/Config")
local Defaults: Config.Settings = {Enabled = true, Retries = 2, Name = "default"}
local Result = Config.Decode(' { "Enabled":false,"Retries":0,"Name":"" } ', Defaults)
assert(Result.Enabled == false and Result.Retries == 0 and Result.Name == "")
assert(Defaults.Enabled and Defaults.Retries == 2 and Defaults.Name == "default")
assert(Config.Decode("{}", Defaults) ~= Defaults and table.isfrozen(Result))
for _, Bad in {"[]", "null", '{"X":1}', '{"Name":null}', '{"Enabled":1}', '{"Retries":"2"}',
    '{"Retries":1,"Retries":2}', '{"Name":[]}', '{"Name":{}}', '{"Name":"x"}y'} do
    assert(not pcall(Config.Decode, Bad, Defaults))
end
local Fields: Config.Settings = {}
for Index = 1, 64 do Fields["Field" .. tostring(Index)] = Index end
assert(Config.Decode("{}", Fields).Field64 == 64)
Fields.Field65 = false
assert(not pcall(Config.Decode, "{}", Fields))
assert(not pcall(Config.Decode, "{}", {["bad key"] = false}))
assert(not pcall(Config.Decode, "{}", {Invalid = math.huge}))
assert(not pcall(Config.Decode, "{}", {Name = string.rep("x", 4097)}))
local Canary = "sensitive-private-value"
local Ok, Failure = pcall(Config.Decode, '{"' .. Canary .. '":false}', Defaults)
assert(not Ok and not string.find(tostring(Failure), Canary, 1, true))
