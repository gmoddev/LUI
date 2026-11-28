--!strict
local Encoding = {Version = "0.1.0", RequiredCapabilities = table.freeze({})}
local MaxBytes = 65536
local Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local Digits = "0123456789ABCDEF"
local Decode: {[number]: number} = {}
for Index = 1, #Alphabet do
    Decode[string.byte(Alphabet, Index)] = Index - 1
end

local function Check(Text: string, Limit: number)
    if type(Text) ~= "string" then error("[LUI:Encoding] StringRequired", 0) end
    if #Text > Limit then error("[LUI:Encoding] LimitExceeded", 0) end
end

local function HexDigit(Byte: number): number
    if Byte >= 48 and Byte <= 57 then return Byte - 48 end
    if Byte >= 65 and Byte <= 70 then return Byte - 55 end
    if Byte >= 97 and Byte <= 102 then return Byte - 87 end
    error("[LUI:Encoding] InvalidHex", 0)
end

local function HexPair(Byte: number): string
    local High, Low = math.floor(Byte / 16) + 1, Byte % 16 + 1
    return string.sub(Digits, High, High) .. string.sub(Digits, Low, Low)
end

function Encoding.HexEncode(Bytes: string): string
    Check(Bytes, MaxBytes)
    return (string.gsub(Bytes, ".", function(Char) return HexPair(string.byte(Char)) end))
end

function Encoding.HexDecode(Text: string): string
    Check(Text, MaxBytes * 2)
    if #Text % 2 ~= 0 then error("[LUI:Encoding] InvalidHex", 0) end
    local Out = buffer.create(#Text / 2)
    for Index = 1, #Text, 2 do
        local High, Low = string.byte(Text, Index, Index + 1)
        buffer.writeu8(Out, (Index - 1) / 2, HexDigit(High) * 16 + HexDigit(Low))
    end
    return buffer.tostring(Out)
end

function Encoding.Base64Encode(Bytes: string): string
    Check(Bytes, MaxBytes)
    local Out = buffer.create(math.ceil(#Bytes / 3) * 4)
    local Offset = 0
    for Index = 1, #Bytes, 3 do
        local A, B, C = string.byte(Bytes, Index, Index + 2)
        local Tail = B or 0
        buffer.writeu8(Out, Offset, (string.byte(Alphabet, math.floor(A / 4) + 1)))
        buffer.writeu8(Out, Offset + 1, (string.byte(Alphabet, A % 4 * 16 + math.floor(Tail / 16) + 1)))
        buffer.writeu8(Out, Offset + 2, if B then string.byte(Alphabet, Tail % 16 * 4 + math.floor((C or 0) / 64) + 1) else 61)
        buffer.writeu8(Out, Offset + 3, if C then string.byte(Alphabet, C % 64 + 1) else 61)
        Offset += 4
    end
    return buffer.tostring(Out)
end

function Encoding.Base64Decode(Text: string): string
    Check(Text, math.ceil(MaxBytes / 3) * 4)
    if #Text % 4 ~= 0 then error("[LUI:Encoding] InvalidBase64", 0) end
    local Padding = if string.sub(Text, -2) == "==" then 2 elseif string.sub(Text, -1) == "=" then 1 else 0
    local Size = #Text / 4 * 3 - Padding
    if Size > MaxBytes then error("[LUI:Encoding] LimitExceeded", 0) end
    local Out = buffer.create(Size)
    local Offset = 0
    for Index = 1, #Text, 4 do
        local A, B, C, D = string.byte(Text, Index, Index + 3)
        local X, Y, Z, W = Decode[A], Decode[B], Decode[C], Decode[D]
        local Last = Index + 3 == #Text
        local ThirdPad = Last and Padding == 2
        local FourthPad = Last and Padding > 0
        if not X or not Y or (not Z and not ThirdPad) or (not W and not FourthPad)
            or (ThirdPad and Y % 16 ~= 0) or (FourthPad and not ThirdPad and Z % 4 ~= 0) then
            error("[LUI:Encoding] InvalidBase64", 0)
        end
        buffer.writeu8(Out, Offset, X * 4 + math.floor(Y / 16))
        if not ThirdPad then buffer.writeu8(Out, Offset + 1, Y % 16 * 16 + math.floor(Z / 4)) end
        if not FourthPad then buffer.writeu8(Out, Offset + 2, Z % 4 * 64 + W) end
        Offset += 3
    end
    return buffer.tostring(Out)
end

function Encoding.UrlEncode(Bytes: string): string
    Check(Bytes, MaxBytes)
    return (string.gsub(Bytes, "[^A-Za-z0-9._~%-]", function(Char) return "%" .. HexPair(string.byte(Char)) end))
end

function Encoding.UrlDecode(Text: string): string
    Check(Text, MaxBytes * 3)
    local Out = buffer.create(math.min(#Text, MaxBytes))
    local Index, Offset = 1, 0
    while Index <= #Text do
        if Offset == MaxBytes then error("[LUI:Encoding] LimitExceeded", 0) end
        local Byte = string.byte(Text, Index)
        if Byte == 37 then
            if Index + 2 > #Text then error("[LUI:Encoding] InvalidEscape", 0) end
            local High, Low = string.byte(Text, Index + 1, Index + 2)
            Byte = HexDigit(High) * 16 + HexDigit(Low)
            Index += 2
        end
        buffer.writeu8(Out, Offset, Byte)
        Offset += 1
        Index += 1
    end
    return buffer.readstring(Out, 0, Offset)
end

return table.freeze(Encoding)
