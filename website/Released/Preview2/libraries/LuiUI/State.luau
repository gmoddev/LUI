--!strict
-- State belongs to the application and never creates a second scheduler.
local State = {}

function State.New(Initial: any)
    local Current = Initial
    local Listeners: {[number]: (any) -> ()} = {}
    local NextId = 0
    local Alive = true
    local Dispatching = false
    local Pending = false
    local Value: any = {}
    function Value.Get(_Self: any): any return Current end
    function Value.Set(_Self: any, Next: any)
        assert(Alive, '[LUI:State] Retired')
        if Current == Next then return end
        Current = Next; Pending = true
        if Dispatching then return end
        Dispatching = true
        local Passes = 0
        local Failed = false
        while Pending and Alive do
            Pending = false; Passes += 1
            if Passes > 64 then Failed = true; break end
            local Snapshot = {}
            for Id in Listeners do table.insert(Snapshot, Id) end
            table.sort(Snapshot)
            local Delivered = Current
            for _, Id in Snapshot do
                local Listener = Listeners[Id]
                if Listener and Alive then
                    if not pcall(Listener, Delivered) then Failed = true end
                end
            end
        end
        Dispatching = false; Pending = false
        if Failed then error('[LUI:State] DeliveryFailed', 0) end
    end
    function Value.Subscribe(_Self: any, Callback: (any) -> ())
        assert(Alive and NextId < 1000000, '[LUI:State] SubscriptionLimit')
        local Count = 0; for _ in Listeners do Count += 1 end
        assert(Count < 128, '[LUI:State] SubscriptionLimit')
        NextId += 1; local Id = NextId; Listeners[Id] = Callback
        local Connected = true
        return {Disconnect = function(_Self: any)
            if Connected then Connected = false; Listeners[Id] = nil end
        end}
    end
    function Value.Dispose(_Self: any)
        if not Alive then return end
        Alive = false; Listeners = {}; Pending = false
    end
    return Value
end

return State
