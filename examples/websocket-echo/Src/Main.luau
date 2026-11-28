--!strict
-- Finite client/server journey; port18083 is declared in the example's host policy.
local Server = app:GetService('HttpServerService'):CreateServer({Port = 18083})
Server:RouteWebSocket('/echo', function(Socket)
    local Ok = pcall(function()
        local Message = Socket:ReceiveAsync()
        if Message then Socket:SendAsync(Message.Data, Message.Binary) end
        Socket:Close()
        Socket:ReceiveAsync()
    end)
    if not Ok then Socket:Cancel() end
end)
Server:Start()
task.spawn(function()
    local Socket: WebSocket? = nil
    local Ok = pcall(function()
        local Connected = app:GetService('WebSocketService'):ConnectAsync('ws://127.0.0.1:18083/echo')
        Socket = Connected
        Connected:SendAsync('Hello from the public SDK')
        local Message = Connected:ReceiveAsync()
        assert(Message and not Message.Binary and Message.Data == 'Hello from the public SDK')
        Connected:Close(); assert(Connected:ReceiveAsync() == nil)
    end)
    if Socket then Socket:Cancel() end
    Server:Close()
    assert(Ok, '[LUI:Echo] JourneyFailed')
    print('[LUI:Echo] Real client/server WebSocket echo passed')
end)
