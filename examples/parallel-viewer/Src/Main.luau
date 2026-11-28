--!strict
local Workers = require("@Workers/Workers")
local Window = Instance.new("Window", {Title = "Parallel viewer", Size = UDim2.fromOffset(500, 210), Visible = true})
local Input = Instance.new("TextBox", {Parent = Window, Text = "", PlaceholderText = "Type while workers compute",
    Position = UDim2.fromOffset(12, 12), Size = UDim2.new(1, -24, 0, 38)}) :: TextBox
local Echo = Instance.new("TextLabel", {Parent = Window, Text = "", Position = UDim2.fromOffset(12, 56),
    Size = UDim2.new(1, -24, 0, 34)})
local Progress = Instance.new("TextLabel", {Parent = Window, Text = "Starting workers", Position = UDim2.fromOffset(12, 96),
    Size = UDim2.new(1, -24, 0, 34)})
local Button = Instance.new("TextButton", {Parent = Window, Text = "Stop", Position = UDim2.fromOffset(12, 146),
    Size = UDim2.fromOffset(100, 36)})
local Actors: {Workers.Actor} = {}
local Starts: {NativeRequest} = {}
local Clock = app:GetService("PerformanceService")
local LastDisplay = -100
local Running, Batches = true, 0
local function Stop()
    if not Running then return end
    Running = false
    for _, Request in Starts do Request:Cancel() end
    for _, Worker in Actors do Worker:Close() end
    table.clear(Starts)
    table.clear(Actors)
    Progress.Text = "Stopped"
end
local function Submit(Worker: Workers.Actor, Seed: number)
    Worker:Send(Seed, function(Result, Failure)
        if not Running then return end
        if Failure then Stop(); Progress.Text = Failure; return end
        Batches += 1
        local Now = Clock:GetTime()
        if Now - LastDisplay >= 100 then
            LastDisplay = Now
            Progress.Text = "Batches " .. tostring(Batches)
        end
        Submit(Worker, Result)
    end)
end
Input.TextChanged:Connect(function() Echo.Text = "Edited " .. Input.Text end)
Button.Activated:Connect(Stop)
Window.CloseRequested:Connect(function() Stop(); Window:Destroy() end)
for Index = 1, 2 do
    table.insert(Starts, Workers.Start("./Compute", {}, function(Worker, Failure)
        if not Running then if Worker then Worker:Close() end; return end
        if not Worker then Stop(); Progress.Text = Failure or "Worker failed"; return end
        table.insert(Actors, Worker)
        Submit(Worker, Index)
    end))
end
