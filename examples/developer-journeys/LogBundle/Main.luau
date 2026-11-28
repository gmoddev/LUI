--!strict
local Compression = require("@Compression/Compression")
local Regex = require("@Regex/Regex")
local Stdio = require("@Stdio/Stdio")

local Args = Stdio.Arguments()
local function Fail(Message: string, Code: number)
    Stdio.SetExitStatus(Code)
    Stdio.WriteError("[LogBundle:Input] " .. Message .. "\n", function() end)
end
if #Args ~= 1 then Fail("Pass one regular expression.", 2); return end
local Valid, Pattern = pcall(Regex.Compile, Args[1])
if not Valid then Fail("The regular expression is invalid.", 2); return end
local Lines: {string} = {}
local Bytes = 0
local function Compress()
    Pattern:Close()
    Compression.Encode("gzip", buffer.fromstring(table.concat(Lines)), 65536, function(Data, Failure)
        if not Data or Failure then Fail("Compression failed or exceeded 64 KiB.", 1); return end
        Stdio.Write(buffer.tostring(Data), function(Status)
            if Status ~= "Ok" then Stdio.SetExitStatus(1) end
        end)
    end)
end
local function Read()
    Stdio.ReadLine(4096, function(Status, Line)
        if Status == "Eof" then Compress(); return end
        Bytes += #Line
        if Status ~= "Ok" or Bytes > 49152 then
            Pattern:Close()
            Fail("Input exceeds 48 KiB or contains an unreadable line.", 1)
            return
        end
        if Pattern:Find(Line) then table.insert(Lines, Line) end
        Read()
    end)
end
Read()
