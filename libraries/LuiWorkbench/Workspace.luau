--!strict
-- Bounded presentation state. IDs never resolve files or native objects.
local Workspace = {}
export type Snapshot = {
    read Version: number, read Left: {string}, read Right: {string},
    read ActiveLeft: string?, read ActiveRight: string?, read Ratio: number,
}
export type Model = {
    Add: (Self: Model, Id: string, Side: string?) -> (),
    Remove: (Self: Model, Id: string) -> boolean,
    Dock: (Self: Model, Id: string, Side: string, Index: number?) -> (),
    Activate: (Self: Model, Id: string) -> (),
    Snapshot: (Self: Model) -> Snapshot,
    Restore: (Self: Model, Input: any) -> (boolean, string?),
    Encode: (Self: Model) -> string,
    Decode: (Self: Model, Bytes: string) -> (boolean, string?),
    SetRatio: (Self: Model, Ratio: number) -> (),
    Layout: (Self: Model, Width: number) -> (number, number, boolean),
    Dispose: (Self: Model) -> (),
}
local function Finite(Value: any): boolean
    return type(Value) == 'number' and Value == Value and math.abs(Value) < math.huge
end
local function Id(Value: any): boolean
    return type(Value) == 'string' and #Value > 0 and #Value <= 128
        and utf8.len(Value) ~= nil and not string.find(Value, '\0', 1, true)
end
local function Side(Value: string): boolean return Value == 'Left' or Value == 'Right' end

