local Counter = require("./Counter")
assert(Counter == require("./Counter"))
local Window = Instance.new("Window", {Name = "MobileWindow", Title = "LUI Android controls", Size = UDim2.fromOffset(360, 640)})
local function Make(Class, Name, X, Y, W, H, Properties)
    local Values = Properties or {}
    Values.Name = Name
    Values.Position = UDim2.fromOffset(X, Y)
    Values.Size = UDim2.fromOffset(W, H)
    Values.Parent = Values.Parent or Window
    return Instance.new(Class, Values)
end
local Button = Make("TextButton", "CounterButton", 24, 16, 180, 48, {Text = "Count 0"})
Button.Activated:Connect(function() Button.Text = "Count " .. Counter.Next() end)
local Status = Make("TextLabel", "TaskStatus", 24, 72, 280, 30, {Text = "Tasks pending"})
local Order = {}
task.defer(function()
    table.insert(Order, "A")
    task.defer(function() table.insert(Order, "C"); Status.Text = table.concat(Order) end)
end)
task.delay(0, function() table.insert(Order, "B") end)
local Checked = Make("CheckBox", "Toggle", 24, 108, 220, 48, {Text = "Enable option"})
Checked.CheckedChanged:Connect(function() Status.Text = Checked.Checked and "Checked" or "Unchecked" end)
local Activations = 0
Checked.Activated:Connect(function()
    Activations += 1
    Checked.AccessibilityLabel = "Toggle " .. Activations
end)
local Slider = Make("Slider", "Range", 24, 164, 260, 40, {Minimum = 10, Maximum = 30, Value = 20})
local Progress = Make("ProgressBar", "Progress", 24, 212, 260, 24, {Minimum = 10, Maximum = 30, Value = 20})
Slider.ValueChanged:Connect(function() Progress.Value = Slider.Value end)
local Input = Make("TextBox", "Editor", 24, 248, 270, 48, {PlaceholderText = "Synthetic text"})
Input.TextChanged:Connect(function() Status.Text = Input.Text end)
local Reset = Make("TextButton", "ResetInputButton", 212, 16, 88, 48, {Text = "Reset"})
Reset.Activated:Connect(function() Input.Text = "Reset" end)
local Image = Make("ImageLabel", "Pixel", 308, 16, 32, 32, {Source = "Pixel.png"})
Image.LoadChanged:Connect(function() Image.AccessibilityLabel = "Image " .. Image.LoadState end)
local Scroller = Make("ScrollingFrame", "Scroller", 24, 308, 270, 88, {CanvasSize = UDim2.fromOffset(270, 240)})
Make("TextLabel", "ScrollTop", 8, 8, 240, 32, {Text = "Scroll top", Parent = Scroller})
Make("TextLabel", "ScrollBottom", 8, 168, 240, 32, {Text = "Scroll bottom", Parent = Scroller})
local Inner = Make("ScrollingFrame", "InnerScroller", 180, 8, 80, 64, {Parent = Scroller, CanvasSize = UDim2.fromOffset(80, 180), AccessibilityLabel = "Inner scroll group", AccessibilityDescription = "Synthetic nested scroll"})
local InnerTop = Make("TextLabel", "InnerTop", 0, 0, 80, 48, {Text = "Inner top", Parent = Inner})
local Clip = Make("Frame", "ClipFrame", 24, 412, 110, 60, {ClipsDescendants = true, BackgroundColor3 = Color3.fromRGB(20, 30, 60)})
local Clipped = Make("TextButton", "ClippedButton", 80, 12, 96, 48, {Text = "Clipped", Parent = Clip})
Clipped.Activated:Connect(function() Status.Text = "Clipped activated" end)
local Overflow = Make("Frame", "OverflowFrame", 160, 412, 30, 60)
local OverflowButton = Make("TextButton", "OverflowButton", 40, 12, 100, 48, {Text = "Overflow", Parent = Overflow})
OverflowButton.Activated:Connect(function() Status.Text = "Overflow activated" end)
local Back = Make("TextButton", "BackButton", 24, 492, 160, 48, {Text = "Back", ZIndex = 1})
local Front = Make("TextButton", "FrontButton", 80, 504, 160, 48, {Text = "Front", ZIndex = 2})
Front.Activated:Connect(function() Status.Text = "Front activated" end)
Back.Activated:Connect(function() Status.Text = "Back activated" end)
local Resize = Make("TextButton", "ResizeButton", 24, 552, 160, 48, {Text = "Request size"})
Resize.Activated:Connect(function() Window.Size = UDim2.fromOffset(100, 100); Status.Text = "Size requested" end)
local InputCounts = Make("TextLabel", "InputCounts", 24, 610, 320, 30, {Text = "B0 E0 C0 K0"})
local Began, Ended, Canceled, Keys = 0, 0, 0, 0
local KeyEnded, KeyRepeated = 0, 0
local KeyStatus = Make("TextLabel", "KeyStatus", 24, 778, 320, 30, {Text = "Keys 0 0 0"})
local function KeyCounts() KeyStatus.Text = "Keys " .. Keys .. " " .. KeyRepeated .. " " .. KeyEnded end
local function Counts() InputCounts.Text = "B" .. Began .. " E" .. Ended .. " C" .. Canceled .. " K" .. Keys end
local function Track(Target)
    Target.InputBegan:Connect(function(Event)
        if Event.Device == "Keyboard" then Keys += 1; KeyCounts() else Began += 1 end
        Counts()
    end)
    Target.InputEnded:Connect(function(Event)
        if Event.Device == "Keyboard" then KeyEnded += 1; KeyCounts() else
            Ended += 1
            if Event.IsCanceled then Canceled += 1 end
        end
        Counts()
    end)
    Target.InputChanged:Connect(function(Event)
        if Event.Device == "Keyboard" then KeyRepeated += 1; KeyCounts() end
    end)
