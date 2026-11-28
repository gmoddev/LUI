--!strict
local Json = require('@Json/Json')
local JsonClient = require('@JsonClient/JsonClient')
local Http = app:GetService('HttpService')
local WebSockets = app:GetService('WebSocketService')
local Headless = app:GetService('PlatformService').BackendName == 'headless'
local Server = app:GetService('HttpServerService'):CreateServer({Port = if Headless then 18082 else 0})
local Requests = 0
local Messages = 0
Server:Route('POST', '/echo', function(Request)
    local Value = Json.Decode(Request.Body)
    Requests += 1
    return {Headers = {{Name = 'Content-Type', Value = 'application/json'}},
        Body = Json.Encode({Message = (Value :: any).Message, Requests = Requests})}
end)
Server:Route('GET', '/broken', function() return {Body = '{invalid json'} end)
Server:RouteWebSocket('/echo', function(Socket)
    local Ok = pcall(function()
        while true do
            local Message = Socket:ReceiveAsync()
            if not Message then break end
            Messages += 1
            Socket:SendAsync(Message.Data, Message.Binary)
        end
    end)
    if Ok then Socket:Close() else Socket:Cancel() end
end)
Server:Start()
local Client = JsonClient.Open()
local Origin = 'http://127.0.0.1:' .. Server.Port
local SocketUrl = 'ws://127.0.0.1:' .. Server.Port .. '/echo'
if Headless then
    task.spawn(function()
        local Socket: WebSocket? = nil
        local Ok = pcall(function()
            local Data, Response = Client:RequestAsync(Origin .. '/echo', {Method = 'POST', Value = {Message = 'SDK headless HTTP'}})
            assert(Response.Success and (Data :: any).Message == 'SDK headless HTTP' and Requests == 1)
            local Token = Http:CreateCancellationToken()
            Token:Cancel()
            local Cancelled, Failure = pcall(function()
                Client:RequestAsync(Origin .. '/echo', {Method = 'POST', Value = {Message = 'Must not arrive'}, CancellationToken = Token})
            end)
            assert(not Cancelled and type(Failure) == 'string' and string.match(Failure, '%[LUI:Http%] Canceled$'))
            assert(Requests == 1)
            Data = Client:RequestAsync(Origin .. '/echo', {Method = 'POST', Value = {Message = 'Unrelated request'}})
            assert((Data :: any).Message == 'Unrelated request' and Requests == 2)
            local Connected = WebSockets:ConnectAsync(SocketUrl)
            Socket = Connected
            Connected:SendAsync(Json.Encode({Message = 'SDK headless WebSocket'}))
            local Message = Connected:ReceiveAsync()
            assert(Message and not Message.Binary and (Json.Decode(Message.Data) :: any).Message == 'SDK headless WebSocket')
            assert(Messages == 1)
            Connected:Close(); assert(Connected:ReceiveAsync() == nil)
        end)
        if Socket then Socket:Cancel() end
        Server:Close()
        assert(Ok, '[LUI:NetworkDesk] HeadlessJourneyFailed')
        print('[LUI:NetworkDesk] HTTP, token isolation and WebSocket traffic passed')
    end)
    return
end
local Network = app:GetService('NetworkService')
local Window = Instance.new('Window', {Name = 'NetworkDesk', Title = 'Network Desk', Size = UDim2.fromOffset(760, 440)})
local Root = Instance.new('Frame', {Parent = Window})
Instance.new('TextLabel', {Parent = Root, Text = 'Network Desk', TextSize = 24, FontWeight = 'Bold',
    Position = UDim2.fromOffset(20, 16), Size = UDim2.new(1, -40, 0, 40)})
local Input = Instance.new('TextBox', {Parent = Root, Name = 'Message', Text = 'Hello from the SDK', MaxLength = 1024,
    Position = UDim2.fromOffset(20, 68), Size = UDim2.new(1, -40, 0, 42)})
local Status = Instance.new('TextLabel', {Parent = Root, Name = 'Status', Text = 'Local HTTP server is listening on port ' .. Server.Port .. '.',
    TextWrapped = true, TextXAlignment = 'Left', TextYAlignment = 'Top', Position = UDim2.fromOffset(20, 180), Size = UDim2.new(1, -40, 1, -250)})
Instance.new('TextLabel', {Parent = Root, Text = 'Cancel stops the current action. The local server stays available for another request.',
    TextWrapped = true, Position = UDim2.new(0, 20, 1, -64), Size = UDim2.new(1, -40, 0, 52)})
local Alive, Busy = true, false
local Current: HttpCancellationToken? = nil
local ActiveSocket: WebSocket? = nil
local Listener: TcpListener? = nil
local Peer: TcpConnection? = nil
local function CloseSlow()
    local OldListener, OldPeer = Listener, Peer
    Listener = nil; Peer = nil
    if OldPeer then OldPeer:Close() end
    if OldListener then OldListener:Close() end
