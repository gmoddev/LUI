--!strict
local Process = require("@Process/Process")
local Stdio = require("@Stdio/Stdio")

Stdio.SetExitStatus(1)
Process.Pipeline({{Arguments={"ls-files", "--cached", "-z"}}, {Arguments={"hash-object", "--stdin"}}},
    {TimeoutMilliseconds=10000, MaxOutputBytes=131072}, function(Result)
        if Result.Stderr ~= "" then
            Stdio.WriteError(string.sub(Result.Stderr, 1, 65536), function() end)
            return
        end
        if Result.Status ~= "Exited" or Result.ExitCode ~= 0 then
            Stdio.WriteError("[LUI:ToolPipeline] " .. Result.Status .. "\n", function() end)
            return
        end
        Stdio.Write(Result.Stdout, function(Status)
            if Status == "Ok" then Stdio.SetExitStatus(0) end
        end)
    end)
