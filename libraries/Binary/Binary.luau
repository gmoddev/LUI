--!strict
local Binary = {Version = "0.1.0", RequiredCapabilities = table.freeze({})}
local Ceiling = 65536

export type Writer = {
    read U8: (number) -> (), read U16: (number) -> (), read U32: (number) -> (),
    read Bytes: (string) -> (), read Finish: () -> string,
}
export type Reader = {
    read U8: () -> number, read U16: () -> number, read U32: () -> number,
    read Bytes: () -> string, read Remaining: () -> number,
}

local function Integer(Value: number, Maximum: number)
    if type(Value) ~= "number" or Value % 1 ~= 0 or Value < 0 or Value > Maximum then
        error("[LUI:Binary] IntegerOutOfRange", 0)
    end
end

function Binary.Writer(MaxBytes: number?): Writer
    local Limit = if MaxBytes == nil then Ceiling else MaxBytes
    Integer(Limit, Ceiling)
    local Data = buffer.create(math.min(64, Limit))
    local Offset = 0
    local function Reserve(Size: number)
        if Size > Limit - Offset then error("[LUI:Binary] LimitExceeded", 0) end
        local Capacity = buffer.len(Data)
        if Offset + Size > Capacity then
            local Next = buffer.create(math.min(Limit, math.max(Offset + Size, Capacity * 2)))
            buffer.copy(Next, 0, Data, 0, Offset)
            Data = Next
        end
    end
    local function U8(Value: number)
        Integer(Value, 255)
        Reserve(1)
        buffer.writeu8(Data, Offset, Value)
        Offset += 1
    end
    local function U16(Value: number)
        Integer(Value, 65535)
        Reserve(2)
        buffer.writeu16(Data, Offset, Value)
        Offset += 2
    end
    local function U32(Value: number)
        Integer(Value, 4294967295)
        Reserve(4)
        buffer.writeu32(Data, Offset, Value)
        Offset += 4
    end
    local function Bytes(Value: string)
        if type(Value) ~= "string" or #Value > 65535 then error("[LUI:Binary] InvalidBytes", 0) end
        Reserve(2 + #Value)
        buffer.writeu16(Data, Offset, #Value)
        buffer.writestring(Data, Offset + 2, Value)
        Offset += 2 + #Value
    end
    return table.freeze({U8 = U8, U16 = U16, U32 = U32, Bytes = Bytes,
        Finish = function() return buffer.readstring(Data, 0, Offset) end})
end

function Binary.Reader(Input: string): Reader
    if type(Input) ~= "string" or #Input > Ceiling then error("[LUI:Binary] InvalidBytes", 0) end
    local Data = buffer.fromstring(Input)
    local Offset = 0
    local function Require(Size: number)
        if Size > buffer.len(Data) - Offset then error("[LUI:Binary] Truncated", 0) end
    end
    local function U8(): number
        Require(1)
        local Value = buffer.readu8(Data, Offset)
        Offset += 1
        return Value
    end
    local function U16(): number
        Require(2)
        local Value = buffer.readu16(Data, Offset)
        Offset += 2
        return Value
    end
    local function U32(): number
        Require(4)
        local Value = buffer.readu32(Data, Offset)
        Offset += 4
        return Value
    end
    local function Bytes(): string
        Require(2)
        local Length = buffer.readu16(Data, Offset)
        Require(2 + Length)
        local Value = buffer.readstring(Data, Offset + 2, Length)
        Offset += 2 + Length
        return Value
    end
    return table.freeze({U8 = U8, U16 = U16, U32 = U32, Bytes = Bytes,
        Remaining = function() return buffer.len(Data) - Offset end})
end

return table.freeze(Binary)
