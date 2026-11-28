--!strict
local Commands = require('@LuiWorkbench/Commands')
local Registry = Commands.New()
local Window = Instance.new('Window', {Title = 'Editor shortcut', Size = UDim2.fromOffset(640, 300)})
local Editor = Instance.new('TextBox', {Parent = Window, Multiline = true, Text = 'Write here, then press Ctrl+S.',
    Position = UDim2.fromOffset(20, 20), Size = UDim2.new(1, -40, 0, 150)})
local Status = Instance.new('TextLabel', {Parent = Window, Text = 'Nothing captured yet.',
    Position = UDim2.fromOffset(20, 190), Size = UDim2.new(1, -40, 0, 70)})
local Captured = ''
local Alive = true
Registry:Register({Id = 'Note.Capture', Label = 'Capture note for this session',
    Shortcut = {Key = 'S', Control = true}, AllowWhileEditing = true,
    Execute = function()
        Captured = Editor.Text
        Status.Text = 'Captured ' .. #Captured .. ' UTF-8 bytes for this session.'
    end})
Window.Shortcut:Connect(function(Input) Registry:Dispatch(Input) end)
Window.Destroying:Connect(function() Alive = false; Registry:Dispose() end)
Window.Closed:Connect(function() Window:Destroy() end)
Window.Visible = true
task.defer(function() if Alive then Editor:Focus() end end)
