--!strict
local Stdio = {Version = "0.1.0", RequiredCapabilities = table.freeze({"HostServices"})}
export type Completion = (Status: string, Bytes: string) -> ()
local Pending: {[number]: Completion} = {}
local Listening = false

local function Service(Complete: Completion)
    if type(Complete) ~= "function" then error("[LUI:Stdio] CallbackRequired", 0) end
    local Host = app:GetService("Stdio")
    if not Listening then
        Host.Completed:Connect(function(Id: number, Status: string, Bytes: string)
            local Callback = Pending[Id]
            Pending[Id] = nil
            if Callback then Callback(Status, Bytes) end
        end)
        Listening = true
    end
    return Host
end

local function Track(Id: number, Callback: Completion): number
    if Id == 0 then error("[LUI:Stdio] StreamBusy", 0) end
    Pending[Id] = Callback
    return Id
end

function Stdio.Read(MaxBytes: number, Callback: Completion): number
    return Track(Service(Callback):Read(MaxBytes, false), Callback)
end

function Stdio.ReadLine(MaxBytes: number, Callback: Completion): number
    return Track(Service(Callback):Read(MaxBytes, true), Callback)
end

function Stdio.Write(Bytes: string, Callback: Completion): number
    return Track(Service(Callback):Write(1, Bytes), Callback)
end

function Stdio.WriteError(Bytes: string, Callback: Completion): number
    return Track(Service(Callback):Write(2, Bytes), Callback)
end

function Stdio.Cancel(Id: number): boolean
    return app:GetService("Stdio"):Cancel(Id)
end

function Stdio.Arguments(): {string}
    local Host = app:GetService("Stdio")
    local Values: {string} = {}
    for Index = 1, Host:ArgumentCount() do table.insert(Values, Host:Argument(Index)) end
    return Values
end

function Stdio.IsTerminal(Stream: string): boolean
    local Index = if Stream == "stdin" then 0 elseif Stream == "stdout" then 1 elseif Stream == "stderr" then 2 else -1
    if Index < 0 then error("[LUI:Stdio] UnknownStream", 0) end
    return app:GetService("Stdio"):IsTerminal(Index)
end

function Stdio.SetExitStatus(Code: number)
    app:GetService("Stdio"):SetExitStatus(Code)
end

return table.freeze(Stdio)
