--!strict
local Commands = require('@LuiWorkbench/Commands')
local Registry = Commands.New()
local Window = Instance.new('Window', {Title = 'LUI Commands', Size = UDim2.fromOffset(540, 260)})
local Status = Instance.new('TextLabel', {Parent = Window, Text = 'Choose a command', Position = UDim2.fromOffset(20, 20), Size = UDim2.fromOffset(490, 50)})
local Count = 0
Registry:Register({Id = 'Example.Increment', Label = 'Increment', Shortcut = {Key = 'I', Control = true},
    Execute = function() Count += 1; Status.Text = 'Command ran ' .. Count .. ' times' end})
local Button = Instance.new('TextButton', {Parent = Window, Text = 'Increment (Ctrl+I)', Position = UDim2.fromOffset(20, 90), Size = UDim2.fromOffset(260, 42)})
local Activated = Button.Activated:Connect(function() Registry:Execute('Example.Increment') end)
local Shortcut = Window.Shortcut:Connect(function(Input) Registry:Dispatch(Input) end)
Window.Destroying:Connect(function() Activated:Disconnect(); Shortcut:Disconnect(); Registry:Dispose() end)
Window.Visible = true
