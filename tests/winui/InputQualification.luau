-- Interactive native qualification for keyboard pairing and pointer cancellation.
local Window = Instance.new("Window", {Title = "LUI Input Qualification", Size = UDim2.fromOffset(540, 380)})
local Root = Instance.new("Frame", {Size = UDim2.fromScale(1, 1), Parent = Window})
Instance.new("UIPadding", {PaddingTop = UDim.new(0, 16), PaddingLeft = UDim.new(0, 16), Parent = Root})
Instance.new("UIListLayout", {Padding = UDim.new(0, 10), Parent = Root})

local Status = Instance.new("TextLabel", {
    Text = "Ready", Size = UDim2.new(1, 0, 0, 70), Parent = Root,
})
local Input = Instance.new("TextBox", {
    AccessibilityLabel = "Keyboard target",
    AccessibilityDescription = "Type here to verify key and text events",
    Size = UDim2.new(1, 0, 0, 40), Parent = Root,
})
local Button = Instance.new("TextButton", {
    Text = "Press then drag out", Size = UDim2.new(1, 0, 0, 40), Parent = Root,
})
local Disable = Instance.new("TextButton", {
    Text = "Disables on press", Size = UDim2.new(1, 0, 0, 40), Parent = Root,
})
local Reset = Instance.new("TextButton", {
    Text = "Reset", Size = UDim2.new(1, 0, 0, 40), Parent = Root,
})

local Trace = ""
local function Log(Message)
    Trace ..= Message .. " "
    Status.Text = Trace
    print("[LUI:InputQualification] " .. Message)
end

Input.InputBegan:Connect(function(Event)
    if Event.Device == "Keyboard" then Log("K+" .. Event.Key) end
end)
Input.InputChanged:Connect(function(Event)
    if Event.Device == "Keyboard" then Log("K*" .. Event.Key) end
end)
Input.InputEnded:Connect(function(Event)
    if Event.Device == "Keyboard" then Log("K-" .. Event.Key) end
end)
Input.TextChanged:Connect(function() Log("Text:" .. Input.Text) end)
Input.FocusLost:Connect(function() Log("FocusLost") end)

Button.InputBegan:Connect(function(Event)
    if Event.Device ~= "Keyboard" then Log("P+" .. Event.PointerId) end
end)
Button.InputEnded:Connect(function(Event)
    if Event.Device ~= "Keyboard" then
        Log((Event.IsCanceled and "PC" or "P-") .. Event.PointerId)
    end
end)

Disable.InputBegan:Connect(function(Event)
    if Event.Device ~= "Keyboard" then
        Log("D+" .. Event.PointerId)
        Disable.Enabled = false
    end
end)
Disable.InputEnded:Connect(function(Event)
    if Event.Device ~= "Keyboard" then Log(Event.IsCanceled and "DC" or "D-") end
end)
Reset.Activated:Connect(function()
    Trace = ""
    Status.Text = "Ready"
    Disable.Enabled = true
end)
Window.Visible = true
