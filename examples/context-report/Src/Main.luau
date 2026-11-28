--!strict
local Context = require("@Context/Context")
local Stdio = require("@Stdio/Stdio")
local Name = Context.Get("LUI_PROJECT_NAME") or "unnamed"
assert(Context.Elapsed() >= 0)
Stdio.Write(Context.Platform() .. "\t" .. Name .. "\n", function(Status)
    Stdio.SetExitStatus(if Status == "Ok" then 0 else 1)
end)
