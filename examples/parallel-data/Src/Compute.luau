--!strict
local Workers = require("@Workers/Workers")
return Workers.Handler(function(Range: any)
    local Sum = 0
    for I = Range.First, Range.Last do Sum = (Sum + I % 65521) % 1000000007 end
    return Sum
end)
