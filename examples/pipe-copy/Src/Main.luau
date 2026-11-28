--!strict
local Stdio = require("@Stdio/Stdio")

local function Next()
    Stdio.Read(32768, function(Status, Bytes)
        if Status == "Eof" then return end
        if Status ~= "Ok" then Stdio.SetExitStatus(1); return end
        Stdio.Write(Bytes, function(Written)
            if Written == "Ok" then Next() else Stdio.SetExitStatus(1) end
        end)
    end)
end
Next()
