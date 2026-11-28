--!strict
-- Ordinary bounded bytes, using the host's existing scoped StorageService.
local Preferences = {}
export type Completion = (Bytes: buffer?, Failure: string?) -> ()
export type Operation = {read Cancel: (Self: Operation) -> boolean}
export type Store = {
    Read: (Self: Store, Complete: Completion) -> Operation,
    Write: (Self: Store, Bytes: buffer, Complete: Completion) -> Operation,
    Dispose: (Self: Store) -> (),
}
function Preferences.New(Service: StorageService?, Name: string): Store
    assert(type(Name) == 'string' and #Name >= 1 and #Name <= 64
        and string.match(Name, '^[a-z0-9_-]+$'), '[LUI:Preferences] InvalidName')
    local Alive = true
    local Handle: NativeHandle? = nil
    local Current: any = nil
    local Value: any = {}
    local function Close()
        local Previous = Handle; Handle = nil
        if Previous then Previous:Close() end
    end
    local function Start(Bytes: buffer?, Complete: Completion): Operation
        assert(Alive and Current == nil, '[LUI:Preferences] RetiredOrBusy')
        assert(type(Complete) == 'function', '[LUI:Preferences] InvalidCompletion')
        if Bytes then assert(typeof(Bytes) == 'buffer' and buffer.len(Bytes) <= 8192, '[LUI:Preferences] ByteLimit') end
        -- Capture input now: caller mutation during asynchronous Open cannot change a write.
        local Captured = if Bytes then buffer.fromstring(buffer.tostring(Bytes)) else nil
        local Token: any = {Request = nil, Complete = Complete}
        Current = Token
        local function Finish(Result: buffer?, Failure: string?)
            if not Alive or Current ~= Token then return end
            Current = nil; Token.Request = nil
            local Callback = Token.Complete; Token.Complete = nil
            if Result and buffer.len(Result) > 8192 then Result = nil; Failure = 'ByteLimit' end
            if not pcall(Callback, Result, Failure) then print('[LUI:Preferences] CompletionFailed') end
        end
        local function Active(): boolean return Alive and Current == Token end
        local function Transfer()
            if not Active() then return end
            local Provider = Service
            if not Provider then Finish(nil, 'Unavailable'); return end
            local Ok, Request = pcall(function()
                if Captured then return Provider:Write(Captured, Handle, function(Result, _, Failure) Finish(Result, Failure) end) end
                return Provider:Read(buffer.create(0), Handle, function(Result, _, Failure) Finish(Result, Failure) end)
            end)
            if not Ok then Finish(nil, 'Unavailable')
            elseif Active() then Token.Request = Request end
        end
        if Handle then Transfer()
        elseif not Service then Finish(nil, 'Unavailable')
        else
            local Provider: StorageService = Service
            local Opening = true
            local Ok, Request = pcall(function()
                return Provider:Open(buffer.fromstring(Name), nil, function(_, Opened, Failure)
                    Opening = false
                    if not Active() then if Opened then Opened:Close() end; return end
                    if Failure or not Opened then
                        if Opened then Opened:Close() end
                        Finish(nil, Failure or 'Unavailable'); return
                    end
                    Handle = Opened; Transfer()
                end)
            end)
            if not Ok then Finish(nil, 'Unavailable')
            elseif Opening and Active() then Token.Request = Request end
        end
        local OperationValue: any = {}
        function OperationValue.Cancel(_Self: any): boolean
            if Current ~= Token then return false end
            Current = nil
            local Request = Token.Request; Token.Request = nil; Token.Complete = nil
            if Request then Request:Cancel() end
            Close()
            return true
        end
        return table.freeze(OperationValue) :: Operation
    end
    function Value.Read(_Self: any, Complete: Completion): Operation return Start(nil, Complete) end
    function Value.Write(_Self: any, Bytes: buffer, Complete: Completion): Operation return Start(Bytes, Complete) end
    function Value.Dispose(_Self: any)
        if not Alive then return end
        Alive = false
        local Previous = Current; Current = nil
        if Previous then
            local Request = Previous.Request; Previous.Request = nil; Previous.Complete = nil
            if Request then Request:Cancel() end
        end
        Close()
    end
    return table.freeze(Value) :: Store
end
return table.freeze(Preferences)
