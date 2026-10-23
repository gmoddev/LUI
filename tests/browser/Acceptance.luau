-- Identical captured source is executed by native parity and every WASM engine.
local VirtualList = require('./VirtualList')
local Counter = require('./Counter')
assert(Counter == require('./Counter'), 'ModuleCache')
local Window = Instance.new('Window', {Title = 'Acceptance', Size = UDim2.fromOffset(900, 760)})
local State = Instance.new('TextLabel', {Parent = Window, AccessibilityLabel = 'State', Text = 'Starting', Size = UDim2.fromOffset(360, 32)})
local PointerState = Instance.new('TextLabel', {Parent = Window, AccessibilityLabel = 'Pointer state', Position = UDim2.fromOffset(380, 0), Size = UDim2.fromOffset(480, 32)})
local Pointer = Instance.new('TextButton', {Parent = Window, Text = 'Pointer target', Position = UDim2.fromOffset(420, 80), Size = UDim2.fromOffset(140, 44)})
local Began, Ended, Canceled, Activated = 0, 0, 0, 0
Pointer.InputBegan:Connect(function(Input)
    if Input.Device ~= 'Mouse' then return end
    Began += 1
    PointerState.Text = 'Pointer ' .. math.floor(Input.Position.X) .. '/' .. math.floor(Input.Position.Y)
end)
Pointer.InputEnded:Connect(function(Input)
    if Input.Device ~= 'Mouse' then return end
    Ended += 1; if Input.IsCanceled then Canceled += 1 end
    PointerState.Text = 'Ended ' .. Began .. '/' .. Ended .. '/' .. Canceled
end)
Pointer.Activated:Connect(function() Activated += 1; State.Text = 'Activated ' .. Activated end)
local function Command(Name, X, Callback)
    local Button = Instance.new('TextButton', {Parent = Window, Text = Name, Position = UDim2.fromOffset(X, 140), Size = UDim2.fromOffset(130, 36)})
    Button.Activated:Connect(Callback)
    return Button
end
Command('Move target', 420, function() Pointer.Position = UDim2.fromOffset(440, 80); State.Text = 'Moved' end)
local First = Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'First', Position = UDim2.fromOffset(8, 200), Size = UDim2.fromOffset(160, 40), MaxLength = 8})
Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'Hidden', Visible = false, Position = UDim2.fromOffset(8, 250), Size = UDim2.fromOffset(160, 40)})
Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'Disabled', Enabled = false, Position = UDim2.fromOffset(8, 300), Size = UDim2.fromOffset(160, 40)})
Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'Second', Position = UDim2.fromOffset(180, 200), Size = UDim2.fromOffset(160, 40)})
Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'Lines', Multiline = true, Position = UDim2.fromOffset(8, 250), Size = UDim2.fromOffset(330, 70)})
local Holder = Instance.new('Frame', {Parent = Window, Size = UDim2.fromOffset(360, 340)})
-- Keep this noninteractive parent behind the editing controls.
Holder.ZIndex = -1
Command('Reparent field', 560, function() First.Parent = Holder; State.Text = 'Reparented' end)
Command('Remove field', 700, function() First:Destroy(); State.Text = 'Removed' end)
local Outer = Instance.new('ScrollingFrame', {Parent = Window, AccessibilityLabel = 'Outer', Position = UDim2.fromOffset(8, 360), Size = UDim2.fromOffset(300, 200), CanvasSize = UDim2.fromOffset(300, 800)})
local Inner = Instance.new('ScrollingFrame', {Parent = Outer, AccessibilityLabel = 'Inner', Position = UDim2.fromOffset(20, 20), Size = UDim2.fromOffset(200, 120), CanvasSize = UDim2.fromOffset(200, 600)})
Instance.new('TextLabel', {Parent = Inner, Text = 'Nested top', Size = UDim2.fromOffset(180, 36)})
Instance.new('TextLabel', {Parent = Inner, Text = 'Nested bottom', Position = UDim2.fromOffset(0, 560), Size = UDim2.fromOffset(180, 36)})
local Lower = Instance.new('TextButton', {Parent = Window, Text = 'Lower', Position = UDim2.fromOffset(420, 260), Size = UDim2.fromOffset(140, 44)})
local Upper = Instance.new('TextButton', {Parent = Window, Text = 'Upper', ZIndex = 5, Position = UDim2.fromOffset(420, 260), Size = UDim2.fromOffset(140, 44)})
Lower.Activated:Connect(function() State.Text = 'Lower activated' end)
Upper.Activated:Connect(function() State.Text = 'Upper activated' end)
local Viewport = Instance.new('ScrollingFrame', {Parent = Window, AccessibilityLabel = 'Collection', Position = UDim2.fromOffset(420, 360), Size = UDim2.fromOffset(300, 160)})
local Items = {}
local FirstRow
for Index = 1, 200 do Items[Index] = {Id = tostring(Index)} end
local List = VirtualList.New(Viewport, {
    MaxRows = 12, Overscan = 1, GetKey = function(Item) return Item.Id end,
    GetHeight = function() return 40 end,
    Render = function(Parent, Item, Binding, Width)
        local Button = Instance.new('TextButton', {Parent = Parent, Text = 'Row ' .. Item.Id, Size = UDim2.fromOffset(Width, 36)})
        if Item.Id == '1' then FirstRow = Button end
        Binding:Own(Button)
        Binding:Connect(Button.Activated, function() State.Text = 'Row activated ' .. Item.Id end)
    end,
})
List:SetItems(Items)
Command('Insert row', 8, function()
    local Focused = FirstRow and FirstRow.IsFocused
    table.insert(Items, 1, {Id = 'Inserted'}); List:SetItems(Items)
    assert(not Focused or FirstRow.IsFocused, 'ResidentFocus')
    State.Text = 'Inserted'
end)
Command('Retire rows', 148, function() Viewport.CanvasPosition = Vector2.new(0, 1e6); List:Refresh(); State.Text = 'Scrolled' end)
Window.Destroying:Connect(function() List:Dispose() end)

-- Shared reflection, object identity, signal and destruction expectations.
assert(First:IsA('GuiObject') and First.ClassName == 'TextBox' and First.MaxLength == 8, 'Reflection')
assert(not pcall(function() First.MaxLength = 4294967296 end), 'Narrowing')
assert(not pcall(function() First.Text = '\255' end), 'Utf8')
local Original = Instance.new('Frame', {Name = 'Original'})
local Child = Instance.new('TextLabel', {Parent = Original, Name = 'Child', Text = 'Clone value'})
local Copy = Original:Clone()
assert(Copy ~= Original and Copy:FindFirstChild('Child').Text == Child.Text, 'Clone')
local Changes = 0
local Connection = Child.Changed:Connect(function() Changes += 1 end)
Child.Text = 'Changed'; assert(Changes == 1, 'SignalOrder')
Connection:Disconnect(); Child.Text = 'Disconnected'; assert(Changes == 1, 'Disconnect')
Original:Destroy(); Original:Destroy(); Copy:Destroy()
Window.Visible = true
task.defer(function() State.Text = 'Shared accepted' end)
