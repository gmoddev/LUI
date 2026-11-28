--!strict
local Binary = require("@Binary/Binary")
export type Null = {read Kind: "Null"}
export type IntegerValue = {read Kind: "Integer", read Digits: string}
export type Value = number | string | buffer | Null | IntegerValue
export type Parameters = {Value}
export type Result = {Columns: {string}, Rows: {{Value}}}
export type Batch = {Sql: string, Bindings: {Parameters}}
export type Completion = (Results: {Result}?, Failure: string?) -> ()
export type Statement = {
    read RunMany: (Self: Statement, Bindings: {Parameters}, Complete: Completion) -> NativeRequest,
    read Close: (Self: Statement) -> (),
}
export type Connection = {
    read Execute: (Self: Connection, Sql: string, Bindings: Parameters, Complete: Completion) -> NativeRequest,
    read Transaction: (Self: Connection, Groups: {Batch}, Complete: Completion, Mode: string?) -> NativeRequest,
    read Prepare: (Self: Connection, Sql: string) -> Statement,
    read Snapshot: (Self: Connection) -> buffer,
    read Close: (Self: Connection) -> (),
}

local NullValue: Null = table.freeze({Kind = "Null"}) :: Null
local SQLite = {Null = NullValue}

function SQLite.Integer(Digits: string): IntegerValue
    assert(#Digits <= 20 and string.match(Digits, "^%-?%d+$"), "[LUI:SQLite] InvalidInteger")
    return table.freeze({Kind = "Integer", Digits = Digits}) :: IntegerValue
end

local function WriteValue(Writer: Binary.Writer, Item: Value)
    if type(Item) == "number" then
        assert(Item == Item and math.abs(Item) < math.huge, "[LUI:SQLite] InvalidNumber")
        local Data = buffer.create(8)
        buffer.writef64(Data, 0, Item)
        Writer.U8(1)
        Writer.Bytes(buffer.tostring(Data))
    elseif type(Item) == "string" then Writer.U8(2); Writer.Bytes(Item)
    elseif typeof(Item) == "buffer" then
        assert(buffer.len(Item) <= 65535, "[LUI:SQLite] BlobLimit")
        Writer.U8(3); Writer.Bytes(buffer.tostring(Item))
    elseif Item.Kind == "Integer" then Writer.U8(4); Writer.Bytes(Item.Digits)
    else assert(Item == NullValue, "[LUI:SQLite] InvalidValue"); Writer.U8(0) end
end

local function ReadValue(Reader: Binary.Reader): Value
    local Kind = Reader.U8()
    if Kind == 0 then return NullValue end
    local Bytes = Reader.Bytes()
    if Kind == 1 then return buffer.readf64(buffer.fromstring(Bytes), 0) end
    if Kind == 2 then return Bytes end
    if Kind == 3 then return buffer.fromstring(Bytes) end
    assert(Kind == 4, "[LUI:SQLite] InvalidResult")
    local Number = tonumber(Bytes)
    if Number and math.abs(Number) <= 9007199254740991 then return Number end
    return SQLite.Integer(Bytes)
end

local function ReadResult(Data: buffer): (buffer, {Result})
    local Length = buffer.readu32(Data, 0)
    assert(Length <= 1048576 and Length <= buffer.len(Data) - 4, "[LUI:SQLite] InvalidResult")
    local Image = buffer.create(Length)
    buffer.copy(Image, 0, Data, 4, Length)
    local Reader = Binary.Reader(buffer.readstring(Data, 4 + Length, buffer.len(Data) - 4 - Length))
    local Results: {Result} = {}
    for _ = 1, Reader.U16() do
        local Columns: {string} = {}
        for _ = 1, Reader.U8() do table.insert(Columns, Reader.Bytes()) end
        local Rows: {{Value}} = {}
        for _ = 1, Reader.U16() do
            local Row: {Value} = {}
            for _ = 1, #Columns do table.insert(Row, ReadValue(Reader)) end
            table.insert(Rows, Row)
        end
        table.insert(Results, {Columns = Columns, Rows = Rows})
    end
    assert(Reader.Remaining() == 0, "[LUI:SQLite] InvalidResult")
    return Image, Results
end

local function Connection(Image: buffer?, Wait: number?): Connection
    assert(not Image or buffer.len(Image) <= 1048576, "[LUI:SQLite] ImageLimit")
    local Current = if Image then buffer.fromstring(buffer.tostring(Image)) else buffer.create(0)
    local Pending: NativeRequest? = nil
    local Closed = false
    local function Transaction(Groups: {Batch}, Complete: Completion, Mode: string?): NativeRequest
        assert(not Closed and Pending == nil and type(Complete) == "function", "[LUI:SQLite] ClosedOrBusy")
        local Writer = Binary.Writer()
        local Policy = if Mode == nil or Mode == "Deferred" then 0 elseif Mode == "Immediate" then 1 elseif Mode == "Exclusive" then 2 else -1
        assert(Policy >= 0 and #Groups >= 1 and #Groups <= 32, "[LUI:SQLite] InvalidTransaction")
        Writer.U8(Policy); Writer.U8(#Groups)
        local Runs = 0
        for _, Group in Groups do
            Runs += #Group.Bindings
            assert(#Group.Sql >= 1 and #Group.Sql <= 8192 and #Group.Bindings >= 1 and Runs <= 64, "[LUI:SQLite] BatchLimit")
            Writer.Bytes(Group.Sql); Writer.U8(#Group.Bindings)
            for _, Values in Group.Bindings do
                assert(#Values <= 32, "[LUI:SQLite] ParameterLimit")
                Writer.U8(#Values)
                for _, Item in Values do WriteValue(Writer, Item) end
            end
        end
        local Commands = Writer.Finish()
        local Length = buffer.len(Current)
        local Data = buffer.create(4 + Length + #Commands)
        buffer.writeu32(Data, 0, Wait or Length)
        buffer.copy(Data, 4, Current)
        buffer.writestring(Data, 4 + Length, Commands)
        local function Finish(Result: buffer?, _: any, Failure: string?)
            Pending = nil
            if Closed then return end
            if Failure or not Result then Complete(nil, Failure or "ProviderFailure"); return end
            local Ok, NextImage, Results = pcall(ReadResult, Result)
            if not Ok then Complete(nil, "InvalidResult"); return end
            Current = NextImage
            Complete(Results, nil)
        end
        local Service = app:GetService("SQLite")
        local Request = if Wait then Service:LiveTransaction(Data, nil, Finish) else Service:Transaction(Data, nil, Finish)
        Pending = Request
        return Request
    end
    local function Prepare(Query: string): Statement
        assert(not Closed and #Query >= 1 and #Query <= 8192, "[LUI:SQLite] InvalidStatement")
        local Sql: string? = Query
        return table.freeze({
            RunMany = function(_Self: Statement, Bindings: {Parameters}, Complete: Completion): NativeRequest
                return Transaction({{Sql = assert(Sql, "[LUI:SQLite] Closed"), Bindings = Bindings}}, Complete, nil)
            end,
            Close = function(_Self: Statement) Sql = nil end,
        })
    end
    return table.freeze({
        Execute = function(_Self: Connection, Sql: string, Bindings: Parameters, Complete: Completion): NativeRequest
            return Transaction({{Sql = Sql, Bindings = {Bindings}}}, Complete, nil)
        end,
        Transaction = function(_Self: Connection, Groups: {Batch}, Complete: Completion, Mode: string?): NativeRequest
            return Transaction(Groups, Complete, Mode)
        end,
        Prepare = function(_Self: Connection, Sql: string): Statement return Prepare(Sql) end,
        Snapshot = function(_Self: Connection): buffer
            assert(not Closed and not Pending, "[LUI:SQLite] ClosedOrBusy")
            assert(Wait == nil, "[LUI:SQLite] LiveDatabase")
            return buffer.fromstring(buffer.tostring(Current))
        end,
        Close = function(_Self: Connection)
            Closed = true
            if Pending then Pending:Cancel() end
            Current = buffer.create(0)
        end,
    }) :: Connection
end

local function WaitTime(Milliseconds: number?): number
    local Wait = Milliseconds or 2000
    assert(Wait >= 0 and Wait <= 5000 and Wait == math.floor(Wait), "[LUI:SQLite] InvalidTimeout")
    return Wait
end

function SQLite.Open(Image: buffer?): Connection
    return Connection(Image, nil)
end

function SQLite.OpenLive(WaitMilliseconds: number?): Connection
    return Connection(nil, WaitTime(WaitMilliseconds))
end

function SQLite.CheckLive(Complete: (boolean, string?) -> (), WaitMilliseconds: number?): NativeRequest
    local Input = buffer.create(4)
    buffer.writeu32(Input, 0, WaitTime(WaitMilliseconds))
    return app:GetService("SQLite"):CheckLive(Input, nil, function(Data, _, Failure)
        Complete(Data ~= nil and buffer.len(Data) == 1 and buffer.readu8(Data, 0) == 1, Failure)
    end)
end

return table.freeze(SQLite)
