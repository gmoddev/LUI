--!strict
local Json = require("@Json/Json")
local Encoding = require("@Encoding/Encoding")

export type Layout = {read Close: (Self: Layout) -> boolean}
export type CType = string | Layout
export type Field = {Name: string, Type: CType}
export type CallResult = {Value: any, Errno: number, LastError: number}
export type Foreign = {
    read Call: (Self: Foreign, Values: {any}) -> any,
    read CallWithError: (Self: Foreign, Values: {any}) -> CallResult,
    read Close: (Self: Foreign) -> boolean,
}
export type Library = {
    read Bind: (Self: Library, Symbol: string, Parameters: {CType}, Result: CType) -> Foreign,
    read BindVariadic: (Self: Library, Symbol: string, Fixed: number, Parameters: {CType}, Result: CType) -> Foreign,
    read Close: (Self: Library) -> boolean,
}
export type Pointer = {
    read Slice: (Self: Pointer, Offset: number, Size: number) -> Pointer,
    read TrustBorrow: (Self: Pointer, Size: number) -> Pointer,
    read Adopt: (Self: Pointer, Size: number, Release: Foreign) -> (),
    read Read: (Self: Pointer, Offset: number, Size: number) -> string,
    read Write: (Self: Pointer, Offset: number, Bytes: string) -> (),
    read Get: (Self: Pointer, Offset: number, Type: CType) -> any,
    read Set: (Self: Pointer, Offset: number, Type: CType, Value: any) -> (),
    read Close: (Self: Pointer) -> boolean,
}
export type Notification = {read Close: (Self: Notification) -> boolean}
local Ffi = {Null = Json.Null}
local Ids: {[any]: number} = setmetatable({}, {__mode="k"}) :: any
local Members: {[any]: any} = setmetatable({}, {__mode="k"}) :: any
local Handlers: {[number]: (any) -> ()} = {}
local Computed: {[number]: ({any}) -> any} = {}
local ComputedCount = 0
local RequestLimits: Json.Limits = {MaxBytes=131072, MaxDepth=24}
local Service: any = nil
local function GetService(): any
    if not Service then Service = app:GetService("Ffi") end
    return Service
end
local function ComputePacket(Packet: string): string
    local Call = Json.Decode(Packet)
    local Calculate = Computed[Call.Id]
    if not Calculate then return "" end
    local Value = Calculate(Call.Args)
    return Json.Encode(if Value == nil then Json.Null else Value, {MaxBytes=128, MaxDepth=2})
end
local function Dispatch(Packet: string, CallsNative: boolean): string
    local Provider = GetService()
    if ComputedCount ~= 0 and CallsNative then
        return Provider:DispatchScoped(Packet, ComputePacket)
    end
    return Provider:Dispatch(Packet)
end
local function Request(Packet: any): any
    local Reply = Json.Decode(Dispatch(Json.Encode(Packet, RequestLimits), Packet.Op == "Call"))
    if not Reply.Ok then error("[LUI:Ffi] " .. Reply.Error, 0) end
    return Reply.Result
end
local Binary: boolean? = nil
local PackedLayouts = false
local NativeLongBytes: number? = nil
local ChunkLimit = 16384
local function GetPlatform(): any
    local Info = Request({Op="Platform"})
    Binary = Info.Binary == true
    PackedLayouts = Info.PackedLayouts == true
    NativeLongBytes = Info.LongBytes
    return Info
end
local function SupportsBinary(): boolean
    if Binary == nil then GetPlatform() end
    return Binary == true
end
local function Packet(Op: string, Ref: number, Size: number): buffer
    local Data = buffer.create(Size)
    buffer.writestring(Data, 0, "LFB1" .. Op)
    buffer.writeu32(Data, 5, Ref)
    return Data
end
local function CheckReply(Reply: string): string
    if string.byte(Reply, 1) ~= 0 then
        error("[LUI:Ffi] " .. string.sub(Reply, 2), 0)
    end
    return Reply
end
local function ExchangeBytes(Data: buffer): string
    return CheckReply(GetService():Dispatch(buffer.tostring(Data)))
end
local function OffsetWord(Value: number, Limit: number)
    assert(type(Value) == "number" and Value >= 0 and Value <= Limit and Value % 1 == 0, "[LUI:Ffi] InvalidCount")
end
local function Id(Token: any): number
    local N = Ids[Token]
    assert(N and N > 0, "[LUI:Ffi] ClosedOrInvalidHandle")
    return N
