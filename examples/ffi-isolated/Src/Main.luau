--!strict
local Broker = require("@FfiBroker/FfiBroker")
local Stdio = require("@Stdio/Stdio")
Stdio.SetExitStatus(1)
Broker.CallBatch({Library={Kind="System", Name="math"}, Symbol="sqrt", Params={"f64"}, Result="f64", Rows={{1764}, {81}}},
    5000, function(Replies, Failure)
        if not Replies then Stdio.WriteError(tostring(Failure) .. "\n", function() end); return end
        assert(Replies[1].Value == 42 and Replies[2].Value == 9)
        Stdio.Write("isolated=42\n", function(Status) if Status == "Ok" then Stdio.SetExitStatus(0) end end)
    end)
