// These examples use the released runtime surface. New diagnostics are described
// separately so a documentation update cannot silently upgrade an SDK project.
export function Recipes() {
  return [
    { Name: 'Recipe-Images', Title: 'A declared image', Assets: {
      'Pixel.png': Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=', 'base64'),
    }, Files: { 'Main.luau': `local Window = Instance.new("Window", {Title = "A bundled image", Size = UDim2.fromOffset(440, 240)})
local Assets = app:GetService("AssetService")
assert(Assets:Has("Pixel.png"), "Pixel.png must be declared")
Instance.new("ImageLabel", {
    Parent = Window, Source = "Pixel.png",
    Position = UDim2.fromOffset(24, 24), Size = UDim2.fromOffset(96, 96),
})
Instance.new("TextLabel", {
    Parent = Window, Text = "The dark square is a declared PNG image.",
    Position = UDim2.fromOffset(24, 140), Size = UDim2.fromOffset(390, 50),
})
Window.Visible = true
` } },
    { Name: 'Recipe-Time', Title: 'Wall clock and elapsed time', Files: { 'Main.luau': `local Window = Instance.new("Window", {Title = "Time", Size = UDim2.fromOffset(480, 240)})
local Performance = app:GetService("PerformanceService")
local Started = Performance:GetTime()
local Clock = Instance.new("TextLabel", {
    Parent = Window, Text = os.date("!%Y-%m-%d %H:%M:%S UTC"),
    Position = UDim2.fromOffset(24, 24), Size = UDim2.fromOffset(420, 44),
})
local Elapsed = Instance.new("TextLabel", {
    Parent = Window, Text = "Click Update to measure elapsed time.",
    Position = UDim2.fromOffset(24, 76), Size = UDim2.fromOffset(420, 44),
})
local Update = Instance.new("TextButton", {Parent = Window, Text = "Update", Position = UDim2.fromOffset(24, 140)})
Update.Activated:Connect(function()
    Clock.Text = os.date("!%Y-%m-%d %H:%M:%S UTC")
    Elapsed.Text = tostring(math.floor(Performance:GetTime() - Started)) .. " ms since opening"
end)
Window.Visible = true
` } },
    { Name: 'Recipe-RightClick', Title: 'A custom right-click menu', Files: { 'Main.luau': `local Window = Instance.new("Window", {Title = "Right-click input", Size = UDim2.fromOffset(520, 360)})
local Surface = Instance.new("Frame", {
    Parent = Window, Position = UDim2.fromOffset(24, 24), Size = UDim2.fromOffset(460, 230),
    BackgroundColor3 = Color3.fromRGB(225, 235, 240),
})
local Status = Instance.new("TextLabel", {
    Parent = Surface, Text = "Right-click this area, or use Open menu.",
    Position = UDim2.fromOffset(12, 12), Size = UDim2.fromOffset(420, 40),
})
local Menu = Instance.new("Frame", {Parent = Surface, Visible = false, Size = UDim2.fromOffset(180, 48), ZIndex = 10})
local Choice = Instance.new("TextButton", {Parent = Menu, Text = "Choose this action", Size = UDim2.fromOffset(180, 48)})
local Open = Instance.new("TextButton", {Parent = Window, Text = "Open menu", Position = UDim2.fromOffset(24, 280)})
local function ShowMenu(X: number, Y: number)
    Menu.Position = UDim2.fromOffset(math.clamp(X, 0, 280), math.clamp(Y, 0, 182))
    Menu.Visible = true
    Choice:Focus()
end
Surface.InputEnded:Connect(function(Input)
    if Input.Device == "Mouse" and Input.Button == "Right" and not Input.IsCanceled then
        ShowMenu(Input.Position.X, Input.Position.Y)
    end
end)
Open.Activated:Connect(function() ShowMenu(12, 64) end)
Choice.Activated:Connect(function()
    Status.Text = "Action selected"
    Menu.Visible = false
    Open:Focus()
end)
Choice.InputBegan:Connect(function(Input)
    if Input.Device == "Keyboard" and Input.Key == "Escape" then
        Menu.Visible = false
        Open:Focus()
    end
end)
Window.Visible = true
` } },
    { Name: 'Recipe-Http', Title: 'A local HTTP client and server', Capabilities: ['network.client', 'network.server'],
      NetworkPolicy: { ClientLoopbackOnly: true, ServerLoopbackOnly: true, ClientPortMin: 0, ClientPortMax: 0, ServerPortMin: 0, ServerPortMax: 0 },
      Files: { 'Main.luau': `local Http = app:GetService("HttpService")
local Server = app:GetService("HttpServerService"):CreateServer({Port = 0})
Server:Route("GET", "/status", function(Request)
    return {StatusCode = 200, Body = "Hello from your local server"}
end)
Server:Start()
local Window = Instance.new("Window", {Title = "Local HTTP", Size = UDim2.fromOffset(540, 240)})
local Status = Instance.new("TextLabel", {
    Parent = Window, Text = "Click Send request. Port: " .. tostring(Server.Port),
    Position = UDim2.fromOffset(24, 24), Size = UDim2.fromOffset(480, 70),
})
local Send = Instance.new("TextButton", {Parent = Window, Text = "Send request", Position = UDim2.fromOffset(24, 120)})
Send.Activated:Connect(function()
    task.spawn(function()
        local Ok, Response = pcall(function()
            return Http:RequestAsync({Url = "http://127.0.0.1:" .. tostring(Server.Port) .. "/status", TimeoutMs = 5000, MaxResponseBytes = 1024})
        end)
        if Window.IsClosed then return end
        if Ok then Status.Text = Response.Body else Status.Text = "Request failed. Check local host support and grants." end
    end)
end)
Window.Closed:Connect(function() Server:Close() end)
Window.Visible = true
` } },
  ];
}