end
local function Close(Self: any): boolean
    if Ids[Self] == 0 then return false end
    local N = Id(Self)
    local Closed = Request({Op="Close", Ref=N})
    Handlers[N] = nil
    if Computed[N] then
        Computed[N] = nil
        ComputedCount -= 1
    end
    Ids[Self] = 0
    return Closed
end
local function Token(N: number, Methods: any): any
    local Object = table.clone(Methods)
    Object.Close = Close
    Ids[Object] = N
    return table.freeze(Object)
end
local function Spec(T: CType): any
    return if type(T) == "string" then T else Id(T)
end
local function Sequence(Values: {any}, Convert: (any) -> any): {any}
    local Result = {}
    for I, Value in Values do
        assert(type(I) == "number" and I % 1 == 0 and I >= 1 and I <= #Values, "[LUI:Ffi] InvalidSequence")
        Result[I] = Convert(Value)
    end
    return Json.Array(Result)
end
local PointerMethods: any = {}
local function Convert(T: CType, V: any, Out: boolean): any
    if V == nil or V == Json.Null then return Json.Null end
    if T == "pointer" then
        return if Out then Token(V.Ref, PointerMethods) else {Ref=Id(V)}
    end
    if type(T) == "string" then return V end
    local Definition = Members[T]
    assert(Definition and type(V) == "table", "[LUI:Ffi] InvalidAggregate")
    if Definition.Element then
        return Sequence(V, function(Item) return Convert(Definition.Element, Item, Out) end)
    end
    local Result: {[string]: any} = {}
    for Name, Item in V do
        local Member = Definition.Fields[Name]
        assert(Member, "[LUI:Ffi] UnknownField")
        Result[Name] = Convert(Member, Item, Out)
    end
    return Result
end
type CompactCodec = {
    Bytes: number,
    Write: (Data: buffer, At: number, Value: any) -> boolean,
    Read: (Data: buffer, At: number) -> any,
}
local function PrepareCompact(T: CType): CompactCodec
    if type(T) == "string" then
        if T == "void" then return {Bytes=0, Write=function() return true end, Read=function() return nil end} end
        if T == "pointer" then
            return {Bytes=8, Write=function(Data, At, Value)
                buffer.writeu32(Data, At, if Value == nil or Value == Json.Null then 0 else Id(Value))
                return true
            end, Read=function(Data, At)
                local Ref = buffer.readu32(Data, At)
                return if Ref == 0 then Json.Null else Token(Ref, PointerMethods)
            end}
        end
        local Word = T == "i64" or T == "u64" or T == "size" or T == "intptr" or T == "uintptr"
        if T == "long" or T == "ulong" then
            Word = (NativeLongBytes or GetPlatform().LongBytes) == 8
        end
        if Word then
            return {Bytes=8, Write=function(Data, At, Value)
                assert(type(Value) == "string" and #Value == 16, "[LUI:Ffi] ExpectedWord64")
                buffer.writestring(Data, At, string.reverse(Encoding.HexDecode(Value)))
                return true
            end, Read=function(Data, At)
                return string.format("%08x%08x", buffer.readu32(Data, At + 4), buffer.readu32(Data, At))
            end}
        end
        if T == "bool8" or T == "bool32" then
            return {Bytes=8, Write=function(Data, At, Value)
                assert(type(Value) == "boolean", "[LUI:Ffi] ExpectedBoolean")
                buffer.writef64(Data, At, if Value then 1 else 0)
                return true
            end, Read=function(Data, At) return buffer.readf64(Data, At) ~= 0 end}
        end
        return {Bytes=8, Write=function(Data, At, Value)
            assert(type(Value) == "number", "[LUI:Ffi] ExpectedNumber")
            buffer.writef64(Data, At, Value)
            return true
        end, Read=buffer.readf64}
    end
    local Definition = Members[T]
    if Definition.Element then
        local Child = PrepareCompact(Definition.Element)
        local Count: number = Definition.Count
        return {Bytes=Count * Child.Bytes, Write=function(Data, At, Value)
            assert(type(Value) == "table", "[LUI:Ffi] InvalidAggregate")
            if getmetatable(Value) ~= nil then return false end
            assert(#Value == Count, "[LUI:Ffi] InvalidAggregate")
            for Key in Value do
                assert(type(Key) == "number" and Key % 1 == 0 and Key >= 1 and Key <= Count, "[LUI:Ffi] InvalidSequence")
            end
            for Index = 1, Count do
                local Item = rawget(Value, Index)
                assert(Item ~= nil, "[LUI:Ffi] InvalidAggregate")
                if not Child.Write(Data, At + (Index - 1) * Child.Bytes, Item) then return false end
            end
            return true
        end, Read=function(Data, At)
            local Value = table.create(Count)
            for Index = 1, Count do Value[Index] = Child.Read(Data, At + (Index - 1) * Child.Bytes) end
            return Json.Array(Value)
        end}
    end
    local Fields: {{Name: string, Offset: number, Codec: CompactCodec}} = {}
    local Bytes = 0
    local Ordered: {Field} = Definition.Ordered
    for _, Field in Ordered do
        local Child = PrepareCompact(Field.Type)
        table.insert(Fields, {Name=Field.Name, Offset=Bytes, Codec=Child})
        Bytes += Child.Bytes
    end
    local ByName = Definition.Fields
    return {Bytes=Bytes, Write=function(Data, At, Value)
        assert(type(Value) == "table", "[LUI:Ffi] InvalidAggregate")
        if getmetatable(Value) ~= nil then return false end
        for Name in Value do assert(ByName[Name], "[LUI:Ffi] UnknownField") end
        for _, Field in Fields do
            local Member = rawget(Value, Field.Name)
            assert(Member ~= nil, "[LUI:Ffi] InvalidAggregate")
            if not Field.Codec.Write(Data, At + Field.Offset, Member) then return false end
        end
        return true
    end, Read=function(Data, At)
        local Value: {[string]: any} = {}
        for _, Field in Fields do Value[Field.Name] = Field.Codec.Read(Data, At + Field.Offset) end
        return Value
    end}
end
function PointerMethods.Read(Self: Pointer, Offset: number, Size: number): string
    local Ref = Id(Self)
    if not SupportsBinary() then
        return Encoding.HexDecode(Request({Op="Read", Ref=Ref, Offset=Offset, Size=Size}))
    end
    OffsetWord(Offset, 1048576)
    OffsetWord(Size, ChunkLimit)
    local Data = Packet("R", Ref, 17)
    buffer.writeu32(Data, 9, Offset)
    buffer.writeu32(Data, 13, Size)
    return string.sub(ExchangeBytes(Data), 2)
end
function PointerMethods.Write(Self: Pointer, Offset: number, Bytes: string)
    local Ref = Id(Self)
    if not SupportsBinary() then
        Request({Op="Write", Ref=Ref, Offset=Offset, Bytes=Encoding.HexEncode(Bytes)})
        return
    end
    OffsetWord(Offset, 1048576)
    assert(type(Bytes) == "string" and #Bytes <= ChunkLimit, "[LUI:Ffi] InvalidBytes")
    local Data = Packet("W", Ref, 13 + #Bytes)
    buffer.writeu32(Data, 9, Offset)
    buffer.writestring(Data, 13, Bytes)
    ExchangeBytes(Data)
end
function PointerMethods.Get(Self: Pointer, Offset: number, T: CType): any
    return Convert(T, Request({Op="Get", Ref=Id(Self), Offset=Offset, Type=Spec(T)}), true)
end
function PointerMethods.Set(Self: Pointer, Offset: number, T: CType, Value: any)
    Request({Op="Set", Ref=Id(Self), Offset=Offset, Type=Spec(T), Value=Convert(T, Value, false)})
end
function PointerMethods.Slice(Self: Pointer, Offset: number, Size: number): Pointer
    return Token(Request({Op="Slice", Ref=Id(Self), Offset=Offset, Size=Size}).Ref, PointerMethods)
end
function PointerMethods.TrustBorrow(Self: Pointer, Size: number): Pointer
    return Token(Request({Op="TrustBorrow", Ref=Id(Self), Size=Size}).Ref, PointerMethods)
end
function PointerMethods.Adopt(Self: Pointer, Size: number, Release: Foreign)
    Request({Op="Adopt", Ref=Id(Self), Size=Size, Release=Id(Release)})
end
local function BindSignature(Self: Library, Symbol: string, Parameters: {CType}, Result: CType, Fixed: number?): Foreign
    local Types = table.clone(Parameters)
    local Bound = Request({Op=if Fixed then "BindVariadic" else "Bind", Library=Id(Self), Symbol=Symbol,
        Params=Sequence(Types, Spec), Result=Spec(Result), Fixed=Fixed})
    if Bound.Scalar == true then
        local Count = #Types
        local Provider = GetService()
        local function InvokeScalar(Function: Foreign, Values: {any}, WithError: boolean): (any, buffer?)
            assert(#Values == Count, "[LUI:Ffi] ArgumentCount")
            local Data = Packet(if WithError then "E" else "C", Id(Function), 9 + 9 * Count)
            for I = 1, Count do
                local Value = Values[I]
                local At = 9 * I
                if type(Value) == "boolean" then
                    buffer.writeu8(Data, At, 66) -- B
                    buffer.writef64(Data, At + 1, if Value then 1 else 0)
                elseif type(Value) == "number" then
                    buffer.writeu8(Data, At, 78) -- N
                    buffer.writef64(Data, At + 1, Value)
                else
                    error("[LUI:Ffi] InvalidScalarArgument", 0)
                end
            end
            local Payload = buffer.tostring(Data)
            local Reply = CheckReply(if ComputedCount == 0 then Provider:Dispatch(Payload)
                else Provider:DispatchScoped(Payload, ComputePacket))
            local At = if WithError then 10 else 2
            local Kind = string.sub(Reply, At, At)
            local Value: any
            local ResultBytes: buffer? = nil
            if Kind == "N" then
                ResultBytes = buffer.fromstring(Reply)
                Value = buffer.readf64(ResultBytes, At)
            elseif Kind == "T" then Value = true
            elseif Kind == "F" then Value = false
            else assert(Kind == "V", "[LUI:Ffi] InvalidBinaryReply") end
            if WithError and not ResultBytes then ResultBytes = buffer.fromstring(Reply) end
            return Value, ResultBytes
        end
        local function CallWithError(Function: Foreign, Values: {any}): CallResult
            local Value, Bytes = InvokeScalar(Function, Values, true)
            local Errors = assert(Bytes)
            return {Value=Value, Errno=buffer.readi32(Errors, 1), LastError=buffer.readu32(Errors, 5)}
        end
        local function Call(Function: Foreign, Values: {any}): any
            local Value = InvokeScalar(Function, Values, false)
            return Value
        end
        return Token(Bound.Ref, {CallWithError=CallWithError, Call=Call})
    end
    local function InvokeStructured(Function: Foreign, Values: {any}): CallResult
        assert(#Values == #Types, "[LUI:Ffi] ArgumentCount")
        local Args = {}
        for I, T in Types do Args[I] = Convert(T, Values[I], false) end
        local Reply = Request({Op="Call", Function=Id(Function), Args=Json.Array(Args)})
        Reply.Value = if Result == "void" then nil else Convert(Result, Reply.Value, true)
        return Reply
    end
    local Provider = GetService()
    local Extended = Bound.CompactHandles == true and (Provider.DispatchScoped or Provider.CompactHandles)
    if Extended or (Bound.Compact == true and (Provider.DispatchScoped or Provider.CompactAggregates)) then
        local Parameters: {{Offset: number, Codec: CompactCodec}} = {}
        local Size = 9
        for I, T in Types do
            local Codec = PrepareCompact(T)
            Parameters[I] = {Offset=Size, Codec=Codec}
            Size += Codec.Bytes
        end
        local Returned = PrepareCompact(Result)
        local CallOp = if Extended then "H" else "A"
        local ErrorOp = if Extended then "J" else "D"
        local function InvokeCompact(Function: Foreign, Values: {any}, WithError: boolean): (any, buffer?)
            assert(#Values == #Parameters, "[LUI:Ffi] ArgumentCount")
            local PacketBytes = Packet(if WithError then ErrorOp else CallOp, Id(Function), Size)
            for I, Parameter in Parameters do
                if not Parameter.Codec.Write(PacketBytes, Parameter.Offset, Values[I]) then return nil, nil end
            end
            local Reply = CheckReply(Dispatch(buffer.tostring(PacketBytes), true))
            local Offset = if WithError then 9 else 1
            assert(#Reply == Offset + Returned.Bytes, "[LUI:Ffi] InvalidBinaryReply")
            local Data = buffer.fromstring(Reply)
            return Returned.Read(Data, Offset), Data
        end
        local function CallWithError(Function: Foreign, Values: {any}): CallResult
            local Value, Data = InvokeCompact(Function, Values, true)
            if not Data then return InvokeStructured(Function, Values) end
            return {Value=Value, Errno=buffer.readi32(Data, 1), LastError=buffer.readu32(Data, 5)}
        end
        local function Call(Function: Foreign, Values: {any}): any
            local Value, Data = InvokeCompact(Function, Values, false)
            return if Data then Value else InvokeStructured(Function, Values).Value
        end
        return Token(Bound.Ref, {CallWithError=CallWithError, Call=Call})
    end
    return Token(Bound.Ref, {CallWithError=InvokeStructured, Call=function(Function, Values) return InvokeStructured(Function, Values).Value end})
end
local function Bind(Self: Library, Symbol: string, Parameters: {CType}, Result: CType): Foreign
    return BindSignature(Self, Symbol, Parameters, Result, nil)
end
local function BindVariadic(Self: Library, Symbol: string, Fixed: number, Parameters: {CType}, Result: CType): Foreign
    assert(type(Fixed)=="number" and Fixed>=1 and Fixed%1==0,"[LUI:Ffi] InvalidFixedCount")
    return BindSignature(Self, Symbol, Parameters, Result, Fixed)
end
local function Open(Kind: string, Name: string, Digest: string?): Library
    return Token(Request({Op="Open", Kind=Kind, Name=Name, Sha256=Digest}).Ref, {Bind=Bind,BindVariadic=BindVariadic})
end
function Ffi.System(Name: string): Library return Open("System", Name) end
function Ffi.Open(AbsolutePath: string, Sha256: string?): Library return Open("Selected", AbsolutePath, Sha256) end
function Ffi.Packaged(Name: string, Sha256: string): Library return Open("Packaged", Name, Sha256) end
function Ffi.Platform(): {OS: string, Abi: string, LongBytes: number, PointerBytes: number}
    local Info = GetPlatform()
    return {OS=Info.OS, Abi=Info.Abi, LongBytes=Info.LongBytes, PointerBytes=Info.PointerBytes}
end
function Ffi.Word64(High: number, Low: number): string
    for _, Part in {High, Low} do
        assert(Part >= 0 and Part <= 4294967295 and Part % 1 == 0, "[LUI:Ffi] InvalidWord")
    end
    return string.format("%08x%08x", High, Low)
end
function Ffi.Struct(Fields: {Field}, Pack: number?): Layout
    if Pack ~= nil then
        if Binary == nil then GetPlatform() end
        assert(PackedLayouts, "[LUI:Ffi] PackedLayoutUnavailable")
    end
    local ByName: {[string]: CType} = {}
    local Ordered: {Field} = {}
    local Specs = Sequence(Fields, function(Field)
        ByName[Field.Name] = Field.Type
        return {Name=Field.Name, Type=Spec(Field.Type)}
    end)
    for I, Field in Specs do Ordered[I] = {Name=Field.Name, Type=ByName[Field.Name]} end
    local T = Token(Request({Op="Layout", Fields=Specs, Pack=Pack}).Ref, {})
    Members[T] = {Fields=ByName, Ordered=Ordered}
    return T
end
function Ffi.Array(Element: CType, Count: number): Layout
    local T = Token(Request({Op="Layout", Element=Spec(Element), Count=Count}).Ref, {})
    Members[T] = {Element=Element, Count=Count}
    return T
end
function Ffi.Describe(T: CType): {Size: number, Alignment: number, Offsets: {number}}
    return Request({Op="Describe", Type=Spec(T)})
end
function Ffi.Allocate(Size: number, Alignment: number?): Pointer
    return Token(Request({Op="Allocate", Size=Size, Alignment=Alignment}).Ref, PointerMethods)
end
function Ffi.Callback(Params: {CType}, Result: CType, Notify: ({any}) -> (), Fallback: any?): Notification
    assert(type(Notify) == "function", "[LUI:Ffi] HandlerRequired")
    local N = Request({Op="Callback", Params=Sequence(Params, Spec), Result=Spec(Result), Fallback=Fallback}).Ref
    Handlers[N] = Notify
    return Token(N, {})
end
function Ffi.SyncCallback(Params: {CType}, Result: CType, Compute: ({any}) -> any, Fallback: any?): Notification
    assert(type(Compute) == "function", "[LUI:Ffi] HandlerRequired")
    assert(type(GetService().DispatchScoped) == "function", "[LUI:Ffi] SynchronousCallbackUnavailable")
    local N = Request({Op="Callback", Params=Sequence(Params, Spec), Result=Spec(Result), Fallback=Fallback, Computed=true}).Ref
    Computed[N] = Compute
    ComputedCount += 1
    return Token(N, {})
end
local Polling = false
function Ffi.Poll(): (number, number, {string})
    assert(not Polling, "[LUI:Ffi] ReentrantPoll")
    local Reply = Request({Op="Poll"})
    Polling = true
    local Errors: {string} = {}
    local Count = 0
    for _, Event in Reply.Events do
        local Handler = Handlers[Event.Id]
        if Handler then
            Count += 1
            local Ok, Why = pcall(Handler, Event.Args)
            if not Ok then table.insert(Errors, if type(Why) == "string" then Why else "NotificationHandlerFailed") end
        end
    end
    Polling = false
    return Count, Reply.Dropped, Errors
end
function Ffi.Stats(): {Live: number, AllocatedBytes: number} return Request({Op="Stats"}) end
return table.freeze(Ffi)
