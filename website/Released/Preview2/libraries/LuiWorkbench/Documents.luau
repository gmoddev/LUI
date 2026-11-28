--!strict
-- Text ownership without filesystem or UI authority.
local Documents = {}

export type Document = {read Id: string, read Label: string, read Text: string, read Revision: number, read Dirty: boolean}
export type Receipt = {read Id: string, read Revision: number, read Text: string}
export type LoadReceipt = {read Id: string, read Revision: number}
export type Store = {
    Open: (Self: Store, Id: string, Label: string, Text: string) -> Document,
    Get: (Self: Store, Id: string) -> Document?,
    List: (Self: Store) -> {Document},
    SetText: (Self: Store, Id: string, Text: string) -> Document,
    BeginLoad: (Self: Store, Id: string) -> LoadReceipt,
    CompleteLoad: (Self: Store, Token: LoadReceipt, Text: string?) -> (boolean, string?),
    CancelLoad: (Self: Store, Token: LoadReceipt) -> boolean,
    BeginSave: (Self: Store, Id: string) -> Receipt,
    CompleteSave: (Self: Store, Token: Receipt, Succeeded: boolean) -> (boolean, string?),
    CancelSave: (Self: Store, Token: Receipt) -> boolean,
    Close: (Self: Store, Id: string, Discard: boolean?) -> (boolean, string?),
    Move: (Self: Store, Id: string, Index: number) -> (),
    Dispose: (Self: Store) -> (),
}

local function Text(Value: any, Maximum: number, Empty: boolean): boolean
    return type(Value) == 'string' and #Value <= Maximum and (Empty or #Value > 0)
        and utf8.len(Value) ~= nil and not string.find(Value, '\0', 1, true)
end

