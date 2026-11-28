--!strict
local Compression = require("@Compression/Compression")
local Stdio = require("@Stdio/Stdio")
local Arguments = Stdio.Arguments()
local Mode = Arguments[1]
local Format = Arguments[2]
if (Mode ~= "encode" and Mode ~= "decode") or (Format ~= "gzip" and Format ~= "deflate" and Format ~= "zlib" and Format ~= "zstd") then
    Stdio.SetExitStatus(2)
    return
end
local FormatName = Format :: Compression.Format
local Parts: {string} = {}
local Size = 0
local function Complete(Result: buffer?, Failure: string?)
    if not Result or Failure then Stdio.SetExitStatus(1); return end
    local Bytes = buffer.tostring(Result)
    local Offset = 1
    local function Write()
        if Offset > #Bytes then return end
        local Chunk = string.sub(Bytes, Offset, Offset + 65535)
        Offset += #Chunk
        Stdio.Write(Chunk, function(Status)
            if Status == "Ok" then Write() else Stdio.SetExitStatus(1) end
        end)
    end
    Write()
end
local function Read()
    Stdio.Read(65536, function(Status, Bytes)
        if Status == "Eof" then
            local Data = buffer.fromstring(table.concat(Parts))
            if Mode == "encode" then Compression.Encode(FormatName, Data, 1048576, Complete)
            else Compression.Decode(FormatName, Data, 1048576, Complete) end
        elseif Status == "Ok" and Size + #Bytes <= 1048576 then
            table.insert(Parts, Bytes)
            Size += #Bytes
            Read()
        else Stdio.SetExitStatus(1) end
    end)
end
Read()