end
Track(Button)
Track(Scroller:GetChildren()[1])
Track(InnerTop)
local Selection = Make("TextLabel", "SelectionStatus", 24, 646, 320, 30, {Text = "Selection 0 0"})
Input.SelectionChanged:Connect(function() Selection.Text = "Selection " .. Input.SelectionStart .. " " .. Input.SelectionLength end)
local Secret = Make("TextBox", "Password", 24, 686, 270, 48, {Secret = true, PlaceholderText = "Synthetic password", MaxLength = 32})
local Composition = Make("TextLabel", "CompositionStatus", 24, 742, 320, 30, {Text = "Compose 0 0 0"})
local Started, Changed, Finished = 0, 0, 0
local function CompositionCounts() Composition.Text = "Compose " .. Started .. " " .. Changed .. " " .. Finished end
Input.CompositionStarted:Connect(function() Started += 1; CompositionCounts() end)
Input.CompositionChanged:Connect(function() Changed += 1; CompositionCounts() end)
Input.CompositionEnded:Connect(function() Finished += 1; CompositionCounts() end)
local Hidden = Make("Frame", "HiddenGroup", 0, 0, 360, 900, {Visible = false})
local ChangeTarget = Make("TextButton", "ChangeTarget", 24, 820, 160, 48, {Text = "Change target"})
local MoveTarget = Make("TextButton", "MoveTarget", 200, 820, 140, 48, {Text = "Move target"})
local Moved = false
MoveTarget.Activated:Connect(function()
    Moved = not Moved
    Button.Position = UDim2.fromOffset(24, Moved and 72 or 16)
end)
local Stage = 0
ChangeTarget.Activated:Connect(function()
    if Stage == 0 then Button.Enabled = false
    elseif Stage == 1 then Button.Enabled = true
    elseif Stage == 2 then Button.Parent = Hidden
    elseif Stage == 3 then Button.Parent = Window
    else Button:Destroy() end
    Stage += 1
end)
Window.Visible = true
require("./Offline").Mount(Window)
