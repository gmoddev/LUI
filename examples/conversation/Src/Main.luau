--!strict
local Scope = require('@LuiUI/Scope')
local State = require('@LuiUI/State')
local VirtualList = require('@LuiUI/VirtualList')
local Owner = Scope.New()
local Theme = app:GetService('ThemeService')
local Performance = app:GetService('PerformanceService')
local Settings = State.New({FontSize = 14, Page = 'History'})
Owner:Add(function() Settings:Dispose() end)
local Window = Instance.new('Window', {Title = 'LUI Conversations', Size = UDim2.fromOffset(1100, 760)})
local Root = Instance.new('Frame', {Parent = Window})
local Sidebar = Instance.new('Frame', {Parent = Root, Size = UDim2.new(0, 180, 1, 0)})
local Main = Instance.new('Frame', {Parent = Root, Position = UDim2.fromOffset(192, 0), Size = UDim2.new(1, -204, 1, 0)})
local Header = Instance.new('TextLabel', {Parent = Main, Text = 'Synthetic conversation', TextSize = 22, FontWeight = 'Bold', Position = UDim2.fromOffset(0, 8), Size = UDim2.new(1, 0, 0, 36)})
local Viewport = Instance.new('ScrollingFrame', {Parent = Main, Name = 'History', Position = UDim2.fromOffset(0, 92), Size = UDim2.new(1, 0, 1, -264), ClipsDescendants = true}) :: ScrollingFrame
local Composer = Instance.new('TextBox', {Parent = Main, Name = 'Composer', Multiline = true, PlaceholderText = 'Write a synthetic message. Native selection, clipboard and IME work here.', Position = UDim2.new(0, 0, 1, -156), Size = UDim2.new(1, -100, 0, 96), MaxLength = 4096, CornerRadius = 8})
local Status = Instance.new('TextLabel', {Parent = Main, Name = 'Status', TextSize = 12, Position = UDim2.new(0, 0, 1, -50), Size = UDim2.new(1, 0, 0, 42)})
local SettingsPage = Instance.new('Frame', {Parent = Main, Visible = false, Position = UDim2.fromOffset(0, 92), Size = UDim2.new(1, 0, 1, -144)})
local Secret = Instance.new('TextBox', {Parent = SettingsPage, Name = 'Secret', Secret = true, PlaceholderText = 'Synthetic secret; masked and classified', Position = UDim2.fromOffset(0, 92), Size = UDim2.new(1, -20, 0, 44), MaxLength = 128})
Instance.new('TextLabel', {Parent = SettingsPage, Text = 'Settings', TextSize = 22, FontWeight = 'Bold', Size = UDim2.new(1, 0, 0, 36)})
Instance.new('TextLabel', {Parent = SettingsPage, Text = 'Font size: switch to remeasure variable-height rows. Secret input always uses native password presentation.', Position = UDim2.fromOffset(0, 40), Size = UDim2.new(1, -20, 0, 44)})
local Items: {any} = {}
local NextId = 10001
local Ready = false
local List: any

local function Button(Parent: Instance, Text: string, X: number, Y: number, Width: number, Callback: () -> ())
    local Value = Instance.new('TextButton', {Parent = Parent, Text = Text, TextSize = 13, TextWrapped = false, Position = UDim2.fromOffset(X, Y), Size = UDim2.fromOffset(Width, 34), CornerRadius = 6})
    Owner:Connect(Value.Activated, Callback)
    return Value
end
local function Palette()
    if Theme.CurrentTheme == 'Dark' then return Color3.fromRGB(19,23,31), Color3.fromRGB(31,37,48), Color3.fromRGB(230,235,244) end
    return Color3.fromRGB(241,244,249), Color3.fromRGB(224,231,240), Color3.fromRGB(24,32,45)
