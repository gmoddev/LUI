--!strict
-- Native desktop fixture: wheel inside Inner first, then at its end chains to Outer.
local Window = Instance.new('Window', {Title = 'LUI nested wheel qualification', Size = UDim2.fromOffset(540, 460)})
local Outer = Instance.new('ScrollingFrame', {Parent = Window, Position = UDim2.fromOffset(20, 20), Size = UDim2.fromOffset(480, 300), CanvasSize = UDim2.fromOffset(480, 1200), BorderSizePixel = 2}) :: ScrollingFrame
local Inner = Instance.new('ScrollingFrame', {Parent = Outer, Position = UDim2.fromOffset(20, 20), Size = UDim2.fromOffset(380, 180), CanvasSize = UDim2.fromOffset(380, 600), BorderSizePixel = 2}) :: ScrollingFrame
Instance.new('TextLabel', {Parent = Inner, Text = 'Wheel here: inner must move first.\nAt the inner end, the outer consumes the remainder.', Size = UDim2.fromOffset(360, 600)})
local Status = Instance.new('TextLabel', {Parent = Window, Position = UDim2.fromOffset(20, 340), Size = UDim2.fromOffset(480, 40)})
local EndButton = Instance.new('TextButton', {Parent = Window, Text = 'Move inner to end', Position = UDim2.fromOffset(20, 390), Size = UDim2.fromOffset(200, 36)})
local function Update()
    Status.Text = 'InnerY=' .. tostring(Inner.AbsoluteCanvasPosition.Y) .. ' OuterY=' .. tostring(Outer.AbsoluteCanvasPosition.Y)
end
Inner.CanvasChanged:Connect(Update)
Outer.CanvasChanged:Connect(Update)
EndButton.Activated:Connect(function() Inner.CanvasPosition = Vector2.new(0, 1e6); Update() end)
Window.Visible = true
task.defer(Update)
