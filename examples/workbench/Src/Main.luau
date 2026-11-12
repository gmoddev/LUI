--!strict
local Commands = require('@LuiWorkbench/Commands')
local Documents = require('@LuiWorkbench/Documents')
local Workspace = require('@LuiWorkbench/Workspace')
local Split = require('@LuiWorkbench/Split')
local Preferences = require('@LuiWorkbench/Preferences')
local Scope = require('@LuiUI/Scope')
local VirtualList = require('@LuiUI/VirtualList')
local Owner = Scope.New()
local Registry = Commands.New()
local Buffers = Documents.New()
local Layout = Workspace.New()
local Divider = Split.New()
local Theme = app:GetService('ThemeService')
local Storage: StorageService? = nil
if app:GetService('PlatformService'):Supports('Storage') then Storage = app:GetService('StorageService') end
local Settings = Preferences.New(Storage, 'workbench_layout')
local Selected = ''
local PresentationRevision = 0
local Alive, Ready, Rendering = true, false, false
local TabOwner: any = nil
local PaletteOwner: any = nil
local Navigation: any = nil
local SavedLayout: string? = nil
local Writing = false
local SavingDocument = false
local Stores: {[string]: Preferences.Store} = {}
local Loading: {[string]: any} = {}
local ThemeChoice: string? = nil
local Window = Instance.new('Window', {Name = 'Workbench', Title = 'LUI Workbench', Size = UDim2.fromOffset(1180, 780)})
local Root = Instance.new('Frame', {Parent = Window, ClipsDescendants = true})
local Header = Instance.new('TextLabel', {Parent = Root, Text = 'LUI Workbench', FontWeight = 'Bold', TextSize = 22,
    Position = UDim2.fromOffset(20, 12), Size = UDim2.fromOffset(320, 42)})
local Toolbar = Instance.new('Frame', {Parent = Root, Position = UDim2.fromOffset(16, 62), Size = UDim2.new(1, -32, 0, 38)})
local NavigationView = Instance.new('ScrollingFrame', {Parent = Root, Name = 'Navigation', ClipsDescendants = true,
    Position = UDim2.fromOffset(16, 114), Size = UDim2.new(0, 180, 1, -172)}) :: ScrollingFrame
local Content = Instance.new('ScrollingFrame', {Parent = Root, Position = UDim2.fromOffset(208, 114), Size = UDim2.new(1, -440, 1, -172), CanvasSize = UDim2.fromScale(1, 1), ClipsDescendants = true}) :: ScrollingFrame
local Tabs = Instance.new('ScrollingFrame', {Parent = Content, Name = 'Tabs', Size = UDim2.new(1, 0, 0, 38), ClipsDescendants = true}) :: ScrollingFrame
local Left = Instance.new('CodeEditor', {Parent = Content, Name = 'LeftEditor', Position = UDim2.fromOffset(0, 46), Size = UDim2.new(0.5, -4, 1, -46), MaxLength = 65534, FontFamily = 'Consolas', TextSize = 14})
local Right = Instance.new('CodeEditor', {Parent = Content, Name = 'RightEditor', Position = UDim2.new(0.5, 4, 0, 46), Size = UDim2.new(0.5, -4, 1, -46), MaxLength = 65534, FontFamily = 'Consolas', TextSize = 14})
local Editors: {[string]: CodeEditor} = {Left = Left, Right = Right}
local Grip = Instance.new('Frame', {Parent = Content, Name = 'Divider', Position = UDim2.new(0.5, -4, 0, 46), Size = UDim2.new(0, 8, 1, -46), AccessibilityLabel = 'Resize editor panes'})
local Inspector = Instance.new('TextLabel', {Parent = Root, Name = 'Inspector', Text = 'Loading workspace…', TextWrapped = true,
    TextXAlignment = 'Left', TextYAlignment = 'Top', Position = UDim2.new(1, -216, 0, 124), Size = UDim2.new(0, 200, 1, -180)})
