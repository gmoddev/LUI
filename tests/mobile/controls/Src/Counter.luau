local Count = 0
local Counter = {}
function Counter.Next(): number
    Count += 1
    return Count
end
return Counter
