--!strict
local Files = require('@LuiWorkbench/Files')
local Pending: {any} = {}
local Canceled, Closed = 0, 0
local WeakHandles: any = setmetatable({}, {__mode='v'})
local NextHandle = 0
local Provider: any = {}
for _, Name in {'OpenFile','SaveFile','OpenFolder','Read','ReadRange','ByteLength','Write','List','Open','BeginWriter','WriteAt','CommitWriter'} do
    Provider[Name] = function(_Self: any, Input: buffer, Handle: any, Complete: any): any
        table.insert(Pending, {Name=Name, Input=buffer.tostring(Input), Handle=Handle, Complete=Complete})
        return {Cancel=function() Canceled += 1 end}
    end
end
local function Handle(): any
    local Alive = true
    local Value = {Close=function() if Alive then Closed += 1; Alive = false end end}
    NextHandle += 1; WeakHandles[NextHandle] = Value
    return Value
end
local function Complete(Bytes: string?, Resource: any?, Failure: string?)
    local Request = table.remove(Pending, 1)
    Request.Complete(if Bytes then buffer.fromstring(Bytes) else nil, Resource, Failure)
end
local Picker = Files.New(Provider)
local Chosen: Files.File? = nil
Picker:OpenFile(function(File, Failure) assert(not Failure); Chosen = File end)
assert(Pending[1].Name=='OpenFile' and Pending[1].Input=='')
Complete('note.txt', Handle(), nil)
local File = assert(Chosen)
assert(File.Name=='note.txt' and not File.Writable)
File:Read(function(Bytes, Failure) assert(not Failure and buffer.tostring(assert(Bytes))=='hello') end)
Complete('hello', nil, nil)
local Stream = Files.Reader(File, 4294967303)
assert(Stream:Position() == 4294967303)
local Deliveries: {{Data: string?, Failure: string?}} = {}
local function Receive(Bytes: buffer?, Failure: string?)
    table.insert(Deliveries, {Data = if Bytes then buffer.tostring(Bytes) else nil, Failure = Failure})