end
local function UpdateStatus()
    local Stats = Performance:GetStats()
    local Rows: number = List and List:GetStats().LiveRows or 0
    Status.Text = tostring(#Items) .. ' messages | ' .. tostring(Rows) .. ' resident rows | ' .. tostring(Stats.LiveInstances) .. ' Instances | layout p95 ' .. tostring(Stats.LayoutP95Ms) .. ' ms | ' .. Theme.CurrentTheme
end
local function UpdateItems(Follow: boolean?)
    if List then List:SetItems(Items, Follow); UpdateStatus() end
end
local function NewMessage(Text: string)
    local Value = {Id = 'Message-' .. NextId, Author = 'Synthetic member', Text = Text}
    NextId += 1; return Value
end
local function VisibleIndex(): number
    local Anchor = List and List:GetAnchor()
    if Anchor then for Index, Item in Items do if Item.Id == Anchor.Key then return Index end end end
    return 1
end
Instance.new('TextLabel', {Parent = Sidebar, Text = 'LUI\nConversations', FontWeight = 'Bold', TextSize = 18, Position = UDim2.fromOffset(16, 18), Size = UDim2.fromOffset(150, 72)})
Button(Sidebar, 'History', 16, 116, 148, function() Settings:Set({FontSize = Settings:Get().FontSize, Page = 'History'}) end)
Button(Sidebar, 'Settings', 16, 160, 148, function() Settings:Set({FontSize = Settings:Get().FontSize, Page = 'Settings'}) end)
Button(Sidebar, 'Scroll to bottom', 16, 220, 148, function() if Ready then Viewport.CanvasPosition = Vector2.new(0, 1e9); List:Refresh(); UpdateStatus() end end)
Button(Main, 'Insert above', 0, 48, 110, function() if Ready then table.insert(Items, 1, NewMessage('Inserted synthetic item. The visible stable row remains anchored.')); UpdateItems() end end)
Button(Main, 'Edit visible', 120, 48, 110, function()
    if Ready and #Items > 0 then local Index = VisibleIndex(); local Previous = Items[Index]; Items[Index] = {Id = Previous.Id, Author = Previous.Author, Text = Previous.Text .. '\nEdited: extra Unicode \u{1F642} and variable height.'}; UpdateItems() end
end)
Button(Main, 'Delete visible', 240, 48, 120, function() if Ready and #Items > 0 then table.remove(Items, VisibleIndex()); UpdateItems() end end)
Button(Main, 'Append', 370, 48, 90, function() if Ready then table.insert(Items, NewMessage('Appended synthetic message. Bottom follows only when already at bottom.')); UpdateItems(true) end end)
Button(Main, 'Send', 0, 0, 90, function()
    if Ready and Composer.Text ~= '' then table.insert(Items, NewMessage(Composer.Text)); Composer.Text = ''; UpdateItems(true) end
end).Position = UDim2.new(1, -90, 1, -156)
Button(SettingsPage, '14 px', 0, 160, 90, function() Settings:Set({FontSize = 14, Page = 'Settings'}) end)
Button(SettingsPage, '18 px', 104, 160, 90, function() Settings:Set({FontSize = 18, Page = 'Settings'}) end)
Owner:Connect(Window.Destroying, function() Owner:Dispose() end)
local Subscription = Settings:Subscribe(function(Value: any)
    Viewport.Visible = Value.Page == 'History'; Composer.Visible = Value.Page == 'History'; SettingsPage.Visible = Value.Page == 'Settings'
    Viewport.TextSize = Value.FontSize; Composer.TextSize = Value.FontSize
    if List then List:Invalidate(); UpdateStatus() end
end)
Owner:Add(function() Subscription:Disconnect() end)
local function ApplyTheme()
    local Background, Panel, Foreground = Palette()
    Root.BackgroundColor3 = Background; Root.TextColor3 = Foreground
    Sidebar.BackgroundColor3 = Panel; Composer.BackgroundColor3 = Panel
    if List then List:Invalidate(); UpdateStatus() end
end
Owner:Connect(Theme.ThemeChanged, ApplyTheme)
ApplyTheme()

task.defer(function()
    for Index = 1, 10000 do
        Items[Index] = {Id = 'Message-' .. Index, Author = 'Synthetic member ' .. (Index % 17),
            Text = string.rep('Portable UI preserves owned state, scroll anchoring and bounded objects. ', 1 + Index % 4) .. '\u{1F642} #' .. Index}
    end
    List = VirtualList.New(Viewport, {
        MaxRows = 48, Overscan = 2,
        GetKey = function(Item: any) return Item.Id end,
        GetHeight = function(Item: any, Width: number)
            local Font = Settings:Get().FontSize
            local Columns = math.max(1, math.floor((Width - 84) / Font))
            return 44 + math.ceil((utf8.len(Item.Text) or #Item.Text) / Columns) * Font * 1.6
        end,
        Render = function(Parent: Frame, Item: any, Binding: any, Width: number)
            local _, Panel = Palette()
            Parent.BackgroundColor3 = Panel; Parent.CornerRadius = 8; Parent.BorderSizePixel = 0
            Binding:Own(Instance.new('ImageLabel', {Parent = Parent, Source = 'Avatar.png', Position = UDim2.fromOffset(12, 10), Size = UDim2.fromOffset(32, 32)}))
            Binding:Own(Instance.new('TextLabel', {Parent = Parent, Name = 'Author', Text = Item.Author, FontWeight = 'Bold', Position = UDim2.fromOffset(56, 8), Size = UDim2.fromOffset(Width - 72, 24)}))
            local Columns = math.max(1, math.floor((Width - 84) / Settings:Get().FontSize))
            local Height = math.ceil((utf8.len(Item.Text) or #Item.Text) / Columns) * Settings:Get().FontSize * 1.6
            Binding:Own(Instance.new('TextLabel', {Parent = Parent, Name = 'Body', Text = Item.Text, Position = UDim2.fromOffset(56, 36), Size = UDim2.fromOffset(Width - 72, Height)}))
        end,
        Update = function(Parent: Frame, Item: any, Width: number)
            local _, Panel = Palette(); Parent.BackgroundColor3 = Panel
            local Font = Settings:Get().FontSize
            local Columns = math.max(1, math.floor((Width - 84) / Font))
            local Height = math.ceil((utf8.len(Item.Text) or #Item.Text) / Columns) * Font * 1.6
            local Author = Parent:FindFirstChild('Author') :: TextLabel
            local Body = Parent:FindFirstChild('Body') :: TextLabel
            Author.Size = UDim2.fromOffset(Width - 72, 24)
            Body.Size = UDim2.fromOffset(Width - 72, Height)
        end,
    })
    Owner:Add(function() List:Dispose() end)
    List:SetItems(Items); Ready = true; Window.Visible = true
    UpdateStatus()
    Owner:Connect(Viewport.CanvasChanged, UpdateStatus)
    Owner:Connect(Composer.CompositionStarted, function() Status.Text = 'IME composing: preedit is separate from committed text' end)
    Owner:Connect(Composer.CompositionEnded, UpdateStatus)
end)
