--!strict
local Process = require("@Process/Process")
local Stdio = require("@Stdio/Stdio")

Stdio.SetExitStatus(1)
local Session: Process.Session? = nil
local Reading: number? = nil
local Finished, Failed = false, false
local Reply = ""

local function ReadQuery()
    if Finished then return end
    Reading = Stdio.ReadLine(4096, function(Status, Line)
        Reading = nil
        if Finished then return end
        local Child = assert(Session)
        if Status == "Eof" then
            Child:CloseInput()
            return
        end
        local Query = string.gsub(Line, "[\r\n]+$", "")
        if Status ~= "Ok" or #Query == 0 or #Query > 4095 or string.find(Query, string.char(0), 1, true) then
            Failed = true
            Child:Kill()
            return
        end
        if not Child:Write(Query .. "\n") then
            Failed = true
            Child:Kill()
        end
    end)
end

Session = Process.Open({Arguments={"cat-file", "--batch-check"}, TimeoutMilliseconds=30000,
    OnOutput=function(Stream, Bytes)
        if Stream ~= "stdout" then return end
        Reply ..= Bytes
        if string.sub(Reply, -1) ~= "\n" then return end
        local Line = Reply
        Reply = ""
        Stdio.Write(Line, function(Status)
            if Status == "Ok" then ReadQuery() else
                Failed = true
                assert(Session):Kill()
            end
        end)
    end}, function(Result)
        Finished = true
        local Pending = Reading
        if Pending then Stdio.Cancel(Pending) end
        if Failed then
            Stdio.WriteError("[LUI:ToolSession] Query or stream failed\n", function() end)
        elseif Result.Status == "Exited" and Result.ExitCode == 0 and Reply == "" then
            Stdio.SetExitStatus(0)
        else
            Stdio.WriteError(if Result.Stderr ~= "" then Result.Stderr else "[LUI:ToolSession] " .. Result.Status .. "\n", function() end)
        end
    end)
ReadQuery()
