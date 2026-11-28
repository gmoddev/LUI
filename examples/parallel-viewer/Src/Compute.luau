--!strict
local Workers = require("@Workers/Workers")
return Workers.Handler(function(Seed: number)
    local State = Seed
    for _ = 1, 240000 do
        State = State * 48271 % 2147483647
    end
    return State
end)
