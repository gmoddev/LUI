--!strict
-- Preview.3: selected documents and folders, plus an app-private scratchpad.
local Preferences = require('@LuiWorkbench/Preferences')
local Files = require('@LuiWorkbench/Files')
local Platform = app:GetService('PlatformService')
local Storage: StorageService? = nil
if Platform:Supports('Storage') then Storage = app:GetService('StorageService') end
local Store = Preferences.New(Storage, 'scratchpad')
local FileProvider: FileService? = nil
pcall(function() FileProvider = app:GetService('FileService') end)
local Picker = Files.New(FileProvider)
local Folder: Files.Folder? = nil
local Current: Files.Operation? = nil
local ActiveFile: Files.File? = nil
local Window = Instance.new('Window', {Name = 'FileDesk', Title = 'File Desk', Size = UDim2.fromOffset(820, 680)})
local Root = Instance.new('Frame', {Parent = Window, ClipsDescendants = true})
local Heading = Instance.new('TextLabel', {Parent = Root, Text = 'File Desk', FontWeight = 'Bold', TextSize = 24,
    Position = UDim2.fromOffset(20, 12), Size = UDim2.new(1, -40, 0, 42)})
local Selected = Instance.new('TextLabel', {Parent = Root, Name = 'SelectedFile', Text = 'No file selected.', TextWrapped = true,
    TextXAlignment = 'Left', Position = UDim2.fromOffset(20, 160), Size = UDim2.new(1, -40, 0, 58)})
local ChildName = Instance.new('TextBox', {Parent = Root, Name = 'ChildName', PlaceholderText = 'File name in the selected folder',
    Position = UDim2.fromOffset(20, 222), Size = UDim2.new(1, -40, 0, 34)})
local Editor = Instance.new('TextBox', {Parent = Root, Name = 'Note', Multiline = true, MaxLength = 8192,
    PlaceholderText = 'Open a text file or write a note. Save as writes a file; Save note keeps an app-private copy.',
    TextXAlignment = 'Left', TextYAlignment = 'Top', Position = UDim2.fromOffset(20, 264), Size = UDim2.new(1, -40, 1, -344)})
local Status = Instance.new('TextLabel', {Parent = Root, Name = 'Status', Text = 'Ready. Notes are limited to 8,192 UTF-8 bytes.',
    TextWrapped = true, TextXAlignment = 'Left', Position = UDim2.new(0, 20, 1, -70), Size = UDim2.new(1, -40, 0, 58)})
local Alive, Busy, Dirty = true, false, false
local function Report(Text: string) if Alive then Status.Text = Text end end
local function Button(Name: string, Text: string, Index: number, Action: () -> ())
    local Value = Instance.new('TextButton', {Parent = Root, Name = Name, Text = Text, TextSize = 14,
        Position = UDim2.new(((Index - 1) % 5) / 5, 20, 0, 66 + math.floor((Index - 1) / 5) * 46), Size = UDim2.new(1 / 5, -24, 0, 38)})
    Value.Activated:Connect(Action)
end
local function ReadFile(File: Files.File, Before: string)
    ActiveFile = File
    local Operation = File:Read(function(Bytes, Failure)
        File:Close(); ActiveFile = nil
        if not Alive then return end
        Busy = false; Current = nil
        if Failure or not Bytes then Report('File was not read: ' .. (Failure or 'Unavailable')); return end
        local Text = buffer.tostring(Bytes)
        if #Text > 8192 or not utf8.len(Text) or string.find(Text, '\0', 1, true) then Report('Choose a UTF-8 text file of at most 8,192 bytes.'); return end
        if Editor.Text ~= Before then Report('Your newer edits were kept. Open the file again when ready.'); return end
        Editor.Text = Text; Dirty = false; Selected.Text = File.Name
        Report('Opened for reading. Save as writes a copy; Save note keeps a copy in this app.')
    end)
    if Busy then Current = Operation end
