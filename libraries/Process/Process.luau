--!strict
local Json = require("@Json/Json")
local Process = {}
export type Options = {
    Arguments: {string}?, Environment: {[string]: string}?, Input: string?,
    TimeoutMilliseconds: number?, MaxOutputBytes: number?,
    OnOutput: ((Stream: string, Bytes: string) -> ())?,
}
export type Result = {Status: string, ExitCode: number?, Stdout: string, Stderr: string}
export type Request = {read Cancel: (Self: Request) -> boolean}
export type Session = {
    read Write: (Self: Session, Bytes: string) -> boolean,
    read CloseInput: (Self: Session) -> boolean,
    read Terminate: (Self: Session) -> boolean,
    read Kill: (Self: Session) -> boolean,
}
type Call = {Id: number, Stdout: {string}, Stderr: {string}, Output: ((string, string) -> ())?, Complete: (Result) -> ()}
local Calls: {[number]: Call} = {}
local Listening = false
local function Start(Options: Options, Complete: (Result) -> (), Interactive: boolean): (number, any)
    assert((Interactive or next(Calls)==nil) and type(Complete)=="function","[LUI:Process] BusyOrInvalid")
    local Host = app:GetService("Process")
    if not Listening then
        Host.Output:Connect(function(Id: number, Stream: number, Bytes: string)
            local Current = Calls[Id]
            if not Current or Current.Id ~= Id then return end
            table.insert(if Stream == 1 then Current.Stdout else Current.Stderr, Bytes)
            if Current.Output then Current.Output(if Stream == 1 then "stdout" else "stderr", Bytes) end
        end)
        Host.Finished:Connect(function(Id: number, Status: string, Code: number)
            local Current = Calls[Id]
            if not Current or Current.Id ~= Id then return end
            Calls[Id] = nil
            Current.Complete({Status=Status, ExitCode=if Status == "Exited" then Code else nil,
                Stdout=table.concat(Current.Stdout), Stderr=table.concat(Current.Stderr)})
        end)
        Listening = true
    end
    local Packet = Json.Encode({Arguments=Json.Array(Options.Arguments or {}), Environment=Json.Object(Options.Environment or {}),
        Timeout=Options.TimeoutMilliseconds or 5000, Limit=Options.MaxOutputBytes or 65536, Interactive=if Interactive then true else nil},
        {MaxBytes=16384, MaxDepth=3, MaxNodes=128})
    local Id = Host:Start(Packet, Options.Input or "")
    Calls[Id] = {Id=Id, Stdout={}, Stderr={}, Output=Options.OnOutput, Complete=Complete}
    return Id,Host
end
function Process.Run(Options: Options, Complete: (Result) -> ()): Request
    local Id,Host=Start(Options,Complete,false)
    return table.freeze({Cancel=function(_Self: Request): boolean return Host:Stop(Id) end})
end
function Process.Open(Options: Options, Complete: (Result) -> ()): Session
    local Id,Host=Start(Options,Complete,true)
    local function Control(Command: string, Bytes: string): boolean
        return Calls[Id]~=nil and Host:Start(Json.Encode({Command=Command,Id=Id}),Bytes)==1
    end
    local function Finish(_Self: Session): boolean return Control("Eof","") end
    local function Write(_Self: Session,Bytes: string): boolean
        assert(type(Bytes)=="string" and #Bytes<=4096,"[LUI:Process] InvalidChunk")
        return Control("Write",Bytes)
    end
    local function Kill(_Self: Session): boolean return Calls[Id]~=nil and Host:Stop(Id) end
    return table.freeze({Write=Write,CloseInput=Finish,Terminate=Finish,Kill=Kill})
end
function Process.Pipeline(Stages: {Options}, Options: Options, Complete: (Result) -> ()): Request
    assert(#Stages==2 and next(Calls)==nil and type(Complete)=="function","[LUI:Process] InvalidPipeline")
    local Down: Session
    local Up: Session
    local Parts: {string} = {}
    local First, Last, At, Bytes = 1, 0, 1, 0
    local Ended, Retrying, Canceled, Completed = false, false, false, false
    local Final: Result? = nil
    local FirstFailure: string? = nil
    local Outputs: {string} = {}
    local Errors: {string} = {}
    local Total = 0
    local Limit=Options.MaxOutputBytes or 65536
    local function Finish()
        local ConsumerResult=Final
        if Completed or not Ended or ConsumerResult==nil then return end
        Completed=true
        local Status=if Canceled then "Canceled" else FirstFailure or ConsumerResult.Status
        Complete({Status=Status, ExitCode=if Status=="Exited" then ConsumerResult.ExitCode else nil,
            Stdout=table.concat(Outputs), Stderr=table.concat(Errors)})
    end
    local function Pump()
        if Retrying or Final or Canceled then return end
        while First<=Last do
            local Text=Parts[First]
            local Piece=string.sub(Text,At,At+4095)
            if not Down:Write(Piece) then
                Retrying=true
                task.delay(0.005,function() Retrying=false; if not Final and (First<=Last or Ended) then Pump() end end)
                return
            end
            At+=#Piece;Bytes-=#Piece
            if At>#Text then Parts[First]=nil;First+=1;At=1 end
        end
        if Ended then Down:CloseInput() end
    end
    local function Output(Stream: string,Text: string)
        if FirstFailure=="OutputLimit" then return end
        Total+=#Text
        if Total>Limit then
            FirstFailure="OutputLimit";Up:Kill();Down:Kill();return
        end
        table.insert(if Stream=="stderr" then Errors else Outputs,Text)
        if Options.OnOutput then Options.OnOutput(Stream,Text) end
    end
    local function Stage(Index: number): Options
        local Source=Stages[Index]
        return {Arguments=Source.Arguments, Environment=Source.Environment or Options.Environment,
            TimeoutMilliseconds=Options.TimeoutMilliseconds, MaxOutputBytes=Limit}
    end
    local Consumer=Stage(2);Consumer.OnOutput=Output
    Down=Process.Open(Consumer,function(Result)
        Final=Result
        if not Ended and Up then Up:Kill() end
        Finish()
    end)
    local Producer=Stage(1);Producer.Input=Options.Input
    Producer.OnOutput=function(Stream,Text)
        if Stream=="stderr" then Output(Stream,Text);return end
        if Final or Canceled then return end
        Bytes+=#Text
        if Bytes>131072 then FirstFailure="OutputLimit";Up:Kill();Down:Kill();return end
        Last+=1;Parts[Last]=Text;Pump()
    end
    local Started,Handle=pcall(Process.Open,Producer,function(Result: Result)
        Ended=true
        if Result.Status~="Exited" and not Final then FirstFailure=FirstFailure or Result.Status; Down:Kill() end
        Pump();Finish()
    end)
    if not Started then Down:Kill();error("[LUI:Process] PipelineStartFailed",0) end
    Up=Handle;Up:CloseInput()
    return table.freeze({Cancel=function(_Self: Request): boolean
        if Canceled or Completed then return false end
        local A,B=Up:Kill(),Down:Kill()
        Canceled=A or B
        return Canceled
    end})
end
return table.freeze(Process)
