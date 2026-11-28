--!strict
local Workers = require("@Workers/Workers")
local Stdio = require("@Stdio/Stdio")
local Count, Sum = 0, 0
Stdio.SetExitStatus(1)
local Pool = Workers.Pool("./Compute", 2)
for Part = 1, 2 do
    local Job = Pool:Send({First=(Part-1)*100000+1,Last=Part*100000},function(Result, Why)
        if Why then Pool:Close(); return end
        Count += 1
        Sum = (Sum + Result) % 1000000007
        if Count == 2 then
            Pool:Close()
            Stdio.Write(tostring(Sum).."\n",function(Status)
                if Status=="Ok" then Stdio.SetExitStatus(0) end
            end)
        end
    end)
    if not Job then Pool:Close(); break end
end
