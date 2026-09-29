local Window = Instance.new("Window", {
    Title = "LUI Grid",
    Size = UDim2.fromOffset(560, 400),
})

local Root = Instance.new("Frame", {
    Size = UDim2.fromScale(1, 1),
    Parent = Window,
})

Instance.new("UIPadding", {
    PaddingTop = UDim.new(0, 16),
    PaddingBottom = UDim.new(0, 16),
    PaddingLeft = UDim.new(0, 16),
    PaddingRight = UDim.new(0, 16),
    Parent = Root,
})

Instance.new("UIGridLayout", {
    CellSize = UDim2.fromOffset(160, 88),
    CellPadding = UDim2.fromOffset(12, 12),
    Parent = Root,
})

for Index = 1, 6 do
    local Tile = Instance.new("TextButton", {
        Text = "Tile " .. Index,
        LayoutOrder = Index,
        Parent = Root,
    })
    if Index == 1 then
        Instance.new("UISizeConstraint", {
            MinSize = Vector2.new(172, 88),
            Parent = Tile,
        })
    end
    Tile.Activated:Connect(function()
        print("[LUI:Grid] Activated tile " .. Index)
    end)
end

Window.Visible = true
