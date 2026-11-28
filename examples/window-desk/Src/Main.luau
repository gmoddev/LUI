--!strict
local Commands = require('@LuiWorkbench/Commands')
local Documents = require('@LuiWorkbench/Documents')
local Workspace = require('@LuiWorkbench/Workspace')
local Preferences = require('@LuiWorkbench/Preferences')
local Registry, Buffers, Layout = Commands.New(), Documents.New(), Workspace.New()
local Theme = app:GetService('ThemeService')
local Storage: StorageService? = nil
if app:GetService('PlatformService'):Supports('Storage') then Storage = app:GetService('StorageService') end
local Settings = Preferences.New(Storage, 'workspace')
local Note = Preferences.New(Storage, 'shared_note')
Buffers:Open('Shared', 'Shared note', 'This note is shared by every document window.')
Layout:Add('Shared', 'Left')
local Main = Instance.new('Window', {Name = 'WindowDesk', Title = 'Window Desk', Size = UDim2.fromOffset(640, 480)})
local Root = Instance.new('Frame', {Parent = Main})
local Status = Instance.new('TextLabel', {Parent = Root, Name = 'Status', Text = 'Loading workspace…', TextWrapped = true,
    Position = UDim2.new(0, 20, 1, -100), Size = UDim2.new(1, -40, 0, 80)})
Instance.new('TextLabel', {Parent = Root, Text = 'Window Desk', TextSize = 24, FontWeight = 'Bold',
    Position = UDim2.fromOffset(20, 16), Size = UDim2.new(1, -40, 0, 44)})
local Views: {[Window]: {Editor: TextBox, Root: Frame, Context: Frame}} = {}
local Alive, Saving, NoteLoading, LayoutLoading, LayoutBusy, Rendering = true, false, true, true, false, false
local NoteFailure: string? = nil
local ThemeConnection: Connection? = nil
local Serial = 0
local Dark = Theme.CurrentTheme == 'Dark'
local NewView: () -> ()
local function WorkspaceText(): string
    local Count = 0; for _ in Views do Count += 1 end
    return (if Dark then 'Dark' else 'Light') .. '\n' .. Count .. '\n' .. Layout:Encode()
end
local function Report(Text: string) if Alive then Status.Text = Text end end
local function Paint(Panel: Frame)
    if Theme.CurrentTheme == 'HighContrast' then Panel.BackgroundColor3 = nil; Panel.TextColor3 = nil; return end
    Panel.BackgroundColor3 = if Dark then Color3.fromRGB(25, 29, 38) else Color3.fromRGB(245, 247, 250)
    Panel.TextColor3 = if Dark then Color3.fromRGB(235, 239, 245) else Color3.fromRGB(22, 28, 38)
end
local function Render()
    if not Alive then return end
    Rendering = true
    local Document = Buffers:Get('Shared')
    if Document then
        for Window, View in Views do
            if View.Editor.Text ~= Document.Text then View.Editor.Text = Document.Text end
            Window.Title = 'Shared note' .. (if Document.Dirty then ' *' else '') .. ' — Window Desk'
        end
    end
    Rendering = false
end
local function Execute(Id: string)
    local Ok, Failure = Registry:Execute(Id)
    if not Ok then Report('Action unavailable: ' .. tostring(Failure)) end
end
local function Button(Parent: Instance, Name: string, Text: string, Y: number, Action: () -> ())
    local Value = Instance.new('TextButton', {Parent = Parent, Name = Name, Text = Text,
        Position = UDim2.fromOffset(20, Y), Size = UDim2.new(1, -40, 0, 38)})
    Value.Activated:Connect(Action)
    return Value