function Documents.New(): Store
    local Records: {[string]: any} = {}
    local Order: {string} = {}
    local Pending: {[Receipt]: any} = {}
    local Loads: {[LoadReceipt]: any} = {}
    local Alive = true
    local Value: any = {}
    local function Live()
        assert(Alive, '[LUI:Documents] Retired')
    end
    local function Record(Id: string): any
        Live()
        local Entry = Records[Id]
        assert(Entry ~= nil, '[LUI:Documents] Missing')
        return Entry
    end
    local function View(Entry: any): Document
        return table.freeze({Id = Entry.Id, Label = Entry.Label, Text = Entry.Text,
            Revision = Entry.Revision, Dirty = Entry.Text ~= Entry.Saved})
    end
    function Value.Open(_Self: any, Id: string, Label: string, Initial: string): Document
        Live()
        assert(Text(Id, 128, false) and Text(Label, 256, false) and Text(Initial, 65534, true), '[LUI:Documents] InvalidDocument')
        if Records[Id] then return View(Records[Id]) end
        assert(#Order < 32, '[LUI:Documents] DocumentLimit')
        local Entry = {Id = Id, Label = Label, Text = Initial, Saved = Initial, Revision = 1, Saving = false}
        Records[Id] = Entry; table.insert(Order, Id)
        return View(Entry)
    end
    function Value.Get(_Self: any, Id: string): Document?
        Live()
        return if Records[Id] then View(Records[Id]) else nil
    end
    function Value.List(_Self: any): {Document}
        Live()
        local Result: {Document} = {}
        for _, Id in Order do table.insert(Result, View(Records[Id])) end
        return table.freeze(Result)
    end
    function Value.SetText(_Self: any, Id: string, Next: string): Document
        local Entry = Record(Id)
        assert(Text(Next, 65534, true), '[LUI:Documents] InvalidText')
        if Entry.Text ~= Next then
            assert(Entry.Revision < 9007199254740991, '[LUI:Documents] RevisionLimit')
            Entry.Text = Next; Entry.Revision += 1
        end
        return View(Entry)
    end
    function Value.BeginSave(_Self: any, Id: string): Receipt
        local Entry = Record(Id)
        assert(not Entry.Saving, '[LUI:Documents] SavePending')
        if Entry.Loading then Loads[Entry.Loading] = nil; Entry.Loading = nil end
        local Token: Receipt = table.freeze({Id = Id, Revision = Entry.Revision, Text = Entry.Text})
        Pending[Token] = Entry; Entry.Saving = true
        return Token
    end
    function Value.BeginLoad(_Self: any, Id: string): LoadReceipt
        local Entry = Record(Id)
        assert(not Entry.Saving, '[LUI:Documents] SavePending')
        assert(not Entry.Loading, '[LUI:Documents] LoadPending')
        assert(Entry.Text == Entry.Saved, '[LUI:Documents] Dirty')
        local Token: LoadReceipt = table.freeze({Id = Id, Revision = Entry.Revision})
        Loads[Token] = Entry
        Entry.Loading = Token
        return Token
    end
    function Value.CompleteLoad(_Self: any, Token: LoadReceipt, Next: string?): (boolean, string?)
        if not Alive then return false, 'Retired' end
        local Entry = Loads[Token]
        if not Entry then return false, 'Stale' end
        Loads[Token] = nil
        Entry.Loading = nil
        if Records[Token.Id] ~= Entry or Entry.Revision ~= Token.Revision then return false, 'Stale' end
        if Next == nil then return false, 'Failed' end
        if not Text(Next, 65534, true) then return false, 'InvalidText' end
        if Entry.Revision == 9007199254740991 then return false, 'RevisionLimit' end
        Entry.Text = Next
        Entry.Saved = Next
        Entry.Revision += 1
        return true, nil
    end
    function Value.CancelLoad(_Self: any, Token: LoadReceipt): boolean
        local Entry = Loads[Token]
        if not Entry then return false end
        Loads[Token] = nil
        Entry.Loading = nil
        return true
    end
    function Value.CompleteSave(_Self: any, Token: Receipt, Succeeded: boolean): (boolean, string?)
        if not Alive then return false, 'Retired' end
        assert(type(Succeeded) == 'boolean', '[LUI:Documents] InvalidResult')
        local Entry = Pending[Token]
        if not Entry or Records[Token.Id] ~= Entry then return false, 'Stale' end
        Pending[Token] = nil; Entry.Saving = false
        if not Succeeded then return false, 'Failed' end
        Entry.Saved = Token.Text
        return true, nil
    end
    function Value.CancelSave(_Self: any, Token: Receipt): boolean
        local Entry = Pending[Token]
        if not Entry then return false end
        Pending[Token] = nil; Entry.Saving = false
        return true
    end
    function Value.Close(_Self: any, Id: string, Discard: boolean?): (boolean, string?)
        if not Alive then return false, 'Retired' end
        assert(Discard == nil or type(Discard) == 'boolean', '[LUI:Documents] InvalidDiscard')
        local Entry = Records[Id]
        if not Entry then return false, 'Missing' end
        if Entry.Saving then return false, 'SavePending' end
        if Entry.Text ~= Entry.Saved and not Discard then return false, 'Dirty' end
        if Entry.Loading then Loads[Entry.Loading] = nil end
        Records[Id] = nil
        local Index = table.find(Order, Id)
        if Index then table.remove(Order, Index) end
        return true, nil
    end
    function Value.Move(_Self: any, Id: string, Index: number)
        Record(Id)
        assert(type(Index) == 'number' and Index % 1 == 0 and Index >= 1 and Index <= #Order, '[LUI:Documents] InvalidIndex')
        local Previous = table.find(Order, Id)
        if Previous then table.remove(Order, Previous) end
        table.insert(Order, Index, Id)
    end
    function Value.Dispose(_Self: any)
        if not Alive then return end
        Alive = false; Records = {}; Order = {}; Pending = {}; Loads = {}
    end
    return table.freeze(Value) :: Store
end

return table.freeze(Documents)
