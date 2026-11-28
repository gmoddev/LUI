--!strict
-- User-selected documents. Provider buffers/handles stay inside this adapter.
local Files = {}
local OnFileClose: {[any]: {[(boolean) -> ()]: boolean}} = setmetatable({}, {__mode="k"}) :: any
export type Operation = {read Cancel: (Self: Operation) -> boolean}
export type Entry = {read Name: string, read IsDirectory: boolean, read Size: number}
export type Writer = {
    read Position: (Self: Writer) -> number,
    read Seek: (Self: Writer, Offset: number) -> (),
    read Write: (Self: Writer, Bytes: buffer, Complete: (string?) -> ()) -> Operation,
    read Commit: (Self: Writer, Complete: (string?) -> ()) -> Operation,
    read Close: (Self: Writer) -> (),
}
export type File = {
    read Name: string, read Writable: boolean,
    read Read: (Self: File, Complete: (buffer?, string?) -> ()) -> Operation,
    read ReadRange: (Self: File, Position: number, Count: number, Complete: (buffer?, string?) -> ()) -> Operation,
    read ByteLength: (Self: File, Complete: (number?, string?) -> ()) -> Operation,
    read BeginWriter: (Self: File, Complete: (Writer?, string?) -> ()) -> Operation,
    read Write: (Self: File, Bytes: buffer, Complete: (string?) -> ()) -> Operation,
    read Close: (Self: File) -> (),
}
export type Folder = {
    read Name: string,
    read List: (Self: Folder, Complete: ({Entry}?, string?) -> ()) -> Operation,
    read Open: (Self: Folder, Name: string, Complete: (File?, string?) -> ()) -> Operation,
    read OpenOutput: (Self: Folder, Name: string, Complete: (File?, string?) -> ()) -> Operation,
    read CreateDirectory: (Self: Folder, Name: string, Complete: (string?) -> ()) -> Operation,
    read RemoveDirectory: (Self: Folder, Name: string, Complete: (string?) -> ()) -> Operation,
    read RemoveFile: (Self: Folder, Name: string, Complete: (string?) -> ()) -> Operation,
    read Rename: (Self: Folder, Name: string, Destination: string, Complete: (string?) -> ()) -> Operation,
    read Close: (Self: Folder) -> (),
}
export type Reader = {
    read Read: (Self: Reader, Count: number, Complete: (buffer?, string?) -> ()) -> Operation,
    read Seek: (Self: Reader, Offset: number) -> (),
    read Position: (Self: Reader) -> number,
    read Close: (Self: Reader) -> (),
}
local function ValidOffset(Position: number): boolean
    return type(Position) == "number" and Position % 1 == 0 and Position >= 0 and Position <= 9007199254740991
end

function Files.Reader(Document: File, Offset: number?): Reader
    local Source: File? = Document
    local Position = if Offset == nil then 0 else Offset
    assert(ValidOffset(Position), "[LUI:Files] InvalidRange")
    local Closed = false
    local Active: {Request: Operation?}? = nil
    local function Cancel(Ticket: {Request: Operation?}): boolean
        if Active ~= Ticket then return false end
        Active = nil
        if Ticket.Request then Ticket.Request:Cancel() end
        Ticket.Request = nil
        return true
    end
    return table.freeze({
        Position = function(_Self: Reader): number assert(not Closed, "[LUI:Files] Closed"); return Position end,
        Seek = function(_Self: Reader, NewPosition: number)
            assert(not Closed and not Active, "[LUI:Files] ClosedOrBusy")
            assert(ValidOffset(NewPosition), "[LUI:Files] InvalidRange")
            Position = NewPosition
        end,
        Read = function(_Self: Reader, Count: number, Complete: (buffer?, string?) -> ()): Operation
            assert(not Closed and not Active and type(Complete) == "function", "[LUI:Files] ClosedOrBusy")
            local Ticket: {Request: Operation?} = {Request = nil}
            Active = Ticket
            local Ok, Request = pcall(function()
                return assert(Source):ReadRange(Position, Count, function(Bytes, Failure)
                    if Active ~= Ticket then return end
                    Active = nil; Ticket.Request = nil
                    if Bytes and not Failure then
                        local Next = Position + buffer.len(Bytes)
                        if not ValidOffset(Next) then Complete(nil, "LimitExceeded"); return end
                        Position = Next
                    end
                    Complete(Bytes, Failure)
                end)
            end)
            if not Ok then
                if Active == Ticket then Active = nil end
                error("[LUI:Files] ReadRejected", 0)
            end
            if Active == Ticket then Ticket.Request = Request end
            return table.freeze({Cancel = function(_Token: Operation): boolean return Cancel(Ticket) end})
        end,
        Close = function(_Self: Reader)
            Closed = true
            if Active then Cancel(Active) end
            Source = nil
        end,
    }) :: Reader
