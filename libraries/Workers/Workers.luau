--!strict
local Messages = require("@Messages/Messages")
local Workers = {}
export type Options = {MemoryBytes: number?, MaxInterrupts: number?, TimeoutMilliseconds: number?}
export type Actor = {
    read Send: (Self: Actor, Value: any, Complete: (any, string?) -> ()) -> NativeRequest,
    read Close: (Self: Actor) -> (),
}
export type Job = {read Cancel: (Self: Job) -> boolean}
export type Pool = {
    read Send: (Self: Pool, Value: any, Complete: (any, string?) -> ()) -> (Job?, string?),
    read Close: (Self: Pool) -> (),
}
type Reply = (any, string?) -> ()
type Queued = {Packet: buffer?, Complete: Reply?, Request: NativeRequest?, Done: boolean}
local SendEncoded: {[Actor]: (buffer, Reply) -> NativeRequest} = setmetatable({}, {__mode="k"}) :: any
local function InputPacket(Value: any): buffer
    if typeof(Value) ~= "buffer" then return buffer.fromstring(Messages.Encode(Value)) end
    local Size = buffer.len(Value)
    assert(Size <= 65531, "[LUI:Messages] LimitExceeded")
    local Packet = buffer.create(5 + Size)
    buffer.writestring(Packet, 0, "LUM1B")
    buffer.copy(Packet, 5, Value)
    return Packet
end
local function DecodePacket(Packet: buffer): any
    local Size = buffer.len(Packet)
    if Size >= 5 and Size <= 65536 and buffer.readstring(Packet, 0, 5) == "LUM1B" then
        local Value = buffer.create(Size - 5)
        buffer.copy(Value, 0, Packet, 5)
        return Value
    end
    return Messages.Decode(buffer.tostring(Packet))
end
function Workers.Handler(Transform: (any) -> any): (string) -> string
    return function(Packet: string): string
        return Messages.Encode(Transform(Messages.Decode(Packet)))
    end
end
local function Word(Value: number?, Default: number, Minimum: number, Maximum: number): number
    local Result = if Value == nil then Default else Value
    assert(type(Result)=="number" and Result % 1 == 0 and Result >= Minimum and Result <= Maximum, "[LUI:Workers] InvalidOptions")
    return Result
