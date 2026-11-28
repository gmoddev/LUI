--!strict
local Workers = require("@Workers/Workers")
return Workers.Handler(function(Source: string)
    local Lines, Todos = 1, 0
    local Marked = false
    for Index = 1, #Source do
        local Byte = string.byte(Source, Index)
        if Byte == 10 then
            Lines += 1
            Marked = false
        elseif not Marked and Byte == 84 and string.sub(Source, Index, Index + 3) == "TODO" then
            Todos += 1
            Marked = true
        end
    end
    return {Lines = Lines, Todos = Todos}
end)
