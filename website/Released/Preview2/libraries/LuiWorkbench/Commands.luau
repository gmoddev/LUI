--!strict
-- Optional, scheduler-local command registry. No work occurs at module load.
local Commands = {}

export type Shortcut = {read Key: string, read Control: boolean?, read Shift: boolean?, read Alt: boolean?}
export type Command = {
    read Id: string, read Label: string, read Category: string?, read Shortcut: Shortcut?,
    read Enabled: (() -> boolean)?, read Execute: () -> (),
}
export type Row = {read Id: string, read Label: string, read Category: string, read Enabled: boolean, read Shortcut: Shortcut?}
export type Registry = {
    Register: (Self: Registry, Definition: Command) -> (() -> ()),
    List: (Self: Registry, Query: string?, Limit: number?) -> {Row},
    Execute: (Self: Registry, Id: string) -> (boolean, string?),
    Dispatch: (Self: Registry, Input: KeyboardShortcut) -> (boolean, string?),
    Dispose: (Self: Registry) -> (),
}

local function Text(Value: any, Limit: number): boolean
    return type(Value) == 'string' and #Value > 0 and #Value <= Limit
        and utf8.len(Value) ~= nil and not string.find(Value, '\0', 1, true)
end
local function Same(Left: Shortcut, Right: Shortcut): boolean
    return Left.Key == Right.Key and (Left.Control == true) == (Right.Control == true)
        and (Left.Shift == true) == (Right.Shift == true) and (Left.Alt == true) == (Right.Alt == true)
end

function Commands.New(): Registry
    local Entries: {[string]: Command} = {}
    local Identities: {[string]: {}} = {}
    local Count = 0
    local Alive, Executing = true, false
    local Listing = false
    local Value: any = {}
    local function Enabled(Entry: Command): boolean
        if not Entry.Enabled then return true end
        local Ok, Result = pcall(Entry.Enabled)
        return Ok and Result == true
    end
    function Value.Register(_Self: any, Definition: Command): () -> ()
        assert(Alive and not Executing and not Listing, '[LUI:Commands] RetiredOrBusy')
        assert(type(Definition) == 'table' and getmetatable(Definition) == nil, '[LUI:Commands] PlainDefinitionRequired')
        assert(Text(Definition.Id, 128) and Text(Definition.Label, 256)
            and (Definition.Category == nil or Text(Definition.Category, 128))
            and type(Definition.Execute) == 'function'
            and (Definition.Enabled == nil or type(Definition.Enabled) == 'function'), '[LUI:Commands] InvalidCommand')
        assert(not Entries[Definition.Id] and Count < 128, '[LUI:Commands] DuplicateOrLimit')
        local Binding = Definition.Shortcut
        if Binding then
            assert(type(Binding) == 'table' and getmetatable(Binding) == nil, '[LUI:Commands] PlainShortcutRequired')
            assert(Text(Binding.Key, 32) and (Binding.Control == nil or type(Binding.Control) == 'boolean')
                and (Binding.Shift == nil or type(Binding.Shift) == 'boolean')
                and (Binding.Alt == nil or type(Binding.Alt) == 'boolean'), '[LUI:Commands] InvalidShortcut')
            for _, Entry in Entries do
                assert(not Entry.Shortcut or not Same(Entry.Shortcut, Binding), '[LUI:Commands] ShortcutConflict')
            end
            Binding = table.freeze({Key = Binding.Key, Control = Binding.Control, Shift = Binding.Shift, Alt = Binding.Alt})
        end
        local Entry: Command = table.freeze({Id = Definition.Id, Label = Definition.Label,
            Category = Definition.Category, Shortcut = Binding, Enabled = Definition.Enabled, Execute = Definition.Execute})
        Entries[Entry.Id] = Entry; Count += 1
        local Id = Entry.Id
        local Identity = {}
        Identities[Id] = Identity
        return function()
            if Identities[Id] == Identity then Entries[Id] = nil; Identities[Id] = nil; Count -= 1 end
        end
    end
    function Value.List(_Self: any, Query: string?, Limit: number?): {Row}
        assert(Alive and not Listing, '[LUI:Commands] RetiredOrListing')
        local Search = Query or ''
        local Maximum = Limit or 12
        assert(type(Search) == 'string' and #Search <= 256 and utf8.len(Search) ~= nil
            and type(Maximum) == 'number' and Maximum % 1 == 0 and Maximum >= 0 and Maximum <= 128,
            '[LUI:Commands] InvalidQuery')
        Search = string.lower(Search)
        local Snapshot: {Command} = {}
        for _, Entry in Entries do table.insert(Snapshot, Entry) end
        table.sort(Snapshot, function(Left: Command, Right: Command) return Left.Id < Right.Id end)
        local Rows: {Row} = {}
        Listing = true
        for _, Entry in Snapshot do
            local Haystack = string.lower(Entry.Label .. ' ' .. Entry.Id .. ' ' .. (Entry.Category or ''))
            local Match = true
            for Token in string.gmatch(Search, '%S+') do
                if not string.find(Haystack, Token, 1, true) then Match = false; break end
            end
            if Match and #Rows < Maximum and Alive and Entries[Entry.Id] == Entry then
                local Available = Enabled(Entry)
                if Alive and Entries[Entry.Id] == Entry then
                    table.insert(Rows, table.freeze({Id = Entry.Id, Label = Entry.Label,
                        Category = Entry.Category or '', Enabled = Available, Shortcut = Entry.Shortcut}))
                end
            end
        end
        Listing = false
        return table.freeze(Rows)
    end
    function Value.Execute(_Self: any, Id: string): (boolean, string?)
        if not Alive then return false, 'Retired' end
        if Executing or Listing then return false, 'Busy' end
        local Entry = Entries[Id]
        if not Entry then return false, 'Missing' end
        Executing = true
        local Available = Enabled(Entry)
        if not Alive or Entries[Id] ~= Entry then Executing = false; return false, 'Retired' end
        if not Available then Executing = false; return false, 'Disabled' end
        local Ok = pcall(Entry.Execute)
        Executing = false
        return Ok, if Ok then nil else 'Failed'
    end
    function Value.Dispatch(Self: any, Input: KeyboardShortcut): (boolean, string?)
        if not Alive then return false, 'Retired' end
        if Input.Device ~= 'Keyboard' or Input.IsRepeat or Input.TextEditing then return false, 'Ignored' end
        for Id, Entry in Entries do
            if Entry.Shortcut and Same(Entry.Shortcut, Input) then return Self:Execute(Id) end
        end
        return false, 'Missing'
    end
    function Value.Dispose(_Self: any)
        if not Alive then return end
        Alive = false; Entries = {}; Identities = {}; Count = 0
    end
    return table.freeze(Value) :: Registry
end

return table.freeze(Commands)
