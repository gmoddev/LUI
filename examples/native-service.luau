local Platform = app:GetService("PlatformService")
assert(Platform:Supports("NativeExtensions"), "NativeExtensions capability is required")

local NativeMath = app:GetService("NativeMath")
local Window = Instance.new("Window", {Title = "LUI Native Service", Size = UDim2.fromOffset(440, 240)})
local Root = Instance.new("Frame", {Size = UDim2.fromScale(1, 1), Parent = Window})
Instance.new("UIPadding", {PaddingTop = UDim.new(0, 20), PaddingLeft = UDim.new(0, 20), Parent = Root})
Instance.new("UIListLayout", {Padding = UDim.new(0, 12), Parent = Root})

local Result = Instance.new("TextLabel", {
    Text = "NativeMath:Add(2, 3) = " .. NativeMath:Add(2, 3),
    Size = UDim2.new(1, -40, 0, 36), Parent = Root,
})
local Button = Instance.new("TextButton", {
    Text = "Call the native service", Size = UDim2.new(1, -40, 0, 40), Parent = Root,
})
Button.Activated:Connect(function()
    Result.Text = NativeMath:Echo("Native service called from Luau")
end)
Window.Visible = true
