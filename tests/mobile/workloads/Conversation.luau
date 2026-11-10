--!strict
-- Phone entry for the shared pinned Scope/State/VirtualList application profile.
local Scope = require('@LuiUI/Scope')
local State = require('@LuiUI/State')
local VirtualList = require('@LuiUI/VirtualList')
local Owner = Scope.New()
local Settings = State.New({Page = 'History'})
Owner:Add(function() Settings:Dispose() end)
local Window = Instance.new('Window', {Name = 'Conversation', Size = UDim2.fromOffset(400, 820)})
local Toolbar = Instance.new('Frame', {Parent = Window, Size = UDim2.new(1, -16, 0, 100), Position = UDim2.fromOffset(8, 8)})
local Viewport = Instance.new('ScrollingFrame', {Parent = Window, Name = 'History', Position = UDim2.fromOffset(8, 116), Size = UDim2.new(1, -16, 1, -270), ClipsDescendants = true})
local Composer = Instance.new('TextBox', {Parent = Window, Name = 'Composer', Multiline = true, MaxLength = 4096, Position = UDim2.new(0, 8, 1, -146), Size = UDim2.new(1, -100, 0, 70)})
local Status = Instance.new('TextLabel', {Parent = Window, Name = 'WorkloadStatus', Position = UDim2.new(0, 8, 1, -68), Size = UDim2.new(1, -16, 0, 60), TextSize = 12})
local SettingsPage = Instance.new('Frame', {Parent = Window, Name = 'SettingsPage', Visible = false, Position = UDim2.fromOffset(8, 116), Size = UDim2.new(1, -16, 1, -190)})
local Secret = Instance.new('TextBox', {Parent = SettingsPage, Name = 'WorkloadSecret', Secret = true, MaxLength = 64, Size = UDim2.new(1, -100, 0, 48)})
local Images = Instance.new('Frame', {Parent = SettingsPage, Name = 'PressureImages', Position = UDim2.fromOffset(0, 56), Size = UDim2.new(1, 0, 1, -56)})
local Items: {any} = {}
local NextId = 10001
local List: any
local Count = 0
local function StatusText()
    if not List then return end
    local Stats = List:GetStats()
    local Anchor = List:GetAnchor()
    Status.Text = tostring(#Items) .. ' items | ' .. Stats.LiveRows .. ' rows | ' .. Stats.PooledRows .. ' pooled | ' .. Count .. ' actions'
    Status.AccessibilityLabel = Anchor and Anchor.Key or ''
end
local function Update(Follow: boolean?) List:SetItems(Items, Follow); Count += 1; StatusText() end
local function AnchorIndex(): number
    local Anchor = List:GetAnchor()
    if Anchor then for Index, Item in Items do if Item.Id == Anchor.Key then return Index end end end
    return 1
end
local function Button(Parent: Instance, Name: string, Text: string, X: number, Y: number, Callback: () -> ())
    local Value = Instance.new('TextButton', {Parent = Parent, Name = Name, Text = Text, Position = UDim2.fromOffset(X, Y), Size = UDim2.fromOffset(88, 48), TextSize = 12})
    Owner:Connect(Value.Activated, Callback)
    return Value
end
Button(Toolbar, 'Insert', 'Insert', 0, 0, function()
    if not List then return end
    table.insert(Items, 1, {Id = 'Message-' .. NextId, Text = 'Inserted synthetic row', Height = 88, Draft = ''})
    NextId += 1; Update()
end)
Button(Toolbar, 'Edit', 'Edit', 92, 0, function()
    if not List then return end
    local Index = AnchorIndex(); local Previous = Items[Index]
    -- Retain item identity and the resident editor while remeasuring height.
    Previous.Text ..= ' edited'; Previous.Height = Previous.Height == 120 and 88 or 120
    List:Invalidate(); Count += 1; StatusText()
end)
Button(Toolbar, 'Delete', 'Delete', 184, 0, function() if List then table.remove(Items, AnchorIndex()); Update() end end)
Button(Toolbar, 'Append', 'Append', 276, 0, function()
    if not List then return end
    table.insert(Items, {Id = 'Message-' .. NextId, Text = 'Appended synthetic row', Height = 88, Draft = ''})
    NextId += 1; Update(true)
end)
Button(Toolbar, 'Top', 'Top', 0, 52, function() if List then Viewport.CanvasPosition = Vector2.new(0, 0); List:Refresh(); StatusText() end end)
Button(Toolbar, 'Bottom', 'Bottom', 92, 52, function() if List then Viewport.CanvasPosition = Vector2.new(0, 1e9); List:Refresh(); StatusText() end end)
Button(Toolbar, 'HistoryPage', 'History', 184, 52, function() Settings:Set({Page = 'History'}) end)
Button(Toolbar, 'SettingsPageButton', 'Settings', 276, 52, function() Settings:Set({Page = 'Settings'}) end)
local Send = Button(Window, 'Send', 'Send', 0, 0, function()
    if not List or Composer.Text == '' then return end
    table.insert(Items, {Id = 'Message-' .. NextId, Text = Composer.Text, Height = 88, Draft = ''})
    NextId += 1; Composer.Text = ''; Update(true)
end)
Send.Position = UDim2.new(1, -92, 1, -146)
local Subscription = Settings:Subscribe(function(Value: any)
    Viewport.Visible = Value.Page == 'History'; Composer.Visible = Value.Page == 'History'
    SettingsPage.Visible = Value.Page == 'Settings'
end)
Owner:Add(function() Subscription:Disconnect() end)
Owner:Connect(Window.Destroying, function() Owner:Dispose() end)
local Storm: TextButton? = nil
Storm = Button(SettingsPage, 'Storm', 'Storm idle', 276, 0, function()
    local Completed = 0
    for _ = 1, 1024 do task.defer(function() Completed += 1; if Storm then Storm.Text = 'Storm ' .. Completed end end) end
end)
local function Pressure(Source: string)
    for _, Child in Images:GetChildren() do Child:Destroy() end
    for Index = 1, 10 do Instance.new('ImageLabel', {Parent = Images, Name = 'Pressure' .. Index, Source = Source,
        Position = UDim2.fromOffset(((Index - 1) % 5) * 64, math.floor((Index - 1) / 5) * 64 + 56), Size = UDim2.fromOffset(56, 56)}) end
end
Button(SettingsPage, 'Pressure', 'Images', 0, 56, function() Pressure('Pressure.png') end)
Button(SettingsPage, 'Oversize', 'Oversize', 92, 56, function() Pressure('Oversize.png') end)
Button(SettingsPage, 'Broken', 'Broken', 184, 56, function() Pressure('Broken.png') end)
Button(SettingsPage, 'ClearImages', 'Clear', 276, 56, function() for _, Child in Images:GetChildren() do Child:Destroy() end end)
task.defer(function()
    for Index = 1, 10000 do Items[Index] = {Id = 'Message-' .. Index, Text = 'Synthetic row ' .. Index .. ' \u{1F642}', Height = 72 + Index % 4 * 16, Draft = ''} end
    List = VirtualList.New(Viewport, {
        MaxRows = 48, Overscan = 2,
        GetKey = function(Item: any) return Item.Id end,
        GetHeight = function(Item: any, _Width: number) return Item.Height end,
        Render = function(Parent: Frame, Item: any, Binding: any, Width: number)
            Binding:Own(Instance.new('ImageLabel', {Parent = Parent, Source = 'Avatar.png', Position = UDim2.fromOffset(4, 4), Size = UDim2.fromOffset(28, 28)}))
            Binding:Own(Instance.new('TextLabel', {Parent = Parent, Name = 'RowBody', Text = Item.Text, Position = UDim2.fromOffset(36, 4), Size = UDim2.fromOffset(Width - 40, 28)}))
            local Editor = Binding:Own(Instance.new('TextBox', {Parent = Parent, Name = Item.Id, Text = Item.Draft, MaxLength = 256,
                Position = UDim2.fromOffset(36, 34), Size = UDim2.fromOffset(Width - 40, 36)})) :: TextBox
            Binding:Connect(Editor.TextChanged, function() Item.Draft = Editor.Text end)
        end,
        Update = function(Parent: Frame, Item: any, Width: number)
            local Body = Parent:FindFirstChild('RowBody') :: TextLabel
            Body.Text = Item.Text; Body.Size = UDim2.fromOffset(Width - 40, 28)
            local Editor = Parent:FindFirstChild(Item.Id) :: TextBox
            Editor.Size = UDim2.fromOffset(Width - 40, 36)
        end,
    })
    Owner:Add(function() List:Dispose() end)
    List:SetItems(Items); Owner:Connect(Viewport.CanvasChanged, StatusText)
    Window.Visible = true; StatusText()
end)
