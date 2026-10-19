-- Interactive WinUI qualification: focus Target, enter "disable", click Reset target,
-- focus Target again, enter "hide", click Reset target, then focus Target again.
-- Each disable/hide must produce one A-, each transfer to Other one B+,
-- and each restored Target focus one A+. The event sequence is shown above the controls.
local Window = Instance.new("Window", {Title = "LUI Focus Edge Qualification", Size = UDim2.fromOffset(500, 360)})
local Root = Instance.new("Frame", {Size = UDim2.fromScale(1, 1), Parent = Window})
Instance.new("UIPadding", {PaddingTop = UDim.new(0, 16), PaddingLeft = UDim.new(0, 16), Parent = Root})
Instance.new("UIListLayout", {Padding = UDim.new(0, 10), Parent = Root})

local Status = Instance.new("TextLabel", {Text = "Ready", Size = UDim2.new(1, 0, 0, 35), Parent = Root})
local First = Instance.new("TextBox", {
    AccessibilityLabel = "Target input", Size = UDim2.new(1, 0, 0, 40), Parent = Root,
})
local Second = Instance.new("TextBox", {
    AccessibilityLabel = "Other input", Size = UDim2.new(1, 0, 0, 40), Parent = Root,
})
local Reset = Instance.new("TextButton", {Text = "Reset target", Size = UDim2.new(1, 0, 0, 40), Parent = Root})

local Sequence = ""
First.Focused:Connect(function() Sequence ..= "A+ "; Status.Text = Sequence end)
First.FocusLost:Connect(function() Sequence ..= "A- "; Status.Text = Sequence end)
Second.Focused:Connect(function() Sequence ..= "B+ "; Status.Text = Sequence end)
Second.FocusLost:Connect(function() Sequence ..= "B- "; Status.Text = Sequence end)
First.TextChanged:Connect(function()
    if First.Text == "disable" then First.Enabled = false end
    if First.Text == "hide" then First.Visible = false end
end)
Reset.Activated:Connect(function()
    First.Enabled = true
    First.Visible = true
    First.Text = ""
    Status.Text = Sequence .. "reset"
end)
Window.Visible = true
