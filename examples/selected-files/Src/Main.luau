--!strict
local Files = require('@LuiWorkbench/Files')
local Picker = Files.New(app:GetService('FileService'))
local Window = Instance.new('Window', {Title='Selected files', Size=UDim2.fromOffset(620,440)})
local Editor = Instance.new('TextBox', {Parent=Window, Multiline=true, MaxLength=16000,
    Position=UDim2.fromOffset(16,64), Size=UDim2.new(1,-32,1,-128)})
local Status = Instance.new('TextLabel', {Parent=Window, Text='Open a UTF-8 text file or write a note.', TextWrapped=true,
    Position=UDim2.new(0,16,1,-60), Size=UDim2.new(1,-32,0,48)})
local Busy, Alive = false, true
local function Button(Text: string, Index: number, Action: () -> ())
    local Value = Instance.new('TextButton', {Parent=Window, Text=Text,
        Position=UDim2.new((Index-1)/3,16,0,16), Size=UDim2.new(1/3,-22,0,36)})
    Value.Activated:Connect(function() if not Busy then Action() end end)
end
Button('Open file',1,function()
    Busy = true
    local Before = Editor.Text
    Picker:OpenFile(function(File, Failure)
        if not File then Busy=false; Status.Text=Failure or 'Selection cancelled.'; return end
        File:Read(function(Bytes, ReadFailure)
            File:Close(); Busy=false
            if not Bytes then Status.Text=ReadFailure or 'Read failed.'; return end
            local Text = buffer.tostring(Bytes)
            if #Text>16000 or not utf8.len(Text) or string.find(Text,'\0',1,true) then Status.Text='Choose a UTF-8 text file of at most16,000 bytes.'; return end
            if Editor.Text~=Before then Status.Text='Newer edits were kept. Open again when ready.'; return end
            Editor.Text=Text; Status.Text='Opened '..File.Name..'. Save as writes a copy.'
        end)
    end)
end)
Button('Save as',2,function()
    if #Editor.Text>16000 then Status.Text='Shorten the text to16,000 UTF-8 bytes before saving.'; return end
    Busy = true
    local Text = Editor.Text -- Capture now; later edits do not change this save.
    Picker:SaveFile('note.txt',function(File, Failure)
        if not File then Busy=false; Status.Text=Failure or 'Save cancelled.'; return end
        File:Write(buffer.fromstring(Text),function(WriteFailure)
            File:Close(); Busy=false
            Status.Text=if WriteFailure then 'Save could not be confirmed; your text is kept.'
                elseif Editor.Text~=Text then 'Saved the earlier version. New edits still need saving.' else 'File saved.'
        end)
    end)
end)
Button('Open folder',3,function()
    Busy = true
    Picker:OpenFolder(function(Folder, Failure)
        if not Folder then Busy=false; Status.Text=Failure or 'Selection cancelled.'; return end
        Folder:List(function(Entries, ListFailure)
            Folder:Close(); Busy=false
            Status.Text=if Entries then tostring(#Entries)..' entries in '..Folder.Name else ListFailure or 'List failed.'
        end)
    end)
end)
Window.CloseRequested:Connect(function() if Busy then Window:PreventClose(); Status.Text='Wait for the current file task.' end end)
local function Retire() if not Alive then return end; Alive=false; Picker:Dispose() end
Window.Destroying:Connect(Retire)
Window.Closed:Connect(function() Retire(); Window:Destroy() end)
Window.Visible=true
