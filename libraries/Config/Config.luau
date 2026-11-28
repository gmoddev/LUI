--!strict
local Json = require("@Json/Json")
local Config = {Version = "0.1.0", RequiredCapabilities = table.freeze({})}

export type Scalar = boolean | number | string
export type Settings = {[string]: Scalar}

local function Valid(Value: any): boolean
    if type(Value) == "boolean" then return true end
    if type(Value) == "number" then return Value == Value and math.abs(Value) < math.huge end
    return type(Value) == "string" and #Value <= 4096 and utf8.len(Value) ~= nil
end

function Config.Decode(Text: string, Defaults: Settings): Settings
    if type(Text) ~= "string" or #Text > 32768 then error("[LUI:Config] InvalidInput", 0) end
    if type(Defaults) ~= "table" or getmetatable(Defaults) ~= nil then error("[LUI:Config] InvalidDefaults", 0) end
    local Result: Settings = {}
    local Count = 0
    for Name, Value in Defaults do
        Count += 1
        if Count > 64 or type(Name) ~= "string" or #Name > 64 or not string.match(Name, "^[A-Za-z_][A-Za-z0-9_]*$")
            or not Valid(Value) then error("[LUI:Config] InvalidDefaults", 0) end
        Result[Name] = Value
    end
    if not string.match(Text, "^[ \t\r\n]*{") then error("[LUI:Config] ObjectRequired", 0) end
    local Values = Json.Decode(Text, {MaxBytes = 32768, MaxDepth = 2, MaxNodes = 129})
    for Name, Value in Values do
        if Result[Name] == nil then error("[LUI:Config] UnknownSetting", 0) end
        if type(Value) ~= type(Result[Name]) or not Valid(Value) then error("[LUI:Config] InvalidSetting", 0) end
        Result[Name] = Value
    end
    return table.freeze(Result)
end

return table.freeze(Config)
