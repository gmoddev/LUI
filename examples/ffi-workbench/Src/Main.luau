--!strict
local Ffi = require("@Ffi/Ffi")
local Fixture = require("./Binding").Open()
local Start = Fixture:Bind("LuiFfiStartJob", {"pointer","u32"}, "u32")
local Cancel = Fixture:Bind("LuiFfiCancelJob", {}, "void")
local Join = Fixture:Bind("LuiFfiJoinJob", {}, "u32")
local Window = Instance.new("Window", {Title="Native checksum", Size=UDim2.fromOffset(480, 185), Visible=true})
local Input = Instance.new("TextBox", {Parent=Window, PlaceholderText="Type while the native job runs",
    Position=UDim2.fromOffset(12, 12), Size=UDim2.new(1, -24, 0, 34)}) :: TextBox
local Status = Instance.new("TextLabel", {Parent=Window, Text="Ready", Position=UDim2.fromOffset(12, 54),
    Size=UDim2.new(1, -24, 0, 36)})
local Run = Instance.new("TextButton", {Parent=Window, Text="Run", Position=UDim2.fromOffset(12, 104),
    Size=UDim2.fromOffset(110, 36)})
local Stop = Instance.new("TextButton", {Parent=Window, Text="Cancel", Position=UDim2.fromOffset(136, 104),
    Size=UDim2.fromOffset(110, 36)})
local Running, Closed = false, false
local Notice = Ffi.Callback({"i32"}, "void", function(Values)
    Running = false
    Status.Text = if Values[1] == -1 then "Canceled" else "Checksum " .. tostring(Values[1])
end)
local function Pump()
    if Closed then return end
    local _, _, Errors = Ffi.Poll()
    if #Errors > 0 then Status.Text = Errors[1]; Running = false end
    if Running then task.delay(0.016, Pump) end
end
Run.Activated:Connect(function()
    if Running or Closed then return end
    Running = Start:Call({Notice, 4096}) == 1
    Status.Text = if Running then "Computing checksum" else "Busy"
    if Running then Pump() end
end)
Stop.Activated:Connect(function() if Running then Cancel:Call({}) end end)
Input.TextChanged:Connect(function() Window.Title = "Native checksum " .. Input.Text end)
Window.CloseRequested:Connect(function()
    Closed = true
    if Running then Cancel:Call({}) end
    Join:Call({})
    Join:Close()
    Notice:Close()
    Start:Close()
    Cancel:Close()
    Fixture:Close()
    Window:Destroy()
end)
