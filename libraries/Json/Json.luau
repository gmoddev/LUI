--!strict
-- Optional, pure Luau JSON codec. No runtime registration or initialization IO.
export type Limits = { MaxBytes: number?, MaxDepth: number?, MaxNodes: number? }
local Json = {}
local ArrayMeta = table.freeze({})
local ObjectMeta = table.freeze({})
local Null = table.freeze({})
Json.Null = Null
Json.Version = "0.1.0"
Json.RequiredCapabilities = table.freeze({})

local function Fail(Code: string): never
    error("[LUI:Json] " .. Code, 0)
end

local function Bounds(Options: Limits?): (number, number, number)
    local function Limit(Value: number?, Ceiling: number): number
        if Value == nil then return Ceiling end
        if type(Value) ~= "number" or Value ~= Value or Value % 1 ~= 0 or Value < 1 or Value > Ceiling then
            Fail("InvalidOptions")
        end
        return Value
    end
    if Options ~= nil then
        if type(Options) ~= "table" or getmetatable(Options) ~= nil then Fail("InvalidOptions") end
        for Key in Options :: any do
            if Key ~= "MaxBytes" and Key ~= "MaxDepth" and Key ~= "MaxNodes" then Fail("InvalidOptions") end
        end
    end
    return Limit(if Options then Options.MaxBytes else nil, 1048576),
        Limit(if Options then Options.MaxDepth else nil, 64),
        Limit(if Options then Options.MaxNodes else nil, 100000)
end

local function ValidUtf8(Text: string, Checkpoint: (() -> ())?)
    local Index, Length = 1, #Text
    local NextCheckpoint = 256
    while Index <= Length do
        if Checkpoint and Index >= NextCheckpoint then Checkpoint(); NextCheckpoint = Index + 256 end
        local First = string.byte(Text, Index)
        local Count, Scalar, Minimum = 0, 0, 0
        if First < 128 then Index += 1; continue
        elseif First >= 194 and First <= 223 then Count, Scalar, Minimum = 1, First - 192, 128
        elseif First >= 224 and First <= 239 then Count, Scalar, Minimum = 2, First - 224, 2048
        elseif First >= 240 and First <= 244 then Count, Scalar, Minimum = 3, First - 240, 65536
        else Fail("InvalidUtf8") end
        if Index + Count > Length then Fail("InvalidUtf8") end
        for Offset = 1, Count do
            local Byte = string.byte(Text, Index + Offset)
            if Byte < 128 or Byte > 191 then Fail("InvalidUtf8") end
            Scalar = Scalar * 64 + Byte - 128
        end
        if Scalar < Minimum or Scalar > 1114111 or (Scalar >= 55296 and Scalar <= 57343) then Fail("InvalidUtf8") end
        Index += Count + 1
    end
end

function Json.Array(Values: {any}?): {any}
    if Values ~= nil and (type(Values) ~= "table" or getmetatable(Values) ~= nil) then Fail("InvalidValue") end
    local Result = if Values == nil then {} else table.clone(Values)
    return setmetatable(Result, ArrayMeta)
end

function Json.Object(Values: {[string]: any}?): {[string]: any}
    if Values ~= nil and (type(Values) ~= "table" or getmetatable(Values) ~= nil) then Fail("InvalidValue") end
    local Result = if Values == nil then {} else table.clone(Values)
    return setmetatable(Result, ObjectMeta)
end

