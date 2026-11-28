--!strict
local Digest = require("@DigestBindings/Main")
local Stdio = require("@Stdio/Stdio")
local Chunks: {string} = {}
local Size = 0
local function Read()
    Stdio.Read(16384, function(Status, Value)
        if Status == "Eof" then
            Stdio.Write(Digest.Hex(table.concat(Chunks)) .. "\n", function(Written)
                if Written ~= "Ok" then Stdio.SetExitStatus(1) end
            end)
        elseif Status == "Ok" and Size + #Value <= 65536 then
            Size += #Value
            table.insert(Chunks, Value)
            Read()
        else
            Stdio.SetExitStatus(1)
        end
    end)
end
Read()
