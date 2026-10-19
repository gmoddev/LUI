--!strict
local Scope = require('@LuiUI/Scope')
local State = require('@LuiUI/State')
local VirtualList = require('@LuiUI/VirtualList')

local Window = Instance.new('Window', {Title = 'Collection regression', Visible = true, Size = UDim2.fromOffset(400, 240)})
local Viewport = Instance.new('ScrollingFrame', {Parent = Window, Size = UDim2.fromOffset(300, 180)}) :: ScrollingFrame

task.defer(function()
    local ScopeValue = Scope.New()
    local Cleanups = 0
    ScopeValue:Add(function() Cleanups += 1 end)
    ScopeValue:Dispose(); ScopeValue:Dispose(); assert(Cleanups == 1)
    local FullScope = Scope.New()
    for _ = 1, 128 do FullScope:Add(function() end) end
    local BeforeConnections = app:GetService('PerformanceService'):GetStats().Connections
    assert(not pcall(function() FullScope:Connect(Viewport.CanvasChanged, function() end) end))
    assert(app:GetService('PerformanceService'):GetStats().Connections == BeforeConnections)
    FullScope:Dispose()
    local Store = State.New(0)
    local Delivered = {}
    local Subscription = Store:Subscribe(function(Value: any)
        table.insert(Delivered, Value)
        if Value == 1 then Store:Set(2) end
    end)
    Store:Set(1); assert(Delivered[1] == 1 and Delivered[2] == 2)
    Subscription:Disconnect(); Store:Set(3); assert(#Delivered == 2); Store:Dispose()
    assert(not pcall(function() Store:Set(4) end))

    local Items: {any} = {}
    for Index = 1, 10000 do Items[Index] = {Id = 'row-' .. Index, Height = 60 + (Index % 3) * 20, Text = 'Synthetic item ' .. Index} end
    local FirstButton: TextButton? = nil
    local List = VirtualList.New(Viewport, {
        MaxRows = 48, Overscan = 2,
        GetKey = function(Item: any) return Item.Id end,
        GetHeight = function(Item: any, _Width: number) return Item.Height end,
        Render = function(Parent: Frame, Item: any, Binding: any, Width: number)
            local Label = Binding:Own(Instance.new('TextLabel', {Parent = Parent, Text = Item.Text, Size = UDim2.fromOffset(Width - 50, Item.Height)})) :: TextLabel
            local Button = Binding:Own(Instance.new('TextButton', {Parent = Parent, Text = Item.Id, Position = UDim2.fromOffset(Width - 50, 0), Size = UDim2.fromOffset(50, 30)})) :: TextButton
            if not FirstButton then FirstButton = Button end
            Binding:Connect(Button.Activated, function() Label.Text = 'Activated ' .. Item.Id end)
        end,
    })
    List:SetItems(Items)
    assert(List:GetStats().ModelItems == 10000 and List:GetStats().LiveRows <= 48)
    Viewport.CanvasPosition = Vector2.new(0, 800)
    local Offset = Viewport.AbsoluteCanvasPosition.Y
    local Added = table.clone(Items); table.insert(Added, 1, {Id = 'inserted', Height = 70, Text = 'Inserted'})
    List:SetItems(Added); assert(Viewport.AbsoluteCanvasPosition.Y == Offset + 70)
    List:SetItems(Items); assert(Viewport.AbsoluteCanvasPosition.Y == Offset)
    assert(not pcall(function() List:SetItems({Items[1], Items[1]}) end))
    assert(List:GetStats().ModelItems == 10000)
    assert(not pcall(function() List:SetItems({{Id = 'bad', Height = 0}}) end))
    assert(List:GetStats().ModelItems == 10000)
    for Step = 1, 100 do
        Viewport.CanvasPosition = Vector2.new(0, Step * 7000)
        List:Refresh()
        local Stats = List:GetStats()
        assert(Stats.LiveRows + Stats.PooledRows <= 48)
        assert(app:GetService('PerformanceService'):GetStats().LiveInstances <= 150)
    end
    assert(FirstButton ~= nil)
    -- Readable dead metadata and idempotent destruction survive runtime cleanup.
    local RetiredButton = FirstButton :: TextButton
    assert(RetiredButton.ClassName == 'TextButton')
    RetiredButton:Destroy(); RetiredButton:Destroy()
    Viewport.CanvasPosition = Vector2.new(0, 1e9); List:Refresh()
    local Next = table.clone(Items); table.insert(Next, {Id = 'append', Height = 90, Text = 'Appended'})
    List:SetItems(Next, true)
    assert(Viewport.AbsoluteCanvasPosition.Y == Viewport.AbsoluteCanvasSize.Y - Viewport.AbsoluteSize.Y)
    Viewport.Size = UDim2.fromOffset(250, 200); List:Invalidate()
    assert(List:GetStats().LiveRows <= 48)
    List:Dispose(); List:Dispose()
    assert(app:GetService('PerformanceService'):GetStats().LiveInstances == 2)
    local CleanupCount = 0
    local Failing = VirtualList.New(Viewport, {
        GetKey = function(Item: any) return Item.Id end,
        GetHeight = function(_Item: any, _Width: number) return 70 end,
        Render = function(Parent: Frame, _Item: any, Binding: any, _Width: number)
            Binding:Own(Instance.new('TextLabel', {Parent = Parent}))
            Binding:Add(function() CleanupCount += 1; error('synthetic cleanup failure') end)
        end,
    })
    Viewport.CanvasPosition = Vector2.new(0, 0)
    Failing:SetItems({Items[1], Items[2]})
    assert(not pcall(function() Failing:Dispose() end))
    Failing:Dispose()
    assert(CleanupCount == 2 and app:GetService('PerformanceService'):GetStats().LiveInstances == 2)
    Window.Title = 'Collection regression complete'
end)