function Json.Encode(Input: any, Options: Limits?): string
    local MaxBytes, MaxDepth, MaxNodes = Bounds(Options)
    local Parts: {string} = {}
    local Bytes, Nodes = 0, 0
    local Active: {[any]: boolean} = {}
    local function Append(Text: string)
        if #Text > MaxBytes - Bytes then Fail("LimitExceeded") end
        Bytes += #Text; table.insert(Parts, Text)
    end
    local function Node()
        Nodes += 1; if Nodes > MaxNodes then Fail("LimitExceeded") end
    end
    local function Quote(Text: string)
        if #Text > MaxBytes then Fail("LimitExceeded") end
        ValidUtf8(Text); Append('"')
        local Start = 1
        for Index = 1, #Text do
            local Byte = string.byte(Text, Index)
            if Byte < 32 or Byte == 34 or Byte == 92 then
                Append(string.sub(Text, Start, Index - 1))
                if Byte == 34 then Append('\\"')
                elseif Byte == 92 then Append('\\\\')
                else Append(string.format("\\u%04x", Byte)) end
                Start = Index + 1
            end
        end
        Append(string.sub(Text, Start)); Append('"')
    end
    local function Value(Item: any, Depth: number)
        Node(); if Depth > MaxDepth then Fail("LimitExceeded") end
        if Item == Null then Append("null")
        elseif type(Item) == "boolean" then Append(if Item then "true" else "false")
        elseif type(Item) == "string" then Quote(Item)
        elseif type(Item) == "number" then
            if Item ~= Item or Item == math.huge or Item == -math.huge then Fail("InvalidValue") end
            Append(string.format("%.17g", Item))
        elseif type(Item) == "table" then
            local Meta = getmetatable(Item)
            if Meta ~= nil and Meta ~= ArrayMeta and Meta ~= ObjectMeta then Fail("InvalidValue") end
            if Active[Item] then Fail("Cycle") end
            Active[Item] = true
            local Count, Largest, HasNumber, HasString = 0, 0, false, false
            for Key in next, Item do
                Count += 1
                if Count > MaxNodes - Nodes then Fail("LimitExceeded") end
                if type(Key) == "number" and Key % 1 == 0 and Key >= 1 and Key <= MaxNodes then
                    HasNumber = true; Largest = math.max(Largest, Key)
                elseif type(Key) == "string" then HasString = true
                else Fail("InvalidValue") end
            end
            local IsArray = Meta == ArrayMeta or (Meta == nil and HasNumber)
            if IsArray then
                if HasString or Largest ~= Count then Fail("InvalidValue") end
                Append("[")
                for Index = 1, Count do
                    if Index > 1 then Append(",") end
                    Value(rawget(Item, Index), Depth + 1)
                end
                Append("]")
            else
                if HasNumber then Fail("InvalidValue") end
                Append("{")
                -- Sorted object keys give stable output without exposing order
                -- assumptions to callers or running table metamethods.
                local Keys: {string} = {}
                for Key in next, Item do table.insert(Keys, Key :: string) end
                table.sort(Keys)
                for Index, Key in Keys do
                    Node(); if Index > 1 then Append(",") end
                    Quote(Key); Append(":"); Value(rawget(Item, Key), Depth + 1)
                end
                Append("}")
            end
            Active[Item] = nil
        else Fail("InvalidValue") end
    end
    Value(Input, 1)
    return table.concat(Parts)
end