function Workspace.New(): Model
    local Groups: {[string]: {string}} = {Left = {}, Right = {}}
    local Active: {[string]: string?} = {}
    local Known: {[string]: string} = {}
    local Ratio, Count = 0.5, 0
    local Alive = true
    local Value: any = {}
    local function Live() assert(Alive, '[LUI:Workspace] Retired') end
    local function Reconcile()
        for _, Name in {'Left', 'Right'} do
            local Current = Active[Name]
            if not Current or not table.find(Groups[Name], Current) then Active[Name] = Groups[Name][1] end
        end
    end
    function Value.Add(_Self: any, Key: string, Name: string?)
        Live()
        local Zone = Name or 'Left'
        assert(Id(Key) and Side(Zone) and not Known[Key] and Count < 32, '[LUI:Workspace] InvalidOrLimit')
        Known[Key] = Zone; table.insert(Groups[Zone], Key); Count += 1; Reconcile()
    end
    function Value.Remove(_Self: any, Key: string): boolean
        Live()
        local Zone = Known[Key]
        if not Zone then return false end
        local Index = table.find(Groups[Zone], Key)
        if Index then table.remove(Groups[Zone], Index) end
        Known[Key] = nil; Count -= 1; Reconcile(); return true
    end
    function Value.Dock(_Self: any, Key: string, Zone: string, Index: number?)
        Live()
        local Previous = Known[Key]
        assert(Previous and Side(Zone), '[LUI:Workspace] UnknownIdentityOrSide')
        local Maximum = #Groups[Zone] + (if Previous == Zone then 0 else 1)
        local At = Index or Maximum
        assert(Finite(At) and At % 1 == 0 and At >= 1 and At <= Maximum, '[LUI:Workspace] InvalidIndex')
        local Before = table.find(Groups[Previous], Key)
        if Before then table.remove(Groups[Previous], Before) end
        table.insert(Groups[Zone], At, Key); Known[Key] = Zone; Active[Zone] = Key; Reconcile()
    end
    function Value.Activate(_Self: any, Key: string)
        Live()
        assert(Known[Key], '[LUI:Workspace] UnknownIdentity')
        Active[Known[Key]] = Key
    end
    function Value.SetRatio(_Self: any, Next: number)
        Live()
        assert(Finite(Next) and Next >= 0.1 and Next <= 0.9, '[LUI:Workspace] InvalidRatio')
        Ratio = Next
    end
    function Value.Snapshot(_Self: any): Snapshot
        Live()
        return table.freeze({Version = 1, Left = table.freeze(table.clone(Groups.Left)), Right = table.freeze(table.clone(Groups.Right)),
            ActiveLeft = Active.Left, ActiveRight = Active.Right, Ratio = Ratio})
    end
    function Value.Restore(_Self: any, Input: any): (boolean, string?)
        Live()
        if type(Input) ~= 'table' or getmetatable(Input) ~= nil then return false, 'InvalidLayout' end
        local Allowed = {Version = true, Left = true, Right = true, ActiveLeft = true, ActiveRight = true, Ratio = true}
        for Key in Input do if not Allowed[Key] then return false, 'InvalidLayout' end end
        if Input.Version ~= 1 or not Finite(Input.Ratio) or Input.Ratio < 0.1 or Input.Ratio > 0.9 then return false, 'InvalidLayout' end
        local NextGroups: {[string]: {string}} = {Left = {}, Right = {}}
        local NextActive: {[string]: string?} = {}
        local Seen: {[string]: string} = {}
        local Total = 0
        for _, Zone in {'Left', 'Right'} do
            local Items = Input[Zone]
            if type(Items) ~= 'table' or getmetatable(Items) ~= nil then return false, 'InvalidLayout' end
            local Length = #Items
            if Length > 32 then return false, 'InvalidLayout' end
            local Entries = 0
            for Key, Item in Items do
                Entries += 1
                if not Finite(Key) or Key % 1 ~= 0 or Key < 1 or Key > Length
                    or not Id(Item) or not Known[Item] or Seen[Item] then return false, 'InvalidLayout' end
                Seen[Item] = Zone
            end
            if Entries ~= Length then return false, 'InvalidLayout' end
            for _, Item in Items do table.insert(NextGroups[Zone], Item) end
            Total += Length
            local Selected = Input['Active' .. Zone]
            if Length == 0 then
                if Selected ~= nil then return false, 'InvalidLayout' end
            elseif type(Selected) ~= 'string' or Seen[Selected] ~= Zone then return false, 'InvalidLayout' end
            NextActive[Zone] = Selected
        end
        if Total ~= Count then return false, 'InvalidLayout' end
        Groups = NextGroups; Active = NextActive; Known = Seen; Ratio = Input.Ratio
        return true, nil
    end
    function Value.Layout(_Self: any, Width: number): (number, number, boolean)
        Live()
        assert(Finite(Width) and Width >= 0, '[LUI:Workspace] InvalidWidth')
        if #Groups.Left == 0 then return 0, Width, false end
        if #Groups.Right == 0 then return Width, 0, false end
        if Width < 488 then return Width, 0, false end
        local Left = math.clamp((Width - 8) * Ratio, 240, Width - 248)
        return Left, Width - 8 - Left, true
    end
    function Value.Encode(Self: any): string
        local Current = Self:Snapshot()
        local Fields = {'LUIW1', tostring(Current.Ratio), Current.ActiveLeft or '', Current.ActiveRight or '', tostring(#Current.Left)}
        for _, Key in Current.Left do table.insert(Fields, Key) end
        table.insert(Fields, tostring(#Current.Right))
        for _, Key in Current.Right do table.insert(Fields, Key) end
        local Parts = {}
        for _, Field in Fields do table.insert(Parts, tostring(#Field) .. ':' .. Field) end
        return table.concat(Parts)
    end
    function Value.Decode(Self: any, Bytes: string): (boolean, string?)
        Live()
        if type(Bytes) ~= 'string' or #Bytes > 8192 then return false, 'InvalidLayout' end
        local Offset = 1
        local function Field(): string?
            local Finish = string.find(Bytes, ':', Offset, true)
            if not Finish or Finish - Offset > 3 then return nil end
            local Digits = string.sub(Bytes, Offset, Finish - 1)
            if not string.match(Digits, '^%d+$') then return nil end
            local Length = tonumber(Digits)
            if not Length or Length > 128 or Finish + Length > #Bytes then return nil end
            local Result = string.sub(Bytes, Finish + 1, Finish + Length)
            Offset = Finish + Length + 1
            return Result
        end
        if Field() ~= 'LUIW1' then return false, 'InvalidLayout' end
        local NextRatio = tonumber(Field() or '')
        local LeftActive, RightActive = Field(), Field()
        if not LeftActive or not RightActive then return false, 'InvalidLayout' end
        local Next: any = {Version = 1, Ratio = NextRatio, Left = {}, Right = {},
            ActiveLeft = if LeftActive == '' then nil else LeftActive,
            ActiveRight = if RightActive == '' then nil else RightActive}
        for _, Zone in {'Left', 'Right'} do
            local Length = tonumber(Field() or '')
            if not Length or Length % 1 ~= 0 or Length < 0 or Length > 32 then return false, 'InvalidLayout' end
            for _ = 1, Length do
                local Key = Field()
                if not Key then return false, 'InvalidLayout' end
                table.insert(Next[Zone], Key)
            end
        end
        if Offset ~= #Bytes + 1 then return false, 'InvalidLayout' end
        return Self:Restore(Next)
    end
    function Value.Dispose(_Self: any)
        if not Alive then return end
        Alive = false; Groups = {Left = {}, Right = {}}; Active = {}; Known = {}; Count = 0
    end
    return table.freeze(Value) :: Model
end
return table.freeze(Workspace)