end
Button('Open', 'Open file', 1, function()
    if Busy then Report('Finish the current operation first.'); return end
    if Dirty then Report('Save or discard your edits before opening another file.'); return end
    Busy = true
    local Before = Editor.Text
    local Operation = Picker:OpenFile(function(File, Failure)
        if not Alive then return end
        if File then ReadFile(File, Before)
        else Busy = false; Current = nil; Report(if Failure then 'File selection failed: ' .. Failure else 'Selection cancelled. Your note is unchanged.') end
    end)
    if Busy then Current = Operation end
end)
Button('Save', 'Save note', 2, function()
    if Busy then Report('Finish the current operation first.'); return end
    local Captured = Editor.Text
    if #Captured > 8192 then Report('This note exceeds 8,192 UTF-8 bytes. Shorten it before saving.'); return end
    Busy = true
    Store:Write(buffer.fromstring(Captured), function(_, Failure)
        if not Alive then return end
        Busy = false
        if Failure then Report('Note was not saved: ' .. Failure)
        else Dirty = Editor.Text ~= Captured; Report(if Dirty then 'Saved the earlier version. New edits still need saving.' else 'Note saved in this app.') end
    end)
end)
Button('Restore', 'Restore note', 3, function()
    if Busy then Report('Finish the current operation first.'); return end
    if Dirty then Report('Save your edits before restoring.'); return end
    Busy = true
    local Before = Editor.Text
    Store:Read(function(Bytes, Failure)
        if not Alive then return end
        Busy = false
        if Failure then Report('Note was not restored: ' .. Failure)
        elseif Editor.Text ~= Before then Report('You edited the note while it loaded. Your edits were kept.')
        elseif Bytes then
            local Text = buffer.tostring(Bytes)
            if not utf8.len(Text) or string.find(Text, '\0', 1, true) then Report('Saved note is not valid text.'); return end
            Editor.Text = Text; Dirty = false; Report('Note restored.')
        else Report('There is no saved note yet.') end
    end)
end)
Button('Copy', 'Copy note', 4, function()
    if not Platform:Supports('Clipboard') then Report('This host has no clipboard.'); return end
    local Ok, Written = pcall(function() return app:GetService('ClipboardService'):WriteText(Editor.Text) end)
    Report(if Ok and Written then 'Note copied.' else 'Clipboard write failed. You can retry.')
end)
Button('Paste', 'Paste note', 5, function()
    if Busy then Report('Finish the current operation first.'); return end
    if not Platform:Supports('Clipboard') then Report('This host has no clipboard.'); return end
    Busy = true
    local Before = Editor.Text
    local Started = pcall(function() app:GetService('ClipboardService'):ReadText(function(Text, Failure)
        if not Alive then return end
        Busy = false
        if Failure then Report('Clipboard read failed: ' .. Failure)
        elseif Editor.Text ~= Before then Report('You edited the note while the clipboard loaded. Your edits were kept.')
        elseif Text and #Text <= 8192 and utf8.len(Text) and not string.find(Text, '\0', 1, true) then Editor.Text = Text; Report('Clipboard text pasted. Save the note to keep it.')
        else Report('Clipboard text is empty or exceeds the note limit.') end
    end) end)
    if not Started then Busy = false; Report('Clipboard read could not start. You can retry.') end
end)
Button('Discard', 'Discard edits', 6, function()
    if Busy then Report('Finish the current operation first.'); return end
    Editor.Text = ''; Dirty = false
    Report('Unsaved edits discarded. The saved note is unchanged; use Restore note to reopen it.')
end)
Button('SaveFile', 'Save as', 7, function()
    if Busy then Report('Finish the current operation first.'); return end
    local Captured = Editor.Text
    if #Captured > 8192 then Report('Shorten the text to 8,192 UTF-8 bytes before saving.'); return end
    Busy = true
    local Operation = Picker:SaveFile('note.txt', function(File, Failure)
        if not Alive then return end
        if not File then Busy = false; Current = nil; Report(if Failure then 'Save as failed: ' .. Failure else 'Save as cancelled.'); return end
        ActiveFile = File
        local Write = File:Write(buffer.fromstring(Captured), function(WriteFailure)
            File:Close(); ActiveFile = nil
            if not Alive then return end
            Busy = false; Current = nil
            if WriteFailure then Report('Save could not be confirmed: ' .. WriteFailure .. '. The file may have been written; your editor text is kept.')
            else Dirty = Editor.Text ~= Captured; Selected.Text = File.Name; Report(if Dirty then 'Saved the earlier version. New edits still need saving.' else 'File saved.') end
        end)
        if Busy then Current = Write end
    end)
    if Busy then Current = Operation end
end)
Button('Folder', 'Open folder', 8, function()
    if Busy then Report('Finish the current operation first.'); return end
    Busy = true
    local Operation = Picker:OpenFolder(function(SelectedFolder, Failure)
        if not Alive then return end
        if not SelectedFolder then Busy = false; Current = nil; Report(if Failure then 'Folder selection failed: ' .. Failure else 'Folder selection cancelled.'); return end
        if Folder then Folder:Close() end
        Folder = SelectedFolder
        local Listing = SelectedFolder:List(function(Entries, ListFailure)
            if not Alive then return end
            Busy = false; Current = nil
            if not Entries or ListFailure then Report('Folder could not be listed: ' .. (ListFailure or 'Unavailable')); return end
            local Names: {string} = {}
            for Index, Entry in Entries do if Index <= 12 then table.insert(Names, Entry.Name .. (if Entry.IsDirectory then '/' else '')) end end
            Selected.Text = SelectedFolder.Name .. ': ' .. table.concat(Names, ', ')
            Report(tostring(#Entries) .. ' entries. Type a file name above, then choose Open named file. Subfolders are listed; only files can be opened.')
        end)
        if Busy then Current = Listing end
    end)
    if Busy then Current = Operation end
end)
Button('OpenChild', 'Open named file', 9, function()
    if Busy then Report('Finish the current operation first.'); return end
    if Dirty then Report('Save or discard your edits before opening another file.'); return end
    local Directory = Folder
    if not Directory then Report('Choose Open folder first.'); return end
    Busy = true
    local Before = Editor.Text
    local Operation = Directory:Open(ChildName.Text, function(File, Failure)
        if not Alive then return end
        if File then ReadFile(File, Before)
        else Busy = false; Current = nil; Report('Named file was not opened: ' .. (Failure or 'Unavailable')) end
    end)
    if Busy then Current = Operation end
end)
Button('Cancel', 'Cancel file task', 10, function()
    local Operation = Current
    if not Busy or not Operation then Report('There is no cancelable file task.'); return end
    Current = nil; Operation:Cancel()
    if ActiveFile then ActiveFile:Close(); ActiveFile = nil end
    Busy = false; Report('Cancellation requested. A save may already have completed; editor text is kept.')
end)
Editor.TextChanged:Connect(function() Dirty = true end)
Window.CloseRequested:Connect(function()
    if Busy or Dirty then Window:PreventClose(); Report(if Busy then 'Wait for the operation to finish.' else 'Save the note or discard edits before closing.') end
end)
local function Retire()
    if not Alive then return end
    Alive = false; Picker:Dispose(); Store:Dispose()
end
Window.Closed:Connect(function() Retire(); Window:Destroy() end)
Window.Destroying:Connect(Retire)
Window.Visible = true
