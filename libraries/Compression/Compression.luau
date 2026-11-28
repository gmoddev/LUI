--!strict
local Compression = {}
export type Format = "gzip" | "zlib" | "deflate" | "zstd"
export type Completion = (Bytes: buffer?, Failure: string?) -> ()

local function Input(FormatName: Format, Bytes: buffer, MaxOutput: number): buffer
    local FormatId = if FormatName == "gzip" then 0 elseif FormatName == "zlib" then 1 elseif FormatName == "deflate" then 2 elseif FormatName == "zstd" then 3 else -1
    assert(FormatId >= 0 and typeof(Bytes) == "buffer" and buffer.len(Bytes) <= 1048576, "[LUI:Compression] InvalidInput")
    assert(MaxOutput % 1 == 0 and MaxOutput >= 0 and MaxOutput <= 1048576, "[LUI:Compression] InvalidLimit")
    local Data = buffer.create(5 + buffer.len(Bytes))
    buffer.writeu8(Data, 0, FormatId)
    buffer.writeu32(Data, 1, MaxOutput)
    buffer.copy(Data, 5, Bytes)
    return Data
end

function Compression.Encode(FormatName: Format, Bytes: buffer, MaxOutput: number, Complete: Completion): NativeRequest
    assert(type(Complete) == "function", "[LUI:Compression] InvalidCompletion")
    return app:GetService("Compression"):Encode(Input(FormatName, Bytes, MaxOutput), nil, function(Result, _, Failure)
        Complete(Result, Failure)
    end)
end
function Compression.Decode(FormatName: Format, Bytes: buffer, MaxOutput: number, Complete: Completion): NativeRequest
    assert(type(Complete) == "function", "[LUI:Compression] InvalidCompletion")
    return app:GetService("Compression"):Decode(Input(FormatName, Bytes, MaxOutput), nil, function(Result, _, Failure)
        Complete(Result, Failure)
    end)
end
return table.freeze(Compression)
