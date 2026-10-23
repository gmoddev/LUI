local Counter = require("./Counter")
assert(Counter == require("./Counter"))
local Window = Instance.new("Window", { Title = "Synthetic browser probe", Size = UDim2.fromOffset(640, 360) })
local Button = Instance.new("TextButton", {
    Name = "CounterButton", Text = "Count 0", Size = UDim2.fromOffset(160, 48),
    Position = UDim2.fromOffset(24, 24), Parent = Window,
})
Button.Activated:Connect(function()
    Button.Text = "Count " .. Counter.Next()
end)
Window.Visible = true
