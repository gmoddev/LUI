--!strict
local Process = require("@Process/Process")
local Stdio = require("@Stdio/Stdio")
local Args = Stdio.Arguments()
Stdio.SetExitStatus(1)
Process.Run({Arguments=Args, TimeoutMilliseconds=10000}, function(Result)
    if Result.Status ~= "Exited" then return end
    Stdio.Write(Result.Stdout, function(Out)
        Stdio.WriteError(Result.Stderr, function(Err)
            if Out == "Ok" and Err == "Ok" then Stdio.SetExitStatus(math.clamp(Result.ExitCode or 1, 0, 125)) end
        end)
    end)
end)
