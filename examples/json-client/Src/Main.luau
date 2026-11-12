--!strict
local Json = require('@Json/Json')
local JsonClient = require('@JsonClient/JsonClient')
local Server = app:GetService('HttpServerService'):CreateServer({Port = 0})
Server:Route('GET', '/status', function()
    return {Headers = {{Name = 'Content-Type', Value = 'application/json'}},
        Body = Json.Encode({Status = 'Ready', Count = 1})}
end)
Server:Start()
local Client = JsonClient.Open()
local Window = Instance.new('Window', {Title = 'JSON HTTP example', Size = UDim2.fromOffset(520, 220)})
local Status = Instance.new('TextLabel', {Parent = Window, Text = 'Requesting local status...',
    Position = UDim2.fromOffset(20, 20), Size = UDim2.fromOffset(480, 80), TextWrapped = true})
local Alive = true
Window.Destroying:Connect(function()
    Alive = false
    Server:Close()
    app:GetService('HttpService'):CancelAll()
end)
Window.Visible = true
task.spawn(function()
    local Ok, Data = pcall(function()
        local Result, Response = Client:GetAsync('http://127.0.0.1:' .. Server.Port .. '/status')
        assert(Response.Success)
        return Result
    end)
    if not Alive then return end
    Status.Text = if Ok then 'Local JSON status: ' .. tostring((Data :: any).Status) else 'The local status request failed.'
    Server:Close()
end)