end
function Files.Copy(Source: File, Destination: File, Complete: (string?) -> ()): Operation
    assert(Destination.Writable and type(Complete) == "function", "[LUI:Files] WritableFileRequired")
    local Input = Files.Reader(Source)
    local Output: Writer? = nil
    local Active: Operation? = nil
    local Done = false
    local Closed: (boolean) -> ()
    local function Retire()
        if OnFileClose[Source] then OnFileClose[Source][Closed] = nil end
        if OnFileClose[Destination] then OnFileClose[Destination][Closed] = nil end
        Done = true; Input:Close()
        if Output then Output:Close(); Output = nil end
        Active = nil
    end
    local function Finish(Failure: string?)
        if Done then return end
        Retire(); Complete(Failure)
    end
    Closed = function(Notify: boolean)
        if Done then return end
        Done = true
        local Pending = Active
        if Pending then Pending:Cancel() end
        Retire()
        if Notify then Complete("Closed") end
    end
    if OnFileClose[Source] then OnFileClose[Source][Closed] = true end
    if OnFileClose[Destination] then OnFileClose[Destination][Closed] = true end
    local Serial, Copied = 0, 0
    local function Submit(Action: () -> Operation)
        if Done then return end
        Serial += 1
        local This = Serial
        local Ok, Request = pcall(Action)
        if not Ok then Finish("Unavailable")
        elseif not Done and This == Serial then Active = Request end
    end
    local function Next()
        Submit(function() return Input:Read(65536,function(Bytes, Failure)
            if Done then return end
            if Failure or not Bytes then Finish(Failure or "Unavailable"); return end
            local Writer = assert(Output)
            if buffer.len(Bytes) == 0 then Submit(function() return Writer:Commit(Finish) end); return end
            Copied += buffer.len(Bytes)
            if Copied > 134217728 then Finish("LimitExceeded"); return end
            Submit(function() return Writer:Write(Bytes,function(Error)
                if Error then Finish(Error) elseif not Done then Next() end
            end) end)
        end) end)
    end
    Submit(function() return Destination:BeginWriter(function(Writer, Failure)
        if Done then if Writer then Writer:Close() end; return end
        if Failure or not Writer then Finish(Failure or "Unavailable"); return end
        Output = Writer; Next()
    end) end)
    return table.freeze({Cancel=function(_Self: Operation): boolean
        if Done then return false end
        Closed(false); return true
    end})
end

