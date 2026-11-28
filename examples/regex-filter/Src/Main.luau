--!strict
local Regex = require("@Regex/Regex")
local Stdio = require("@Stdio/Stdio")
local Args = Stdio.Arguments()
Stdio.SetExitStatus(2)
if #Args ~= 1 then return end
local Pattern = Regex.Compile(Args[1])
local function Read()
    Stdio.ReadLine(4096, function(Status, Line)
        if Status == "Eof" then Pattern:Close(); Stdio.SetExitStatus(0); return end
        if Status ~= "Ok" then Pattern:Close(); return end
        if Pattern:Find(Line) then
            Stdio.Write(Line, function(Written) if Written == "Ok" then Read() else Pattern:Close() end end)
        else Read() end
    end)
end
Read()