local Status = Instance.new('TextLabel', {Parent = Root, Name = 'Status', Text = 'Loading synthetic documents…', TextSize = 12,
    TextXAlignment = 'Left', Position = UDim2.new(0, 20, 1, -46), Size = UDim2.new(1, -40, 0, 32)})
local Palette = Instance.new('Frame', {Parent = Root, Name = 'Palette', Visible = false, ZIndex = 20,
    Position = UDim2.fromOffset(220, 110), Size = UDim2.fromOffset(490, 410), CornerRadius = 10})
local Search = Instance.new('TextBox', {Parent = Palette, Name = 'CommandSearch', PlaceholderText = 'Search commands', MaxLength = 256,
    Position = UDim2.fromOffset(16, 16), Size = UDim2.fromOffset(458, 42)})
local Choices = Instance.new('Frame', {Parent = Palette, Position = UDim2.fromOffset(16, 70), Size = UDim2.fromOffset(458, 324)})
local Editing: {[string]: string?} = {}
local Render: () -> ()

local function Button(Parent: Instance, Text: string, Name: string, X: number, Y: number, Width: number, Callback: () -> (), Binding: Scope.Scope?): TextButton
    local Lifetime: Scope.Scope = Binding or Owner
    local Value = Instance.new('TextButton', {Parent = Parent, Name = Name, Text = Text, TextSize = 12,
        Position = UDim2.fromOffset(X, Y), Size = UDim2.fromOffset(Width, 34), CornerRadius = 5})
    if Binding then Binding:Own(Value) end
    Lifetime:Connect(Value.Activated, function() Callback() end)
    return Value
end
local function Select(Id: string)
    PresentationRevision += 1
    Selected = Id; Layout:Activate(Id); Render()
end
local function Open(Index: number)
    local Id = 'Note-' .. Index
    if not Buffers:Get(Id) then
        if #Buffers:List() == 32 then Status.Text = 'Close a document before opening another (32 document limit).'; return end
        Buffers:Open(Id, 'Note ' .. Index, '-- Synthetic document ' .. Index .. '\nlocal Message = "Hello from LUI"\nprint(Message)\n')
        Layout:Add(Id, if Index % 2 == 0 then 'Right' else 'Left')
        local Token = Buffers:BeginLoad(Id)
        Loading[Id] = Token
        local Store = Preferences.New(Storage, 'workbench_note_' .. Index)
        Stores[Id] = Store
        Store:Read(function(Bytes, Failure)
            if not Alive or Loading[Id] ~= Token then return end
            Loading[Id] = nil
            local _, Reason = Buffers:CompleteLoad(Token, if Bytes and not Failure then buffer.tostring(Bytes) else nil)
            if Reason == 'InvalidText' then Status.Text = 'Saved note has invalid text; using the synthetic default.'
            elseif Failure then Status.Text = 'Note storage unavailable: ' .. Failure end
            Render()
        end)
    end
    Select(Id)
end
local function PaletteRows()
    if PaletteOwner then PaletteOwner:Dispose() end
    PaletteOwner = Scope.New()
    for Index, Entry in Registry:List(Search.Text, 8) do
        local Value = Button(Choices, Entry.Label, 'Command-' .. Entry.Id, 0, (Index - 1) * 39, 458, function()
            Palette.Visible = false
            local Ok, Failure = Registry:Execute(Entry.Id)
            if not Ok then Status.Text = 'Command unavailable: ' .. tostring(Failure) end
        end, PaletteOwner)
        Value.Enabled = Entry.Enabled
    end
