--!strict
local Utc = require("@Utc/Utc")
local Window = Instance.new("Window", {Title = "UTC timestamp viewer", Size = UDim2.fromOffset(440, 146), Visible = true}) :: Window
local Input = Instance.new("TextBox", {Parent = Window, Text = "2024-02-29T18:15:09+05:30",
    Position = UDim2.fromOffset(12, 12), Size = UDim2.new(1, -24, 0, 34)}) :: TextBox
local Output = Instance.new("TextLabel", {Parent = Window, Position = UDim2.fromOffset(12, 60),
    Size = UDim2.new(1, -24, 0, 48)}) :: TextLabel

local function Update()
    local Ok, Value = pcall(Utc.Parse, Input.Text)
    Output.Text = if Ok then Utc.Format(Value) else "Enter a timestamp with Z or a signed UTC offset."
end

Input.TextChanged:Connect(Update)
Update()
Window.CloseRequested:Connect(function() Window:Destroy() end)