end
Registry:Register({Id = 'Note.Save', Label = 'Save shared note', Shortcut = {Key = 'S', Control = true},
    AllowWhileEditing = true,
    Enabled = function() return not Saving and not NoteLoading end, Execute = function()
        if NoteFailure then Report('Saved note could not be read: ' .. NoteFailure .. '. Discard unsaved edits explicitly to start a replacement note.'); return end
        local Receipt = Buffers:BeginSave('Shared')
        if #Receipt.Text > 8192 then Buffers:CancelSave(Receipt); Report('Notes are limited to 8,192 UTF-8 bytes.'); return end
        Saving = true
        Note:Write(buffer.fromstring(Receipt.Text), function(_, Failure)
            if not Alive then return end
            Saving = false; Buffers:CompleteSave(Receipt, Failure == nil); Render()
            Report(if Failure then 'Note was not saved: ' .. Failure else 'Note saved. Any newer edits still need saving.')
        end)
    end})
Registry:Register({Id = 'View.New', Label = 'Open another view', Shortcut = {Key = 'N', Control = true},
    AllowWhileEditing = true,
    Enabled = function() return not LayoutLoading end, Execute = function() NewView() end})
Registry:Register({Id = 'View.Theme', Label = 'Switch theme', Enabled = function() return Theme.CurrentTheme ~= 'HighContrast' end, Execute = function()
    Dark = not Dark; Paint(Root); for _, View in Views do Paint(View.Root); Paint(View.Context) end
end})
Registry:Register({Id = 'Note.Discard', Label = 'Discard unsaved edits', Enabled = function() return not Saving and not NoteLoading end,
    Execute = function()
        local Current = Buffers:Get('Shared')
        if not Current then return end
        Buffers:Close('Shared', true)
        Buffers:Open('Shared', 'Shared note', '')
        NoteFailure = nil
        Render(); Report('Unsaved edits discarded. The saved note is unchanged. Reopen the app to restore it.')
    end})
Registry:Register({Id = 'Workspace.Save', Label = 'Save workspace', Enabled = function() return not LayoutBusy and not LayoutLoading end,
    Execute = function()
        LayoutBusy = true
        local Captured = WorkspaceText()
        Settings:Write(buffer.fromstring(Captured), function(_, Failure)
            if not Alive then return end
            LayoutBusy = false
            Report(if Failure then 'Workspace was not saved: ' .. Failure
                elseif Captured ~= WorkspaceText() then 'Earlier workspace saved. New view or theme changes still need Save workspace.'
                else 'Workspace saved. Open views and theme will return on the next launch.')
        end)
    end})
NewView = function()
    if not Alive then return end
    local Count = 0; for _ in Views do Count += 1 end
    if Count >= 4 then Report('Close a view before opening another (four-view limit).'); return end
    Serial += 1
    local Window = Instance.new('Window', {Name = 'Document' .. Serial, Title = 'Shared note', Size = UDim2.fromOffset(620, 470)})
    local Panel = Instance.new('Frame', {Parent = Window, ClipsDescendants = true})
    local Editor = Instance.new('TextBox', {Parent = Panel, Name = 'SharedEditor', Multiline = true, MaxLength = 8192,
        TextXAlignment = 'Left', TextYAlignment = 'Top', Position = UDim2.fromOffset(20, 70), Size = UDim2.new(1, -40, 1, -90)})
    local Context = Instance.new('Frame', {Parent = Panel, Name = 'ContextActions', Visible = false, ZIndex = 10,
        Position = UDim2.fromOffset(180, 40), Size = UDim2.fromOffset(240, 110)})
    Button(Context, 'ContextSave', 'Save shared note', 10, function() Context.Visible = false; Execute('Note.Save') end)
    Button(Context, 'ContextFocus', 'Return to editing', 60, function() Context.Visible = false; Editor:Focus() end)
    local Actions = Instance.new('TextButton', {Parent = Panel, Name = 'Actions', Text = 'Note actions',
        Position = UDim2.fromOffset(20, 16), Size = UDim2.fromOffset(160, 38)})
    Actions.Activated:Connect(function() Context.Visible = not Context.Visible end)
    Editor.InputBegan:Connect(function(Input)
        if Input.Device == 'Mouse' and Input.Button == 'Right' then Context.Visible = true end
        if Input.Device == 'Keyboard' and Input.Key == 'Escape' then Context.Visible = false end
    end)
    Views[Window] = {Editor = Editor, Root = Panel, Context = Context}
    Editor.TextChanged:Connect(function()
        if Alive and not Rendering then Buffers:SetText('Shared', Editor.Text); Render() end
    end)
    Window.Shortcut:Connect(function(Input) Registry:Dispatch(Input) end)
    Window.CloseRequested:Connect(function()
        local Current = Buffers:Get('Shared')
        if Current and Current.Dirty then Window:PreventClose(); Report('Use Save shared note or Discard unsaved edits in the main window before closing a view.') end
    end)
    Window.Closed:Connect(function() Views[Window] = nil; Window:Destroy() end)
    Window.Destroying:Connect(function() Views[Window] = nil end)
    Paint(Panel); Paint(Context); Render(); Window.Visible = true
    task.defer(function() if Alive and Views[Window] then Editor:Focus() end end)
