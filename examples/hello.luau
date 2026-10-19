local Window = Instance.new("Window", {
    Title = "LUI Foundation 0",
    Size = UDim2.fromOffset(640, 400),
})

local Button = Instance.new("TextButton", {
    Text = "Click me",
    Size = UDim2.fromOffset(160, 40),
    Parent = Window,
})

Button.Activated:Connect(function()
    Button.Text = "Clicked!"
    print("[LUI:Example] Button activated")
end)

Window.Visible = true
