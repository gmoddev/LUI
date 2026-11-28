--!strict
-- Explicit ownership for reusable components and collection bindings.
local Scope = {}

export type Scope = {
    Alive: boolean,
    Add: (Self: Scope, Cleanup: () -> ()) -> (),
    Connect: (Self: Scope, Signal: any, Callback: (...any) -> ()) -> any,
    Own: (Self: Scope, Object: Instance) -> Instance,
    Dispose: (Self: Scope) -> (),
}

function Scope.New(): Scope
    local Alive = true
    local Cleanups: {() -> ()} = {}
    local Value: any = {Alive = true}
    function Value.Add(Self: any, Cleanup: () -> ())
        if not Alive then Cleanup(); return end
        assert(#Cleanups < 128, '[LUI:Scope] OwnershipLimit')
        table.insert(Cleanups, Cleanup)
    end
    function Value.Connect(Self: any, Signal: any, Callback: (...any) -> ()): any
        assert(not Alive or #Cleanups < 128, '[LUI:Scope] OwnershipLimit')
        local Connection = Signal:Connect(function(...: any)
            if Alive then Callback(...) end
        end)
        Self:Add(function() Connection:Disconnect() end)
        return Connection
    end
    function Value.Own(Self: any, Object: Instance): Instance
        Self:Add(function() Object:Destroy() end)
        return Object
    end
    function Value.Dispose(Self: any)
        if not Alive then return end
        Alive = false; Self.Alive = false
        local Retired = Cleanups; Cleanups = {}
        local Failed = false
        for Index = #Retired, 1, -1 do
            if not pcall(Retired[Index]) then Failed = true end
        end
        if Failed then error('[LUI:Scope] CleanupFailed', 0) end
    end
    return Value
end

return Scope