end
local function ApplyTheme()
    local Dark = (ThemeChoice or Theme.CurrentTheme) == 'Dark'
    local Background = if Dark then Color3.fromRGB(22, 25, 32) else Color3.fromRGB(244, 246, 250)
    local Panel = if Dark then Color3.fromRGB(35, 40, 50) else Color3.fromRGB(226, 232, 242)
    local Foreground = if Dark then Color3.fromRGB(232, 237, 245) else Color3.fromRGB(28, 36, 48)
    Root.BackgroundColor3 = Background; Root.TextColor3 = Foreground
    Toolbar.BackgroundColor3 = Background; Content.BackgroundColor3 = Background
    Left.BackgroundColor3 = Panel; Right.BackgroundColor3 = Panel; Palette.BackgroundColor3 = Panel
    Grip.BackgroundColor3 = Color3.fromRGB(93, 129, 199)
    if Navigation then Navigation:Invalidate() end
end
Render = function()
    if not Alive or not Ready or Rendering then return end
    Rendering = true
    local Snapshot = Layout:Snapshot()
    local Width = Content.AbsoluteSize.X
    local LeftWidth, RightWidth, Wide = Layout:Layout(Width)
    local LeftId = if Wide or #Snapshot.Right == 0 then Snapshot.ActiveLeft else Selected
    if #Snapshot.Left == 0 then LeftId = Snapshot.ActiveRight; LeftWidth = Width end
    local RightId = if Wide then Snapshot.ActiveRight else nil
    Editing.Left = LeftId; Editing.Right = RightId
    Left.Visible = LeftId ~= nil; Right.Visible = RightId ~= nil; Grip.Visible = Wide
    Left.Size = UDim2.new(0, LeftWidth, 1, -46)
    Right.Position = UDim2.fromOffset(LeftWidth + 8, 46); Right.Size = UDim2.new(0, RightWidth, 1, -46)
    Grip.Position = UDim2.fromOffset(LeftWidth, 46)
    for Name, Editor in Editors do
        local Id = Editing[Name]
        local Document = Id and Buffers:Get(Id)
        if Document and Editor.Text ~= Document.Text then Editor.Text = Document.Text end
    end
    if TabOwner then TabOwner:Dispose() end
    TabOwner = Scope.New()
    local Opened = Buffers:List()
    for Index, Document in Opened do
        Button(Tabs, (if Document.Dirty then '* ' else '') .. Document.Label,
            'Tab-' .. Document.Id, (Index - 1) * 126, 0, 120, function() Select(Document.Id) end, TabOwner)
    end
    Tabs.CanvasSize = UDim2.fromOffset(#Opened * 126, 38)
    local Document = if Selected ~= '' then Buffers:Get(Selected) else nil
    Inspector.Text = if Document then 'DOCUMENT\n\n' .. Document.Label .. '\n' .. Document.Id
        .. '\n\nRevision ' .. Document.Revision .. '\n' .. #Document.Text .. ' bytes\n'
        .. (if Document.Dirty then 'Unsaved changes' else 'Saved baseline')
        .. '\n\n' .. #Opened .. ' open documents\n\nDrag the blue divider, or move this tab to the other pane.'
        else 'No document selected.\nChoose a note to begin.'
    Rendering = false
end
Registry:Register({Id = 'Document.Save', Label = 'Save selected note', Enabled = function()
    return Ready and Selected ~= '' and not SavingDocument and Loading[Selected] == nil and Buffers:Get(Selected) ~= nil
end, Execute = function()
    local Receipt = Buffers:BeginSave(Selected)
    if #Receipt.Text > 8192 then Buffers:CancelSave(Receipt); Status.Text = 'Demo note save is limited to 8 KiB.'; return end
    SavingDocument = true
    Stores[Selected]:Write(buffer.fromstring(Receipt.Text), function(_, Failure)
        if not Alive then return end
        SavingDocument = false; Buffers:CompleteSave(Receipt, Failure == nil)
        Status.Text = if Failure then 'Save unavailable: ' .. Failure else 'Saved ' .. Receipt.Id .. '. Later edits stay unsaved.'
        Render()
    end)
end})
Registry:Register({Id = 'Document.Close', Label = 'Close selected note', Enabled = function() return Ready and Selected ~= '' end,
    Execute = function()
        local Id = Selected
        local Closed, Failure = Buffers:Close(Id)
        if not Closed then Status.Text = 'Close refused: ' .. tostring(Failure) .. '. Save before closing.'; return end
        Loading[Id] = nil; Stores[Id]:Dispose(); Stores[Id] = nil
        PresentationRevision += 1
        Layout:Remove(Id); local Opened = Buffers:List(); Selected = if Opened[1] then Opened[1].Id else ''
        Status.Text = 'Document closed.'; Render()
    end})
Registry:Register({Id = 'Document.Discard', Label = 'Discard changes and close selected note', Enabled = function() return Ready and Selected ~= '' end,
    Execute = function()
        local Id = Selected
        local Closed, Failure = Buffers:Close(Id, true)
        if not Closed then Status.Text = 'Discard refused: ' .. tostring(Failure); return end
        Loading[Id] = nil; Stores[Id]:Dispose(); Stores[Id] = nil; Layout:Remove(Id)
        PresentationRevision += 1
        local Opened = Buffers:List(); Selected = if Opened[1] then Opened[1].Id else ''
        Status.Text = 'Discarded changes in ' .. Id .. '.'; Render()
    end})
Registry:Register({Id = 'Workspace.Move', Label = 'Move tab to other pane', Enabled = function() return Ready and Selected ~= '' end,
    Execute = function()
        local Snapshot = Layout:Snapshot()
        PresentationRevision += 1
        Layout:Dock(Selected, if table.find(Snapshot.Left, Selected) then 'Right' else 'Left'); Render()
    end})
Registry:Register({Id = 'Workspace.Save', Label = 'Save workspace layout', Enabled = function() return Ready and not Writing end,
    Execute = function()
        SavedLayout = Layout:Encode(); Writing = true
        Settings:Write(buffer.fromstring(SavedLayout), function(_, Failure)
            if not Alive then return end
            Writing = false; Status.Text = if Failure then 'Layout kept for this session; storage unavailable: ' .. Failure else 'Layout saved locally.'
        end)
    end})
Registry:Register({Id = 'Workspace.Restore', Label = 'Restore workspace layout', Enabled = function() return Ready and SavedLayout ~= nil end,
    Execute = function()
        PresentationRevision += 1
        local Restored = SavedLayout and Layout:Decode(SavedLayout)
        Status.Text = if Restored then 'Workspace restored.' else 'Layout does not match the currently open documents.'
        Render()
    end})
Registry:Register({Id = 'View.Theme', Label = 'Switch light or dark theme', Execute = function()
    ThemeChoice = if (ThemeChoice or Theme.CurrentTheme) == 'Dark' then 'Light' else 'Dark'
    ApplyTheme()
end})
Registry:Register({Id = 'View.Commands', Label = 'Show command palette', Shortcut = {Key = 'P', Control = true, Shift = true},
    Execute = function() Palette.Visible = not Palette.Visible; if Palette.Visible then Search.Text = ''; PaletteRows(); Search:Focus() end end})
for Index, Entry in {{'Document.Save', 'Save note'}, {'Document.Close', 'Close'}, {'Workspace.Move', 'Other pane'},
    {'Workspace.Save', 'Save layout'}, {'Workspace.Restore', 'Restore'}, {'View.Theme', 'Theme'}, {'View.Commands', 'Commands'}} do
    Button(Toolbar, Entry[2], Entry[1], (Index - 1) * 112, 0, 104, function()
        local Ok, Failure = Registry:Execute(Entry[1]); if not Ok then Status.Text = 'Command unavailable: ' .. tostring(Failure) end
    end)
end
for Name, Editor in Editors do
    Owner:Connect(Editor.TextChanged, function()
        local Id = Editing[Name]
        if Alive and Ready and not Rendering and Id then
            Buffers:SetText(Id, Editor.Text); Selected = Id; Render()
        end
    end)
    Owner:Connect(Editor.Focused, function() local Id = Editing[Name]; if Id then Selected = Id; Render() end end)
end
Owner:Connect(Search.TextChanged, function() if Palette.Visible then PaletteRows() end end)
Owner:Connect(Search.InputBegan, function(Input: InputEvent)
    if Input.Device == 'Keyboard' and Input.Key == 'Escape' then Palette.Visible = false end
end)
Owner:Connect(Window.Shortcut, function(Input) Registry:Dispatch(Input) end)
Owner:Connect(Theme.ThemeChanged, ApplyTheme)
Owner:Connect(Content.CanvasChanged, function()
    local Before = Divider:Cancel(); if Before then Layout:SetRatio(Before) end; Render()
end)
Owner:Connect(Grip.InputBegan, function(Input: any)
    if Input.Device == 'Mouse' then
        Divider:Begin(Input, Grip.AbsolutePosition.X + Input.Position.X, Content.AbsoluteSize.X, Layout:Snapshot().Ratio)
    end
end)
Owner:Connect(Grip.InputChanged, function(Input: any)
    if Input.Device == 'Mouse' then
        local Ratio = Divider:Move(Input, Grip.AbsolutePosition.X + Input.Position.X, Content.AbsoluteSize.X)
        if Ratio then PresentationRevision += 1; Layout:SetRatio(Ratio); Render() end
    end
end)
Owner:Connect(Grip.InputEnded, function(Input: any)
    local Ratio = Divider:Finish(Input); if Ratio then Layout:SetRatio(Ratio); Render() end
end)
Owner:Connect(Window.CloseRequested, function()
    for _, Document in Buffers:List() do
        if Document.Dirty or SavingDocument then Window:PreventClose(); Status.Text = 'Save edited notes before closing the workbench.'; return end
    end
end)
Owner:Connect(Window.Closed, function() Window:Destroy() end)
Owner:Connect(Window.Destroying, function()
    if not Alive then return end
    Alive = false
    if Navigation then Navigation:Dispose() end
    if TabOwner then TabOwner:Dispose() end
    if PaletteOwner then PaletteOwner:Dispose() end
    Settings:Dispose()
    for _, Store in Stores do Store:Dispose() end
    Stores = {}; Loading = {}
    Divider:Dispose(); Layout:Dispose(); Buffers:Dispose(); Registry:Dispose()
    Owner:Dispose()
end)
ApplyTheme()
Window.Visible = true
task.defer(function()
    if not Alive then return end
    Ready = true
    Open(1); Open(2); Open(3)
    local Items = {}
    for Index = 1, 1000 do table.insert(Items, {Id = tostring(Index), Index = Index}) end
    Navigation = VirtualList.New(NavigationView, {MaxRows = 24, Overscan = 1,
        GetKey = function(Item: any) return Item.Id end,
        GetHeight = function() return 42 end,
        Render = function(Parent: Frame, Item: any, Binding: any)
            Button(Parent, 'Note ' .. Item.Index, 'Open-' .. Item.Id, 4, 4, 164, function() Open(Item.Index) end, Binding)
        end})
    Navigation:SetItems(Items)
    Status.Text = '1,000 synthetic notes; at most 24 resident navigation rows. Editing does not execute code.'
    Writing = true
    local RestoreRevision = PresentationRevision
    Settings:Read(function(Bytes, Failure)
        if not Alive then return end
        Writing = false
        if Bytes and not Failure then
            SavedLayout = buffer.tostring(Bytes)
            if PresentationRevision == RestoreRevision then
                local Restored = Layout:Decode(SavedLayout)
                Status.Text = if Restored then 'Saved workspace restored.' else 'Saved layout is incompatible; using defaults.'
            else Status.Text = 'Saved layout available; your current arrangement was kept.' end
            Render()
        elseif Failure then Status.Text = 'Local preferences unavailable: ' .. Failure end
    end)
    Render()
end)