end
for Index, Entry in {{'View.New', 'Open shared note / another view'}, {'Note.Save', 'Save shared note'},
    {'Workspace.Save', 'Save workspace'}, {'View.Theme', 'Switch theme'}, {'Note.Discard', 'Discard unsaved edits'}} do
    Button(Root, Entry[1], Entry[2], 70 + (Index - 1) * 52, function() Execute(Entry[1]) end)
end
Main.Shortcut:Connect(function(Input) Registry:Dispatch(Input) end)
Main.CloseRequested:Connect(function()
    local Current = Buffers:Get('Shared')
    if Saving or NoteLoading or LayoutLoading or LayoutBusy or (Current and Current.Dirty) then Main:PreventClose(); Report('Finish pending operations, then save or discard unsaved note edits before closing.') end
end)
local function Retire()
    if not Alive then return end
    Alive = false; Settings:Dispose(); Note:Dispose()
    if ThemeConnection then ThemeConnection:Disconnect(); ThemeConnection = nil end
    for Window in Views do Window:Destroy() end
    table.clear(Views); Registry:Dispose(); Buffers:Dispose(); Layout:Dispose()
end
Main.Closed:Connect(function() Retire(); Main:Destroy() end)
Main.Destroying:Connect(Retire)
ThemeConnection = Theme.ThemeChanged:Connect(function(Value)
    if not Alive then return end
    Dark = Value == 'Dark'; Paint(Root); for _, View in Views do Paint(View.Root); Paint(View.Context) end
end)
local Load = Buffers:BeginLoad('Shared')
Note:Read(function(Bytes, Failure)
    if not Alive then return end
    local Loaded, Reason = Buffers:CompleteLoad(Load, if Bytes and not Failure then buffer.tostring(Bytes) else nil)
    if Failure then NoteFailure = Failure
    elseif not Loaded and Reason == 'InvalidText' then NoteFailure = Reason end
    NoteLoading = false
    Render()
    if NoteFailure then Report('Saved note could not be read: ' .. NoteFailure .. '. Saving a replacement requires Discard unsaved edits.') end
end)
Settings:Read(function(Bytes, Failure)
    if not Alive then return end
    local Count = 1
    if Bytes and not Failure then
        local Choice, Number, Encoded = string.match(buffer.tostring(Bytes), '^(%a+)\n(%d)\n(.+)$')
        local Parsed = tonumber(Number)
        if (Choice == 'Dark' or Choice == 'Light') and Parsed and Parsed >= 0 and Parsed <= 4 and Encoded then
            local Restored = Layout:Decode(Encoded)
            if Restored then Count = Parsed; Dark = Choice == 'Dark' end
        end
    end
    LayoutLoading = false; Paint(Root)
    for _ = 1, Count do NewView() end
    if NoteFailure then Report('Saved note could not be read: ' .. NoteFailure .. '. Saving a replacement requires Discard unsaved edits.')
    else Report(if Failure then 'Workspace storage unavailable. This session still works.' else 'Ready. All views edit the same note. Save note and Save workspace are separate actions.') end
end)
Paint(Root); Main.Visible = true
