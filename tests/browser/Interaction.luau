local Window = Instance.new('Window', {Title = 'LUI browser qualification', Size = UDim2.fromOffset(640, 460)})
local Output = Instance.new('TextLabel', {Parent = Window, Text = 'Ready', AccessibilityLabel = 'Qualification output', Position = UDim2.fromOffset(16, 16), Size = UDim2.fromOffset(600, 40)})
local Button = Instance.new('TextButton', {Parent = Window, Text = 'Qualification action', AccessibilityDescription = 'Synthetic qualification action', Position = UDim2.fromOffset(16, 64), Size = UDim2.fromOffset(240, 40)})
local Text = Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'Qualification input', AccessibilityDescription = 'Synthetic qualification input', Position = UDim2.fromOffset(16, 120), Size = UDim2.fromOffset(460, 44), MaxLength = 64})
local Trace = Instance.new('TextLabel', {Parent = Window, AccessibilityLabel = 'Qualification trace', Position = UDim2.fromOffset(16, 180), Size = UDim2.fromOffset(600, 40)})
local Started, Changed, Ended = 0, 0, 0
local BeganKeys, EndedKeys = {}, {}
Text.CompositionStarted:Connect(function() Started += 1 end)
Text.CompositionChanged:Connect(function() Changed += 1 end)
Text.CompositionEnded:Connect(function() Ended += 1 end)
Text.InputBegan:Connect(function(Input) if Input.Device == 'Keyboard' then table.insert(BeganKeys, Input.Key) end end)
Text.InputEnded:Connect(function(Input) if Input.Device == 'Keyboard' then table.insert(EndedKeys, Input.Key) end end)
Text.TextChanged:Connect(function() Output.Text = Text.Text end)
Button.Activated:Connect(function()
    Trace.Text = 'Composition ' .. Started .. '/' .. Changed .. '/' .. Ended .. ' | keys ' .. table.concat(BeganKeys, ',') .. '/' .. table.concat(EndedKeys, ',')
    Output.Text = 'Activated qualification action'
end)
Window.Visible = true
