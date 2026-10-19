local Window = Instance.new("Window", {
    Title = "LUI Controls",
    Size = UDim2.fromOffset(480, 420),
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

Instance.new("UIListLayout", {
    Padding = UDim.new(0, 10),
    Parent = Root,
})

local Status = Instance.new("TextLabel", {
    Text = "Enter a name",
    Size = UDim2.new(1, 0, 0, 30),
    Parent = Root,
})

local Input = Instance.new("TextBox", {
    AccessibilityLabel = "Name",
    AccessibilityDescription = "Enter a name to personalize the greeting",
    Size = UDim2.new(1, 0, 0, 40),
    Parent = Root,
})

local Check = Instance.new("CheckBox", {
    Text = "Enable notifications",
    Size = UDim2.new(1, 0, 0, 40),
    Parent = Root,
})

local Slider = Instance.new("Slider", {
    AccessibilityLabel = "Level",
    Minimum = 0,
    Maximum = 100,
    Value = 25,
    Size = UDim2.new(1, 0, 0, 40),
    Parent = Root,
})

local Progress = Instance.new("ProgressBar", {
    AccessibilityLabel = "Level progress",
    Minimum = 0,
    Maximum = 100,
    Value = 25,
    Size = UDim2.new(1, 0, 0, 20),
    Parent = Root,
})

local Button = Instance.new("TextButton", {
    Text = "Submit",
    Size = UDim2.new(1, 0, 0, 40),
    Parent = Root,
})

Input.TextChanged:Connect(function()
    Status.Text = Input.Text == "" and "Enter a name" or "Hello, " .. Input.Text
end)

Slider.ValueChanged:Connect(function()
    Progress.Value = Slider.Value
end)

Button.Activated:Connect(function()
    print("[LUI:Controls] Submit: " .. Input.Text)
end)

Button.MouseEnter:Connect(function()
    Button.Text = "Submit (hover)"
end)

Button.MouseLeave:Connect(function()
    Button.Text = "Submit"
end)

Button.InputBegan:Connect(function(Event)
    if Event.Device == "Keyboard" then
        print("[LUI:Input] Key down " .. Event.Key)
    else
        print("[LUI:Input] Press " .. Event.Device .. " #" .. Event.PointerId)
    end
end)

Button.InputEnded:Connect(function(Event)
    if Event.Device == "Keyboard" then
        print("[LUI:Input] Key up " .. Event.Key)
    else
        print("[LUI:Input] Release " .. Event.Device .. " #" .. Event.PointerId)
    end
end)

Window.Visible = true
