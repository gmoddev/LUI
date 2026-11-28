--!strict
local Workers = require("@Workers/Workers")
local Window = Instance.new("Window", {Title = "Source Workbench", Size = UDim2.fromOffset(820, 500)})
local Root = Instance.new("Frame", {Parent = Window, Size = UDim2.fromScale(1, 1)})
local Editor = Instance.new("CodeEditor", {Parent = Root, Name = "Source", AccessibilityLabel = "Source code",
    Text = "-- TODO: give this function a name\nreturn 42", MaxLength = 8192,
    Position = UDim2.fromOffset(14, 14), Size = UDim2.new(0.67, -22, 1, -28), TextSize = 15})
local Text = Instance.new("TextLabel", {Parent = Root, Name = "Summary", Text = "Waiting for analysis…",
    Position = UDim2.new(0.67, 8, 0, 56), Size = UDim2.new(0.33, -22, 0, 54), TextWrapped = true, TextXAlignment = "Left"})
local Search = Instance.new("TextBox", {Parent = Root, Name = "Search", PlaceholderText = "Find text in source",
    Position = UDim2.new(0.67, 8, 0, 132), Size = UDim2.new(0.33, -22, 0, 38), MaxLength = 128})
local Hit = Instance.new("TextLabel", {Parent = Root, Name = "SearchResult", Text = "",
    Position = UDim2.new(0.67, 8, 0, 182), Size = UDim2.new(0.33, -22, 0, 60),
    TextWrapped = true, TextXAlignment = "Left"})
local Mark = Instance.new("ImageLabel", {Parent = Root, Position = UDim2.new(0.67, 8, 0, 14),
    Size = UDim2.fromOffset(28, 28), AccessibilityLabel = "Source analysis"})
if app:GetService("PlatformService"):Supports("RuntimeImages") then
    local Pixels = buffer.create(64)
    for Offset = 0, 60, 4 do buffer.writeu32(Pixels, Offset, 0xff946030) end
    local Resource = app:GetService("AssetService"):FromPixels(Pixels, 4, 4, 16, "BGRA8Premultiplied")
    if Resource then Mark.ImageResource = Resource; Resource:Close() end
end
local Pool = Workers.Pool("./Scan", 1)
local Busy = false
local Generation = 0
local Alive = true
local function SearchSource()
    local Position = if Search.Text == "" then nil else string.find(Editor.Text, Search.Text, 1, true)
    Hit.Text = if Search.Text == "" then "" elseif Position then "Found at byte " .. tostring(Position) else "No match"
end
local function Analyze()
    if Busy or not Alive then return end
    local Current = Generation
    local Job = Pool:Send(Editor.Text, function(Result, Failure)
        Busy = false
        if not Alive then return end
        if Generation ~= Current then Analyze(); return end
        if Failure then Text.Text = "Analysis unavailable."; return end
        Text.Text = string.format("Lines %d · TODO %d", Result.Lines, Result.Todos)
    end)
    Busy = Job ~= nil
    if not Job then Text.Text = "Analysis unavailable." end
end
local function Refresh()
    Generation += 1
    Analyze()
    SearchSource()
end
Search.TextChanged:Connect(SearchSource)
Editor.TextChanged:Connect(Refresh)
Window.Destroying:Connect(function()
    Alive = false
    Pool:Close()
end)
Window.Visible = true
Refresh()
