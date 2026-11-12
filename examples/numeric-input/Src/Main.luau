--!strict
local NumericInput = require('@LuiUI/NumericInput')
local Window = Instance.new('Window', {Title = 'Numeric input example', Size = UDim2.fromOffset(420, 180), Visible = true}) :: Window
local Caption = Instance.new('TextLabel', {Parent = Window, Text = 'Zoom: 100%',
    Position = UDim2.fromOffset(16, 64), Size = UDim2.new(1, -32, 0, 32)}) :: TextLabel
local Zoom = NumericInput.New(Window, {Value = 100, Minimum = 25, Maximum = 400, Step = 25,
    Label = 'Zoom percent', OnChanged = function(Value) Caption.Text = 'Zoom: ' .. tostring(Value) .. '%' end})
Zoom.View.Position = UDim2.fromOffset(16, 16)
Zoom.View.Size = UDim2.new(1, -32, 0, 36)
Instance.new('TextLabel', {Parent = Window, Text = 'Type a number, then press Enter or leave the field.',
    Position = UDim2.fromOffset(16, 104), Size = UDim2.new(1, -32, 0, 32)})
Window.CloseRequested:Connect(function() Zoom:Dispose(); Window:Destroy() end)
