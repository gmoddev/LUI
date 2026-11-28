--!strict
local Process = require("@Process/Process")
local Json = require("@Json/Json")
local FfiBroker = {}
export type Selection = {Kind: string, Name: string, Sha256: string?}
export type Request = {Library: Selection, Symbol: string, Params: {string}, Result: string, Args: {any}}
export type BatchRequest = {Library: Selection, Symbol: string, Params: {string}, Result: string, Rows: {{any}}}
export type Reply = {Value: any, Errno: number, LastError: number}
local function Run(Fields: any, TimeoutMilliseconds: number, Complete: (any, string?) -> ()): Process.Request
    local Packet = Json.Encode(Fields, {MaxBytes=4096, MaxDepth=12})
    return Process.Run({Arguments={"--call"}, Input=Packet .. "\n", TimeoutMilliseconds=TimeoutMilliseconds,
        MaxOutputBytes=65536}, function(Result)
        if Result.Status ~= "Exited" then Complete(nil, Result.Status); return end
        local Valid, Reply = pcall(function() return Json.Decode(Result.Stdout) end)
        if not Valid or type(Reply) ~= "table" then Complete(nil, "InvalidBrokerReply"); return end
        if Result.ExitCode ~= 0 or not Reply.Ok then Complete(nil, Reply.Error or "NativeFailure"); return end
        Complete(Reply.Result, nil)
    end)
end
function FfiBroker.Call(Call: Request, TimeoutMilliseconds: number, Complete: (Reply?, string?) -> ()): Process.Request
    return Run({Library=Call.Library, Symbol=Call.Symbol, Params=Json.Array(Call.Params),
        Result=Call.Result, Args=Json.Array(Call.Args)}, TimeoutMilliseconds, Complete)
end
function FfiBroker.CallBatch(Call: BatchRequest, TimeoutMilliseconds: number, Complete: ({Reply}?, string?) -> ()): Process.Request
    local Count = #Call.Rows
    assert(Count >= 1 and Count <= 32, "[LUI:FfiBroker] BatchLimit")
    local Rows = table.create(Count)
    for Index, Row in Call.Rows do
        assert(type(Index) == "number" and Index >= 1 and Index <= Count and Index % 1 == 0, "[LUI:FfiBroker] InvalidRow")
        Rows[Index] = Json.Array(Row)
    end
    return Run({Library=Call.Library, Symbol=Call.Symbol, Params=Json.Array(Call.Params),
        Result=Call.Result, Rows=Json.Array(Rows)}, TimeoutMilliseconds, function(Value, Failure)
        if Failure then Complete(nil, Failure); return end
        if type(Value) ~= "table" or #Value ~= Count then Complete(nil, "InvalidBrokerReply"); return end
        Complete(Value, nil)
    end)
end
return table.freeze(FfiBroker)
