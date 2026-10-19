--!strict
local Scope = require('./Scope')
local VirtualList = {}

-- Explicit variable heights; renderer callbacks own their children through Scope.
-- A recycled container never carries a previous item's child control/listeners.
function VirtualList.New(Viewport: ScrollingFrame, Options: any)
    local MaxRows = Options.MaxRows or 48
    local Overscan = Options.Overscan or 2
    assert(MaxRows >= 1 and MaxRows <= 48 and MaxRows % 1 == 0, '[LUI:Collection] RowLimit')
    assert(Overscan >= 0 and Overscan <= 8 and Overscan % 1 == 0, '[LUI:Collection] OverscanLimit')
    local Owner = Scope.New()
    local Items: {any} = {}
    local IndexByKey: {[string]: number} = {}
    local Heights: {number} = {}
    local Prefix: {number} = {0}
    local Active: {[string]: any} = {}
    local Pool: {Frame} = {}
    local Alive = true
    local Rendering = false
    local Revision = 0
    local Value: any = {}

    local function Key(Item: any): string
        local Id = Options.GetKey(Item)
        assert(type(Id) == 'string' and #Id > 0 and #Id <= 128, '[LUI:Collection] InvalidKey')
        return Id
    end
    local function Find(Offset: number): number
        local Low, High = 1, #Items
        while Low <= High do
            local Middle = math.floor((Low + High) / 2)
            if Prefix[Middle + 1] <= Offset then Low = Middle + 1 else High = Middle - 1 end
        end
        return math.min(Low, #Items)
    end
    local function Retire(Row: any)
        local Success = pcall(function() Row.Scope:Dispose() end)
        for _, Child in Row.Frame:GetChildren() do Child:Destroy() end
        Row.Frame.Visible = false
        table.insert(Pool, Row.Frame)
        if not Success then error('[LUI:Collection] RetirementFailed', 0) end
    end
    local function Build(Model: {any})
        local NextHeights: {number} = {}
        local NextPrefix = {0}
        local Width = Viewport.AbsoluteSize.X
        for Index, Item in Model do
            local Height = Options.GetHeight(Item, Width)
            assert(type(Height) == 'number' and Height == Height and Height >= 1 and Height <= 4096, '[LUI:Collection] InvalidHeight')
            NextHeights[Index] = Height
            NextPrefix[Index + 1] = NextPrefix[Index] + Height
        end
        return NextHeights, NextPrefix, Width
    end
    local function Measure()
        local NextHeights, NextPrefix, Width = Build(Items)
        Heights = NextHeights; Prefix = NextPrefix
        Viewport.CanvasSize = UDim2.fromOffset(Width, Prefix[#Items + 1])
    end
    function Value.Refresh(_Self: any)
        if not Alive or Rendering then return end
        Rendering = true
        local Success = pcall(function()
            local Width = Viewport.AbsoluteSize.X
            local Offset = Viewport.AbsoluteCanvasPosition.Y
            local Height = Viewport.AbsoluteSize.Y
            local First = #Items == 0 and 1 or math.max(1, Find(Offset) - Overscan)
            local Last = #Items == 0 and 0 or math.min(#Items, Find(Offset + Height) + Overscan)
            assert(Last - First + 1 <= MaxRows, '[LUI:Collection] ViewportRowLimit')
            local Wanted: {[string]: number} = {}
            for Index = First, Last do Wanted[Key(Items[Index])] = Index end
            local Retired = {}
            for Id in Active do if not Wanted[Id] then table.insert(Retired, Id) end end
            table.sort(Retired)
            for _, Id in Retired do local Row = Active[Id]; Active[Id] = nil; Retire(Row) end
            for Index = First, Last do
                local Item = Items[Index]; local Id = Key(Item)
                local Row = Active[Id]
                if Row and Row.Item == Item and Row.Revision ~= Revision and Options.Update then
                    Options.Update(Row.Frame, Item, Width)
                    Row.Revision = Revision
                elseif Row and (Row.Item ~= Item or Row.Revision ~= Revision) then
                    Active[Id] = nil; Retire(Row); Row = nil
                end
                if not Row then
                    local Container = table.remove(Pool)
                    if not Container then Container = Instance.new('Frame', {Parent = Viewport}); Owner:Own(Container) end
                    local FrameValue = Container :: Frame
                    local Binding = Scope.New()
                    Row = {Frame = FrameValue, Scope = Binding, Item = Item, Revision = Revision}
                    Active[Id] = Row
                    FrameValue.Size = UDim2.fromOffset(Width, Heights[Index])
                    Options.Render(FrameValue, Item, Binding, Width)
                end
                Row.Frame.Position = UDim2.fromOffset(0, Prefix[Index])
                Row.Frame.Size = UDim2.fromOffset(Width, Heights[Index])
                Row.Frame.Visible = true
            end
        end)
        Rendering = false
        if not Success then
            local Retired = Active; Active = {}
            for _, Row in Retired do pcall(function() Retire(Row) end) end
            error('[LUI:Collection] RenderFailed', 0)
        end
    end
    function Value.SetItems(Self: any, Next: {any}, FollowEnd: boolean?)
        assert(Alive and not Rendering and #Next <= 20000, '[LUI:Collection] ModelLimit')
        local NextIndex: {[string]: number} = {}
        for Index, Item in Next do local Id = Key(Item); assert(not NextIndex[Id], '[LUI:Collection] DuplicateKey'); NextIndex[Id] = Index end
        local Offset = Viewport.AbsoluteCanvasPosition.Y
        local OldIndex = #Items > 0 and Find(Offset) or 0
        local Anchor = OldIndex > 0 and Key(Items[OldIndex]) or nil
        local Within = OldIndex > 0 and Offset - Prefix[OldIndex] or 0
        local AtEnd = Offset >= math.max(0, Viewport.AbsoluteCanvasSize.Y - Viewport.AbsoluteSize.Y) - 1
        local NextHeights, NextPrefix, Width = Build(Next)
        Items = table.clone(Next); IndexByKey = NextIndex; Heights = NextHeights; Prefix = NextPrefix
        Viewport.CanvasSize = UDim2.fromOffset(Width, Prefix[#Items + 1])
        if FollowEnd and AtEnd then Viewport.CanvasPosition = Vector2.new(0, math.max(0, Prefix[#Items + 1] - Viewport.AbsoluteSize.Y))
        elseif Anchor and NextIndex[Anchor] then Viewport.CanvasPosition = Vector2.new(0, Prefix[NextIndex[Anchor]] + Within)
        else Viewport.CanvasPosition = Vector2.new(0, Offset) end
        Self:Refresh()
    end
    function Value.Invalidate(Self: any)
        if not Alive then return end
        assert(not Rendering, '[LUI:Collection] ReentrantUpdate')
        local Offset = Viewport.AbsoluteCanvasPosition.Y
        local Index = #Items > 0 and Find(Offset) or 0
        local Anchor = Index > 0 and Key(Items[Index]) or nil
        local Within = Index > 0 and Offset - Prefix[Index] or 0
        Measure()
        Revision += 1
        if Anchor then Viewport.CanvasPosition = Vector2.new(0, Prefix[IndexByKey[Anchor]] + Within) end
        Self:Refresh()
    end
    function Value.GetStats(_Self: any)
        local Count = 0; for _ in Active do Count += 1 end
        return table.freeze({ModelItems = #Items, LiveRows = Count, PooledRows = #Pool, MaxRows = MaxRows})
    end
    function Value.GetAnchor(_Self: any): any
        if #Items == 0 then return nil end
        local Offset = Viewport.AbsoluteCanvasPosition.Y
        local Index = Find(Offset)
        return table.freeze({Key = Key(Items[Index]), Offset = Offset - Prefix[Index]})
    end
    function Value.Dispose(_Self: any)
        if not Alive then return end
        Alive = false
        local Failed = false
        for _, Row in Active do
            if not pcall(function() Row.Scope:Dispose() end) then Failed = true end
        end
        Active = {}; Pool = {}; Items = {}; Heights = {}; Prefix = {0}; IndexByKey = {}
        if not pcall(function() Owner:Dispose() end) then Failed = true end
        if Failed then error('[LUI:Collection] DisposalFailed', 0) end
    end
    local LastWidth = Viewport.AbsoluteSize.X
    Owner:Connect(Viewport.CanvasChanged, function()
        local Width = Viewport.AbsoluteSize.X
        if Width ~= LastWidth then LastWidth = Width; Value:Invalidate() else Value:Refresh() end
    end)
    Owner:Connect(Viewport.Destroying, function() Value:Dispose() end)
    return Value
end

return VirtualList
