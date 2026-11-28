--!strict
local Json = require("@Json/Json")
local Config = require("@Config/Config")
local Toml = {}
local Limits: Json.Limits = {MaxBytes = 65536, MaxDepth = 16, MaxNodes = 4096}

function Toml.Decode(Document: string): {[string]: any}
    return Json.Decode(app:GetService("Toml"):Decode(Document), Limits)
end

function Toml.Settings(Document: string, Defaults: Config.Settings): Config.Settings
    return Config.Decode(app:GetService("Toml"):Decode(Document), Defaults)
end

function Toml.Encode(Value: {[string]: any}): string
    local Encoded = Json.Encode(Value, {MaxBytes = 32000, MaxDepth = 15, MaxNodes = 2048})
    assert(string.sub(Encoded, 1, 1) == "{", "[LUI:Toml] TableRequired")
    -- JSON strings from this encoder use quoted UTF-8 and TOML-compatible escapes.
    -- Outside those strings a TOML inline table uses equals between key and value.
    local Parts = {"root = "}
    local Quoted, Escaped, First = false, false, 1
    local Index = 1
    while Index <= #Encoded do
        local Byte = string.byte(Encoded, Index)
        if Quoted then
            if Escaped then Escaped = false
            elseif Byte == 92 then Escaped = true
            elseif Byte == 34 then Quoted = false
            elseif Byte == 127 then
                table.insert(Parts, string.sub(Encoded, First, Index - 1))
                table.insert(Parts, "\\u007f"); First = Index + 1
            end
        elseif Byte == 34 then Quoted = true
        elseif Byte == 45 or (Byte >= 48 and Byte <= 57) then
            local End = string.find(Encoded, "[^%d%.eE%+%-]", Index) or (#Encoded + 1)
            local Token = string.sub(Encoded, Index, End - 1)
            if not string.find(Token, "[%.eE]") and (Token == "-0" or math.abs(assert(tonumber(Token))) > 9007199254740991) then
                table.insert(Parts, string.sub(Encoded, First, End - 1))
                table.insert(Parts, ".0"); First = End
            end
            Index = End - 1
        elseif Byte == 58 then
            table.insert(Parts, string.sub(Encoded, First, Index - 1))
            table.insert(Parts, "="); First = Index + 1
        end
        Index += 1
    end
    table.insert(Parts, string.sub(Encoded, First))
    return app:GetService("Toml"):Format(table.concat(Parts))
end

return table.freeze(Toml)
