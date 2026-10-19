Window = Instance.new("Window", {Title = "LUI Native Mapping", Size = UDim2.fromOffset(420, 300)})
Root = Instance.new("Frame", {Size = UDim2.fromScale(1, 1), Parent = Window})
Instance.new("UIPadding", {PaddingTop = UDim.new(0, 12), PaddingLeft = UDim.new(0, 12), Parent = Root})
Instance.new("UIListLayout", {Padding = UDim.new(0, 8), Parent = Root})

Input = Instance.new("TextBox", {
    Text = "initial", AccessibilityLabel = "Qualification input",
    AccessibilityDescription = "Native help text", Size = UDim2.fromOffset(240, 36), Parent = Root,
})
Check = Instance.new("CheckBox", {
    Text = "Agreement", Checked = true, Enabled = false,
    AccessibilityLabel = "Qualification check", Size = UDim2.fromOffset(240, 36), Parent = Root,
})
Slider = Instance.new("Slider", {
    Minimum = 10, Maximum = 20, Value = 15,
    AccessibilityLabel = "Qualification slider", Size = UDim2.fromOffset(240, 36), Parent = Root,
})
Progress = Instance.new("ProgressBar", {
    Minimum = 0, Maximum = 100, Value = 40,
    AccessibilityLabel = "Qualification progress", Size = UDim2.fromOffset(240, 24), Parent = Root,
})
Hidden = Instance.new("Frame", {
    Visible = false, AccessibilityLabel = "Qualification hidden frame",
    Size = UDim2.fromOffset(240, 24), Parent = Root,
})
Window.Visible = true
