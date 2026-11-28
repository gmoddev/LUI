--!strict
local Crypto = require("@Crypto/Crypto")
local Encoding = require("@Encoding/Encoding")
local Stdio = require("@Stdio/Stdio")
local Chunks: {string} = {}
local Size = 0
local function Read()
    Stdio.Read(65536, function(Status, Bytes)
        if Status == "Eof" then
            local Digest = Crypto.Sha256(table.concat(Chunks))
            Stdio.Write(string.lower(Encoding.HexEncode(Digest)) .. "\n", function(Result)
                if Result ~= "Ok" then Stdio.SetExitStatus(1) end
            end)
        elseif Status == "Ok" and Size + #Bytes <= 65536 then
            Size += #Bytes
            table.insert(Chunks, Bytes)
            Read()
        else Stdio.SetExitStatus(1) end
    end)
end
Read()
