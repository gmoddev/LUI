local State = require('./State')
local Button = require('./Components/Button')
local Home = require('./Screens/Home')
local Library = require('./Screens/Library')
local Window = Instance.new('Window', {Name = 'ModularWorkspace', Size = UDim2.fromOffset(400, 820)})
local Navigation = Instance.new('Frame', {Parent = Window, Name = 'Navigation', Size = UDim2.new(1, -32, 0, 60), Position = UDim2.fromOffset(16, 16)})
Instance.new('UIListLayout', {Parent = Navigation, FillDirection = 'Horizontal', Padding = UDim.new(0, 12)})
local Current: Frame? = nil
local function Show(Screen: string)
    if Current then Current:Destroy() end
    State.Screen = Screen
    Current = if Screen == 'Home' then Home(Window) else Library(Window)
    for _, Child in Current:GetChildren() do
        if Child:IsA('GuiObject') then Child.Size = UDim2.new(1, 0, 0, Child.Size.Y.Offset) end
    end
end
Button(Navigation, 'ShowHome', 'Home', function() Show('Home') end).Size = UDim2.fromOffset(130, 48)
Button(Navigation, 'ShowLibrary', 'Library', function() Show('Library') end).Size = UDim2.fromOffset(130, 48)
Show('Home'); Window.Visible = true