function Json.Decode(Text: string, Options: Limits?, Checkpoint: (() -> ())?): any
    local MaxBytes, MaxDepth, MaxNodes = Bounds(Options)
    if Checkpoint ~= nil and type(Checkpoint) ~= "function" then Fail("InvalidOptions") end
    if type(Text) ~= "string" then Fail("InvalidValue") end
    if #Text > MaxBytes then Fail("LimitExceeded") end
    ValidUtf8(Text, Checkpoint)
    local Index, Nodes, Length = 1, 0, #Text
    local NextCheckpoint = 256
    local function Node()
        Nodes += 1; if Nodes > MaxNodes then Fail("LimitExceeded") end
    end
    local function Byte(): number
        if Checkpoint and Index >= NextCheckpoint then Checkpoint(); NextCheckpoint = Index + 256 end
        return string.byte(Text, Index) or -1
    end
    local function Space()
        while Byte() == 32 or Byte() == 9 or Byte() == 10 or Byte() == 13 do Index += 1 end
    end
    local function Hex(): number
        local Result = 0
        for _ = 1, 4 do
            local Digit = Byte(); Index += 1
            if Digit >= 48 and Digit <= 57 then Digit -= 48
            elseif Digit >= 65 and Digit <= 70 then Digit -= 55
            elseif Digit >= 97 and Digit <= 102 then Digit -= 87
            else Fail("InvalidJson") end
            Result = Result * 16 + Digit
        end
        return Result
    end
    local function String(): string
        if Byte() ~= 34 then Fail("InvalidJson") end
        Index += 1
        local Start = Index
        local Parts: {string} = {}
        while Index <= Length do
            local Current = Byte()
            if Current == 34 then
                table.insert(Parts, string.sub(Text, Start, Index - 1)); Index += 1
                return table.concat(Parts)
            elseif Current < 32 then Fail("InvalidJson")
            elseif Current == 92 then
                table.insert(Parts, string.sub(Text, Start, Index - 1)); Index += 1
                local Escape = Byte(); Index += 1
                if Escape == 34 then table.insert(Parts, '"')
                elseif Escape == 92 then table.insert(Parts, "\\")
                elseif Escape == 47 then table.insert(Parts, "/")
                elseif Escape == 98 then table.insert(Parts, "\b")
                elseif Escape == 102 then table.insert(Parts, "\f")
                elseif Escape == 110 then table.insert(Parts, "\n")
                elseif Escape == 114 then table.insert(Parts, "\r")
                elseif Escape == 116 then table.insert(Parts, "\t")
                elseif Escape == 117 then
                    local Scalar = Hex()
                    if Scalar >= 55296 and Scalar <= 56319 then
                        if Byte() ~= 92 or string.byte(Text, Index + 1) ~= 117 then Fail("InvalidJson") end
                        Index += 2
                        local Low = Hex()
                        if Low < 56320 or Low > 57343 then Fail("InvalidJson") end
                        Scalar = 65536 + (Scalar - 55296) * 1024 + Low - 56320
                    elseif Scalar >= 56320 and Scalar <= 57343 then Fail("InvalidJson") end
                    table.insert(Parts, utf8.char(Scalar))
                else Fail("InvalidJson") end
                Start = Index
            else Index += 1 end
        end
        return Fail("InvalidJson")
    end
    local function Number(): number
        local Start = Index
        if Byte() == 45 then Index += 1 end
        if Byte() == 48 then Index += 1
        elseif Byte() >= 49 and Byte() <= 57 then
            repeat Index += 1 until Byte() < 48 or Byte() > 57
        else Fail("InvalidJson") end
        if Byte() == 46 then
            Index += 1
            if Byte() < 48 or Byte() > 57 then Fail("InvalidJson") end
            repeat Index += 1 until Byte() < 48 or Byte() > 57
        end
        if Byte() == 69 or Byte() == 101 then
            Index += 1
            if Byte() == 43 or Byte() == 45 then Index += 1 end
            if Byte() < 48 or Byte() > 57 then Fail("InvalidJson") end
            repeat Index += 1 until Byte() < 48 or Byte() > 57
        end
        local Result = tonumber(string.sub(Text, Start, Index - 1))
        if Result == nil or Result ~= Result or Result == math.huge or Result == -math.huge then Fail("InvalidJson") end
        return Result :: number
    end
    local function Value(Depth: number): any
        Node(); if Depth > MaxDepth then Fail("LimitExceeded") end
        Space()
        local Current = Byte()
        if Current == 34 then return String()
        elseif Current == 45 or (Current >= 48 and Current <= 57) then return Number()
        elseif Current == 91 then
            Index += 1; Space()
            local Result: {any} = setmetatable({}, ArrayMeta)
            if Byte() == 93 then Index += 1; return Result end
            while true do
                table.insert(Result, Value(Depth + 1)); Space()
                if Byte() == 93 then Index += 1; return Result end
                if Byte() ~= 44 then Fail("InvalidJson") end
                Index += 1
            end
        elseif Current == 123 then
            Index += 1; Space()
            local Result: {[string]: any} = setmetatable({}, ObjectMeta)
            if Byte() == 125 then Index += 1; return Result end
            while true do
                Node(); Space(); local Key = String(); Space()
                if rawget(Result, Key) ~= nil then Fail("DuplicateKey") end
                if Byte() ~= 58 then Fail("InvalidJson") end
                Index += 1; Result[Key] = Value(Depth + 1); Space()
                if Byte() == 125 then Index += 1; return Result end
                if Byte() ~= 44 then Fail("InvalidJson") end
                Index += 1
            end
        end
        if string.sub(Text, Index, Index + 3) == "true" then Index += 4; return true end
        if string.sub(Text, Index, Index + 4) == "false" then Index += 5; return false end
        if string.sub(Text, Index, Index + 3) == "null" then Index += 4; return Null end
        return Fail("InvalidJson")
    end
    local Result = Value(1); Space()
    if Index <= Length then Fail("InvalidJson") end
    return Result
end

return table.freeze(Json)
