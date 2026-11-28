--!strict
local Args = require("@Args/Args")
local Spec: Args.Options = {verbose="Flag",output="Value"}
local P = Args.Parse({"one","--verbose","--output=--literal","--","--two",""}, Spec)
assert(P.Options.verbose and P.Options.output == "--literal" and #P.Positionals == 3 and P.Positionals[2] == "--two")
assert(Args.Parse({"--output="},Spec).Options.output == "")
for _, Input in {{"--bad"},{"--output"},{"--verbose","--verbose"},{"--verbose=1"}} do
    assert(not pcall(Args.Parse, Input, Spec))
end
assert(not pcall(Args.Parse, {[2]="late"}, Spec))
assert(Args.Parse({"--output", "dest"},Spec).Options.output == "dest")
assert(#Args.Parse(table.create(64, "x"),Spec).Positionals == 64)
assert(not pcall(Args.Parse,table.create(65,"x"),Spec))
assert(#Args.Parse(table.create(8,string.rep("a",4096)),Spec).Positionals == 8)
assert(not pcall(Args.Parse,table.create(9,string.rep("a",4096)),Spec))
assert(not pcall(Args.Parse,setmetatable({}, {__metatable=false}),Spec))