export type Picker = {
    read OpenFile: (Self: Picker, Complete: (File?, string?) -> ()) -> Operation,
    read SaveFile: (Self: Picker, SuggestedName: string, Complete: (File?, string?) -> ()) -> Operation,
    read OpenFolder: (Self: Picker, Complete: (Folder?, string?) -> ()) -> Operation,
    read Dispose: (Self: Picker) -> (),
}
function Files.New(Service: FileService?): Picker
    local Alive = true
    local Operations: {[any]: boolean} = {}
    local Resources: {[any]: boolean} = {}
    local Provider: any = Service
    local function Start(Method: string, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> ()): Operation
        assert(Alive and type(Complete) == 'function', '[LUI:Files] RetiredOrInvalidCompletion')
        local Token: any = {Request = nil, Complete = Complete}
        Operations[Token] = true
        local function Finish(Bytes: buffer?, Opened: NativeHandle?, Failure: string?)
            if not Alive or not Operations[Token] then if Opened then Opened:Close() end; return end
            Operations[Token] = nil; Token.Request = nil
            local Callback = Token.Complete; Token.Complete = nil
            local Ok = pcall(Callback, Bytes, Opened, Failure)
            if not Ok then if Opened then Opened:Close() end; print('[LUI:Files] CompletionFailed') end
        end
        local Ok, Request = pcall(function()
            if not Provider then Finish(nil, nil, 'Unavailable'); return nil end
            return Provider[Method](Provider, Input, Handle, Finish)
        end)
        if not Ok then Finish(nil, nil, 'Unavailable')
        elseif Operations[Token] then Token.Request = Request end
        return table.freeze({Cancel = function(_Self: any): boolean
            if not Operations[Token] then return false end
            Operations[Token] = nil
            local Request = Token.Request; Token.Request = nil; Token.Complete = nil
            if Request then Request:Cancel() end
            return true
        end}) :: Operation
    end
    local function WrapWriter(Handle: NativeHandle): Writer
        local Writer: any = {}
        local Owned: NativeHandle? = Handle
        local Position = 0
        local Active: {Request: Operation?}? = nil
        Resources[Writer] = true
        function Writer.Close(_Self: any)
            local Pending = Active
            Active = nil
            if Pending and Pending.Request then Pending.Request:Cancel(); Pending.Request = nil end
            Resources[Writer] = nil
            local Previous = Owned
            Owned = nil
            if Previous then Previous:Close() end
        end
        function Writer.Position(_Self: any): number
            assert(Owned, '[LUI:Files] Closed')
            return Position
        end
        function Writer.Seek(_Self: any, Offset: number)
            assert(Owned and not Active, '[LUI:Files] ClosedOrBusy')
            assert(ValidOffset(Offset) and Offset <= 134217728, '[LUI:Files] InvalidRange')
            Position = Offset
        end
        local function Submit(Method: string, Input: buffer, Count: number, Complete: (string?) -> ()): Operation
            assert(Owned and not Active and type(Complete)=='function', '[LUI:Files] ClosedOrBusy')
            local Ticket: {Request: Operation?} = {Request=nil}
            Active = Ticket
            local Request = Start(Method, Input, Owned, function(_, _, Failure)
                if Active ~= Ticket then return end
                Active = nil
                Ticket.Request = nil
                if Failure or Method=='CommitWriter' then Writer:Close() else Position += Count end
                Complete(Failure)
            end)
            if Active == Ticket then Ticket.Request = Request end
            return table.freeze({Cancel=function(_Token: Operation): boolean
                if Active ~= Ticket then return false end
                Writer:Close()
                return true
            end})
        end
        function Writer.Write(_Self: any, Bytes: buffer, Complete: (string?) -> ()): Operation
            assert(typeof(Bytes)=='buffer' and buffer.len(Bytes)<=65536 and Position+buffer.len(Bytes)<=134217728, '[LUI:Files] ByteLimit')
            local Input = buffer.create(8 + buffer.len(Bytes))
            buffer.writeu32(Input,0,Position)
            buffer.copy(Input,8,Bytes)
            return Submit('WriteAt',Input,buffer.len(Bytes),Complete)
        end
        function Writer.Commit(_Self: any, Complete: (string?) -> ()): Operation
            return Submit('CommitWriter',buffer.create(0),0,Complete)
        end
        return table.freeze(Writer) :: Writer
    end
    local Wrap: (NativeHandle, string, boolean, boolean) -> any
    Wrap = function(Handle: NativeHandle, Name: string, IsFolder: boolean, Writable: boolean): any
        local OwnedHandle: NativeHandle? = Handle
        local Resource: any = {Name = Name}
        Resources[Resource] = true
        if not IsFolder then OnFileClose[Resource] = {} end
        local function Check(): NativeHandle
            assert(Alive and Resources[Resource], '[LUI:Files] Closed')
            return assert(OwnedHandle, '[LUI:Files] Closed')
        end
        function Resource.Close(_Self: any)
            if not Resources[Resource] then return end
            Resources[Resource] = nil
            local Previous = OwnedHandle; OwnedHandle = nil
            if Previous then Previous:Close() end
            local List = OnFileClose[Resource]
            OnFileClose[Resource] = nil
            if List then for Notify in List do pcall(Notify,Alive) end end
        end
        if IsFolder then
            function Resource.List(_Self: any, Complete: ({Entry}?, string?) -> ()): Operation
                local CurrentHandle = Check()
                return Start('List', buffer.create(0), CurrentHandle, function(Bytes, _, Failure)
                    if not Bytes or Failure then Complete(nil, Failure or 'Unavailable'); return end
                    local Entries: {Entry} = {}
                    for Line in string.gmatch(buffer.tostring(Bytes), '[^\n]+') do
                        local NameValue, Kind, Length = string.match(Line, '^(.-)\t(File)\t(%d+)$')
                        if not NameValue then NameValue, Kind, Length = string.match(Line, '^(.-)\t(Folder)\t(%d+)$') end
                        if not NameValue or not Kind or not Length or #Entries == 256 then Complete(nil, 'InvalidResult'); return end
                        table.insert(Entries, table.freeze({Name = NameValue, IsDirectory = Kind == 'Folder', Size = tonumber(Length) :: number}))
                    end
                    Complete(Entries, nil)
                end)
            end
            local function OpenChild(Method: string, Child: string, Complete: (File?, string?) -> ()): Operation
                local CurrentHandle = Check()
                return Start(Method, buffer.fromstring(Child), CurrentHandle, function(Bytes, Opened, Failure)
                    if Failure or not Bytes or not Opened then if Opened then Opened:Close() end; Complete(nil, Failure or 'Unavailable'); return end
                    local Child = Wrap(Opened, buffer.tostring(Bytes), false, Method == "OpenOutput")
                    if not pcall(Complete, Child, nil) then Child:Close(); error('[LUI:Files] CompletionFailed') end
                end)
            end
            function Resource.Open(_Self: any, Child: string, Complete: (File?, string?) -> ()): Operation
                return OpenChild("Open",Child,Complete)
            end
            function Resource.OpenOutput(_Self: any, Child: string, Complete: (File?, string?) -> ()): Operation
                return OpenChild("OpenOutput",Child,Complete)
            end
            local function Change(Method: string, Name: string, Complete: (string?) -> ()): Operation
                return Start(Method,buffer.fromstring(Name),Check(),function(_, _, Failure) Complete(Failure) end)
            end
            for _, Method in {"CreateDirectory","RemoveDirectory","RemoveFile"} do
                Resource[Method] = function(_Self: any, Child: string, Complete: (string?) -> ()): Operation
                    return Change(Method,Child,Complete)
                end
            end
            function Resource.Rename(_Self: any, From: string, To: string, Complete: (string?) -> ()): Operation
                assert(not string.find(From,"\0",1,true) and not string.find(To,"\0",1,true), "[LUI:Files] InvalidName")
                return Change("Rename",From.."\0"..To,Complete)
            end
        else
            Resource.Writable = Writable
            function Resource.BeginWriter(_Self: any, Complete: (Writer?, string?) -> ()): Operation
                assert(Writable and type(Complete)=='function', '[LUI:Files] WritableFileRequired')
                return Start('BeginWriter',buffer.create(0),Check(),function(_, Opened, Failure)
                    if Failure or not Opened then
                        if Opened then Opened:Close() end
                        Complete(nil,Failure or 'Unavailable')
                        return
                    end
                    local Writer = WrapWriter(Opened)
                    if not pcall(Complete,Writer,nil) then Writer:Close(); error('[LUI:Files] CompletionFailed') end
                end)
            end
            function Resource.Read(_Self: any, Complete: (buffer?, string?) -> ()): Operation
                local CurrentHandle = Check()
                return Start('Read', buffer.create(0), CurrentHandle, function(Bytes, _, Failure) Complete(Bytes, Failure) end)
            end
            function Resource.ReadRange(_Self: any, Position: number, Count: number, Complete: (buffer?, string?) -> ()): Operation
                local CurrentHandle = Check()
                assert(type(Complete) == 'function' and ValidOffset(Position) and type(Count) == 'number' and Count % 1 == 0 and
                    Count >= 1 and Count <= 65536, '[LUI:Files] InvalidRange')
                local Input = buffer.create(12)
                buffer.writeu32(Input, 0, Position % 4294967296)
                buffer.writeu32(Input, 4, math.floor(Position / 4294967296))
                buffer.writeu32(Input, 8, Count)
                return Start('ReadRange', Input, CurrentHandle, function(Bytes, _, Failure) Complete(Bytes, Failure) end)
            end
            function Resource.ByteLength(_Self: any, Complete: (number?, string?) -> ()): Operation
                assert(type(Complete) == 'function', '[LUI:Files] InvalidCompletion')
                return Start('ByteLength', buffer.create(0), Check(), function(Bytes, _, Failure)
                    if not Bytes or Failure then Complete(nil, Failure or 'Unavailable'); return end
                    local Size = tonumber(buffer.tostring(Bytes))
                    if not Size or Size > 9007199254740991 then Complete(nil, 'LimitExceeded'); return end
                    Complete(Size, nil)
                end)
            end
            function Resource.Write(_Self: any, Bytes: buffer, Complete: (string?) -> ()): Operation
                local CurrentHandle = Check()
                assert(Writable, '[LUI:Files] ReadOnly')
                assert(typeof(Bytes) == 'buffer' and buffer.len(Bytes) <= 4*1024*1024, '[LUI:Files] ByteLimit')
                return Start('Write', Bytes, CurrentHandle, function(_, _, Failure) Complete(Failure) end)
            end
        end
        return table.freeze(Resource)
    end
    local Picker: any = {}
    local function Pick(Method: string, Name: string, FolderValue: boolean, Writable: boolean, Complete: (any, string?) -> ()): Operation
        return Start(Method, buffer.fromstring(Name), nil, function(Bytes, Handle, Failure)
            if Failure == 'Canceled' then if Handle then Handle:Close() end; Complete(nil, nil); return end
            if Failure or not Handle or not Bytes then if Handle then Handle:Close() end; Complete(nil, Failure); return end
            local Resource = Wrap(Handle, buffer.tostring(Bytes), FolderValue, Writable)
            if not pcall(Complete, Resource, nil) then Resource:Close(); error('[LUI:Files] CompletionFailed') end
        end)
    end
    function Picker.OpenFile(_Self: any, Complete: (File?, string?) -> ()): Operation return Pick('OpenFile', '', false, false, Complete) end
    function Picker.SaveFile(_Self: any, Name: string, Complete: (File?, string?) -> ()): Operation return Pick('SaveFile', Name, false, true, Complete) end
    function Picker.OpenFolder(_Self: any, Complete: (Folder?, string?) -> ()): Operation return Pick('OpenFolder', '', true, false, Complete) end
    function Picker.Dispose(_Self: any)
        if not Alive then return end
        Alive = false
        for Token in Operations do
            Operations[Token] = nil
            local Request = Token.Request; Token.Request = nil; Token.Complete = nil
            if Request then Request:Cancel() end
        end
        for Resource in Resources do Resource:Close() end
    end
    return table.freeze(Picker) :: Picker
end
return table.freeze(Files)
