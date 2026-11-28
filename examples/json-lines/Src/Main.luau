--!strict
local Json = require("@Json/Json")
local Stdio = require("@Stdio/Stdio")
local Limits: Json.Limits = {MaxBytes = 65535, MaxDepth = 32, MaxNodes = 8192}
local Line = 0

local function Read()
    Stdio.ReadLine(65536, function(Status, Bytes)
        if Status == "Eof" then return end
        Line += 1
        if Status == "IoError" or Status == "Canceled" then Stdio.SetExitStatus(1); return end
        local function Normalize(): string
            assert(Status == "Ok")
            if string.sub(Bytes, -1) == "\n" then Bytes = string.sub(Bytes, 1, -2) end
            return Json.Encode(Json.Decode(Bytes, Limits), Limits)
        end
        local Ok, Record = pcall(Normalize)
        if not Ok then
            Stdio.SetExitStatus(2)
            Stdio.WriteError(string.format("[LUI:JsonLines] Invalid record at line %d\n", Line), function(Result)
                if Result ~= "Ok" then Stdio.SetExitStatus(1) end
            end)
            return
        end
        Stdio.Write(Record .. "\n", function(Result)
            if Result == "Ok" then Read() else Stdio.SetExitStatus(1) end
        end)
    end)
end
Read()