end
local function Start(Path: string, Options: Options?, Complete: (Actor?, string?) -> (), Trusted: boolean): NativeRequest
    assert(type(Path)=="string" and #Path >= 3 and #Path <= 256 and string.sub(Path,1,2) == "./" and type(Complete) == "function", "[LUI:Workers] InvalidArgument")
    assert(Options==nil or (type(Options)=="table" and getmetatable(Options)==nil),"[LUI:Workers] InvalidOptions")
    local Limits: Options = Options or {}
    for Key in Limits :: any do
        assert(Key=="TimeoutMilliseconds" or Key=="MemoryBytes" or Key=="MaxInterrupts","[LUI:Workers] InvalidOptions")
    end
    local Service = app:GetService("WorkerService")
    local Input = buffer.create(12 + #Path)
    buffer.writeu32(Input,0,Word(Limits.MemoryBytes,8388608,1048576,16777216))
    buffer.writeu32(Input,4,Word(Limits.MaxInterrupts,1000000,1,100000000))
    buffer.writeu32(Input,8,Word(Limits.TimeoutMilliseconds,5000,1,30000))
    buffer.writestring(Input,12,Path)
    local function Started(_: buffer?, Handle: NativeHandle?, Failure: string?)
        if not Handle then Complete(nil,Failure or "ProviderFailure"); return end
        local Closed = false
        local function Close(_Self: Actor)
            if not Closed then Closed=true; Handle:Close() end
        end
        local function SendPacket(Packet: buffer, Callback: Reply): NativeRequest
            assert(not Closed and type(Callback)=="function","[LUI:Workers] ClosedOrInvalid")
            return Service:Send(Packet,Handle,function(Bytes,_,Why)
                if not Bytes then Callback(nil,Why or "ProviderFailure"); return end
                local Ok,Result=pcall(DecodePacket,Bytes)
                if Ok then Callback(Result,nil) else Callback(nil,"InvalidPacket") end
            end)
        end
        local function Send(_Self: Actor, Value: any, Callback: Reply): NativeRequest
            assert(not Closed and type(Callback)=="function","[LUI:Workers] ClosedOrInvalid")
            return SendPacket(InputPacket(Value), Callback)
        end
        local Actor: Actor = table.freeze({Send=Send,Close=Close})
        SendEncoded[Actor] = SendPacket
        local Ok=pcall(Complete,Actor,nil)
        if not Ok then Actor:Close();error("[LUI:Workers] CompletionFailed",0) end
    end
    if Trusted then return Service:StartTrusted(Input,nil,Started) end
    return Service:Start(Input,nil,Started)
end
function Workers.Start(Path: string, Options: Options?, Complete: (Actor?, string?) -> ()): NativeRequest
    return Start(Path, Options, Complete, false)
end
function Workers.StartTrusted(Path: string, Options: Options?, Complete: (Actor?, string?) -> ()): NativeRequest
    return Start(Path, Options, Complete, true)
end
local function StartPool(Path: string, Count: number?, Options: Options?, Trusted: boolean): Pool
    local Size = Word(Count, 2, 1, 4)
    local Queue: {Queued} = {}
    local Available: {Actor} = {}
    local Actors: {[Actor]: boolean} = {}
    local Running: {[Queued]: boolean} = {}
    local QueueBytes, Starting, Live = 0, Size, 0
    local Closed = false
    local function Complete(Job: Queued, Value: any, Failure: string?)
        if Job.Done then return end
        Job.Done = true
        Job.Request = nil
        local Reply = assert(Job.Complete)
        Job.Complete = nil
        Job.Packet = nil
        task.defer(function() Reply(Value, Failure) end)
    end
    local function Take(Index: number): Queued
        local Job = assert(table.remove(Queue, Index))
        QueueBytes -= buffer.len(assert(Job.Packet))
        return Job
    end
    local function Drain()
        while not Closed and #Available > 0 and #Queue > 0 do
            local Actor = assert(table.remove(Available))
            local Job = Take(1)
            Running[Job] = true
            local function Finished(Value: any, Failure: string?)
                Running[Job] = nil
                Complete(Job, Value, Failure)
                if Closed then return end
                if Failure then
                    Actors[Actor] = nil
                    Live -= 1
                    Actor:Close()
                else
                    table.insert(Available, Actor)
                end
                Drain()
            end
            local Sent, Request = pcall(SendEncoded[Actor], assert(Job.Packet), Finished)
            if Sent then
                Job.Request = Request
            else
                -- The shared owner request limit can reject admission before
                -- the provider sees the packet. The actor remains available.
                Running[Job] = nil
                table.insert(Available, Actor)
                Complete(Job, nil, "WouldBlock")
            end
            Job.Packet = nil
        end
        if Starting == 0 and Live == 0 then
            while #Queue > 0 do Complete(Take(1), nil, "Closed") end
        end
    end
    local function Send(_Self: Pool, Value: any, Callback: Reply): (Job?, string?)
        assert(type(Callback) == "function", "[LUI:Workers] InvalidArgument")
        if Closed or (Starting == 0 and Live == 0) then return nil, "Closed" end
        if #Queue >= 16 then return nil, "WouldBlock" end
        local Packet = InputPacket(Value)
        local Size = buffer.len(Packet)
        if Size > 262144 - QueueBytes then return nil, "WouldBlock" end
        local Request: Queued = {Packet=Packet, Complete=Callback, Done=false, Request=nil}
        QueueBytes += Size
        table.insert(Queue, Request)
        local function Cancel(_Job: Job): boolean
            if Request.Done then return false end
            local Index = table.find(Queue, Request)
            if Index then Take(Index) end
            if Request.Request then
                local Native = Request.Request
                -- A completion may already be queued on this owner turn.
                pcall(function() Native:Cancel() end)
            end
            Complete(Request, nil, "Canceled")
            return true
        end
        Drain()
        return table.freeze({Cancel=Cancel}), nil
    end
    local function Close(_Self: Pool)
        if Closed then return end
        Closed = true
        while #Queue > 0 do Complete(Take(1), nil, "Closed") end
        for Job in Running do Complete(Job, nil, "Closed") end
        for Actor in Actors do Actor:Close() end
        table.clear(Actors)
        table.clear(Available)
    end
    local function Started(Actor: Actor?)
        Starting -= 1
        if Actor then
            if Closed then Actor:Close() else
                Actors[Actor] = true
                Live += 1
                table.insert(Available, Actor)
            end
        end
        Drain()
    end
    local Pool = table.freeze({Send=Send, Close=Close})
    local Launch = if Trusted then Workers.StartTrusted else Workers.Start
    for _ = 1, Size do
        local Submitted, Failure = pcall(Launch, Path, Options, Started)
        if not Submitted then
            Pool:Close()
            error(Failure, 0)
        end
    end
    return Pool
end
function Workers.Pool(Path: string, Count: number?, Options: Options?): Pool
    return StartPool(Path, Count, Options, false)
end
function Workers.PoolTrusted(Path: string, Count: number?, Options: Options?): Pool
    return StartPool(Path, Count, Options, true)
end
return table.freeze(Workers)
