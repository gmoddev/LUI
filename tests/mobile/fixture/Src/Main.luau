local Platform = app:GetService("PlatformService")
assert(not Platform:Supports("Http"))
local Window = Instance.new("Window", {
    Title = "LUI mobile feasibility", Size = UDim2.fromOffset(360, 640),
})
local Count = 0
local Button = Instance.new("TextButton", {
    Name = "CounterButton", Text = "Count 0", Size = UDim2.fromOffset(200, 56),
    Position = UDim2.fromOffset(24, 24), Parent = Window,
})
Button.Activated:Connect(function()
    Count += 1
    Button.Text = "Count " .. Count
end)
Window.Visible = true