end
Stream:Read(4, Receive)
local Header = buffer.fromstring(Pending[1].Input)
assert(buffer.readu32(Header,0)==7 and buffer.readu32(Header,4)==1 and buffer.readu32(Header,8)==4)
assert(not pcall(function() Stream:Seek(0) end))
Complete('ab',nil,nil)
assert(Stream:Position()==4294967305 and #Deliveries==1 and Deliveries[1].Data=='ab' and not Deliveries[1].Failure)
Stream:Seek(7)
local CanceledRead = Stream:Read(1, Receive)
assert(CanceledRead:Cancel() and not CanceledRead:Cancel())
Complete('x',nil,nil)
assert(Stream:Position()==7 and #Deliveries==1)
Stream:Read(1, function(Bytes, Failure)
    Receive(Bytes,Failure)
    Stream:Read(4, Receive)
end)
Complete('z',nil,nil)
Complete('',nil,nil)
assert(Stream:Position()==8 and #Deliveries==3 and Deliveries[2].Data=='z' and Deliveries[3].Data=='')
Stream:Read(1, Receive)
Stream:Close(); Stream:Close()
Complete('x',nil,nil)
assert(#Deliveries==3)
assert(not pcall(function() Stream:Seek(0) end))
assert(not pcall(function() File:Write(buffer.fromstring('denied'),function() end) end))
File:Close(); File:Close(); assert(Closed==1)
assert(not pcall(function() File:Read(function() end) end))
Picker:OpenFile(function(Value, Failure) assert(not Value and not Failure) end)
Complete(nil, nil, 'Canceled')
-- Failed recipient callbacks must release the newly wrapped resource, not keep
-- a closed handle strongly retained in the picker until disposal.
local FailedFile: Files.File? = nil
Picker:OpenFile(function(Value) FailedFile = Value; error('APP_COMPLETION_CANARY') end)
Complete('failed.txt', Handle(), nil); assert(Closed==2)
assert(not pcall(function() assert(FailedFile):Read(function() end) end) and #Pending==0)
local Folder: Files.Folder? = nil
Picker:OpenFolder(function(Value) Folder = Value end)
Complete('selected', Handle(), nil)
local Directory = assert(Folder)
Directory:List(function(Entries, Failure)
    assert(not Failure and Entries and #Entries==2 and Entries[1].Name=='first.txt' and not Entries[1].IsDirectory and Entries[1].Size==12 and Entries[2].IsDirectory)
end)
Complete('first.txt\tFile\t12\nchild\tFolder\t0\n',nil,nil)
local FailedChild: Files.File? = nil
Directory:Open('first.txt',function(Value) FailedChild = Value; error('CHILD_COMPLETION_CANARY') end)
Complete('first.txt',Handle(),nil); assert(Closed==3)
assert(not pcall(function() assert(FailedChild):Read(function() end) end) and #Pending==0)
local Called = false
local Request = Picker:OpenFile(function() Called = true end)
assert(Request:Cancel() and not Request:Cancel() and Canceled==3)
Complete('late.txt',Handle(),nil); assert(not Called and Closed==4)
Picker:SaveFile('copy.txt',function(Save, Failure)
    assert(not Failure and Save and Save.Writable)
    local Bytes = buffer.fromstring('saved')
    Save:Write(Bytes,function(WriteFailure) assert(not WriteFailure) end)
    assert(Pending[1].Name=='Write' and Pending[1].Input=='saved')
end)
Complete('copy.txt',Handle(),nil); Complete(nil,nil,nil)
Picker:OpenFile(function() error('DISPOSED_CALLBACK') end)
Picker:Dispose(); Picker:Dispose(); assert(Closed==6 and Canceled==4)
Complete('retired.txt',Handle(),nil); assert(Closed==7)
assert(not pcall(function() Picker:OpenFile(function() end) end))
-- Retain closed wrappers as a file history while bounded allocation advances
-- the ordinary VM collector. They must not keep underlying handle objects alive.
local History = {File, Directory, FailedFile, FailedChild}
local WeakDocuments: any = setmetatable({}, {__mode='v'})
local Retained: Files.Reader
do
    local Document: any = {}
    WeakDocuments[1] = Document
    Retained = Files.Reader(Document)
    Retained:Close()
end
for _Index = 1, 6000 do local Pressure = buffer.create(1024); buffer.writeu8(Pressure,0,1) end
assert(next(WeakHandles)==nil and #History==4, 'Closed wrappers retained their owned handles')
assert(next(WeakDocuments)==nil and Retained, 'Closed reader retained its document')
-- Independent picker owners can retire either side while a copy awaits native I/O.
for _, Mode in {"source-close","source-dispose","cancel"} do
    local Left = Files.New(Provider)
    local Right = Files.New(Provider)
    local Input: Files.File? = nil
    local Output: Files.File? = nil
    Left:OpenFile(function(F) Input = F end)
    Complete("input",Handle(),nil)
    Right:SaveFile("output",function(F) Output = F end)
    Complete("output",Handle(),nil)
    local Calls, Why = 0, ""
    local Transfer = Files.Copy(assert(Input),assert(Output),function(Failure) Calls += 1; Why = Failure or "" end)
    if Mode == "source-close" then
        assert(Input):Close()
        Complete(nil,Handle(),nil)
        assert(Calls == 1 and Why == "Closed")
    else
        Complete(nil,Handle(),nil)
        assert(Pending[1].Name == "ReadRange")
        if Mode == "source-dispose" then Left:Dispose() else assert(Transfer:Cancel()) end
        Complete("late",nil,nil)
        assert(Calls == 0)
    end
    assert(not Transfer:Cancel() and #Pending == 0)
    Left:Dispose(); Right:Dispose()
end
return true