end
local function Request(Action: (Token: HttpCancellationToken) -> string)
    if Busy then Status.Text = 'A request is running. Use Cancel first.'; return end
    local Created, Value = pcall(function() return Http:CreateCancellationToken() end)
    if not Created then CloseSlow(); Status.Text = 'The request could not start. Wait for earlier work to finish, then retry.'; return end
    local Token = Value :: HttpCancellationToken
    Busy = true; Status.Text = 'Request in progress. The window stays usable.'
    Current = Token
    task.spawn(function()
        if not Alive then return end
        local Ok, Result = pcall(function() return Action(Token) end)
        if not Alive then return end
        CloseSlow(); Busy = false; Current = nil
        if ActiveSocket then ActiveSocket:Cancel(); ActiveSocket = nil end
        Status.Text = if Ok then Result else 'Request failed or was cancelled. You can retry.'
    end)
end
local function Button(Name: string, Text: string, Index: number, Action: () -> ())
    local Value = Instance.new('TextButton', {Parent = Root, Name = Name, Text = Text, TextSize = 13,
        Position = UDim2.new((Index - 1) / 6, 20, 0, 126), Size = UDim2.new(1 / 6, -24, 0, 38)})
    Value.Activated:Connect(Action)
end
Button('Echo', 'Send JSON', 1, function()
    local Message = Input.Text
    Request(function(Token)
        local Result, Response = Client:RequestAsync(Origin .. '/echo', {Method = 'POST', Value = {Message = Message}, CancellationToken = Token})
        assert(Response.Success and (Result :: any).Message == Message)
        return 'Server received and echoed: ' .. tostring((Result :: any).Message) .. '\nReal requests handled: ' .. Requests
    end)
end)
Button('Https', 'Test HTTPS', 2, function()
    Request(function(Token)
        local Response = Http:RequestAsync({Url = 'https://example.com/', TimeoutMs = 10000, CancellationToken = Token})
        assert(Response.Success)
        return 'HTTPS completed with certificate validation. Status ' .. Response.StatusCode .. ', body bytes ' .. #Response.Body
    end)
end)
Button('Broken', 'Invalid JSON', 3, function()
    Request(function(Token)
        local Ok, Failure = pcall(function() Client:RequestAsync(Origin .. '/broken', {CancellationToken = Token}) end)
        assert(not Ok and Failure == '[LUI:Json] InvalidJson')
        return 'Invalid JSON was rejected. Send JSON still works.'
    end)
end)
Button('Slow', 'Slow request', 4, function()
    if Busy then Status.Text = 'A request is already running.'; return end
    local Slow = Network:ListenTcp({Port = 0})
    Listener = Slow
    task.spawn(function()
        local Ok, Accepted = pcall(function() return Slow:AcceptAsync() end)
        if not Ok then return end
        if not Alive or Listener ~= Slow then Accepted:Close(); return end
        Peer = Accepted
        -- Deliberately withhold an HTTP response without blocking the scheduler.
        task.delay(12, function() if Listener == Slow then CloseSlow() end end)
    end)
    Request(function(Token)
        Http:RequestAsync({Url = 'http://127.0.0.1:' .. Slow.Port .. '/', TimeoutMs = 10000, CancellationToken = Token})
        return 'Slow request completed.'
    end)
end)
Button('Cancel', 'Cancel', 5, function()
    if Current then Current:Cancel() end
    CloseSlow()
    Status.Text = 'Cancellation requested for this action. The server and unrelated requests remain available.'
end)
Button('WebSocket', 'WebSocket', 6, function()
    local Text = Input.Text
    Request(function(Token)
        local Socket = WebSockets:ConnectAsync(SocketUrl, {CancellationToken = Token})
        if not Alive or Token.IsCanceled then Socket:Cancel(); error('[LUI:NetworkDesk] Cancelled', 0) end
        ActiveSocket = Socket
        Socket:SendAsync(Json.Encode({Message = Text}))
        local Message = Socket:ReceiveAsync()
        assert(Message and not Message.Binary and (Json.Decode(Message.Data) :: any).Message == Text)
        Socket:Close(); assert(Socket:ReceiveAsync() == nil)
        return 'WebSocket received and echoed: ' .. Text .. '\nReal messages handled: ' .. Messages
    end)
end)
local function Retire()
    if not Alive then return end
    Alive = false
    if Current then Current:Cancel(); Current = nil end
    if ActiveSocket then ActiveSocket:Cancel(); ActiveSocket = nil end
    CloseSlow(); Server:Close()
end
Window.Closed:Connect(function() Retire(); Window:Destroy() end)
Window.Destroying:Connect(Retire)
Window.Visible = true
