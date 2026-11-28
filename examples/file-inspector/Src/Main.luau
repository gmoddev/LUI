--!strict
local Files = require("@LuiWorkbench/Files")
local Encoding = require("@Encoding/Encoding")
local Picker = Files.New(app:GetService("FileService"))
local Platform = app:GetService("PlatformService")
local Clock = app:GetService("PerformanceService")
local Window = Instance.new("Window", {Title = "File inspector", Size = UDim2.fromOffset(620, 250), Visible = true})
local Button = Instance.new("TextButton", {Parent = Window, Text = "Choose a file", Position = UDim2.fromOffset(16, 16),
    Size = UDim2.fromOffset(144, 36)})
local Output = Instance.new("TextLabel", {Parent = Window, Text = Platform.OperatingSystem .. " · " .. Platform.Architecture,
    TextWrapped = true, Position = UDim2.fromOffset(16, 66), Size = UDim2.new(1, -32, 1, -82)})
local Busy = false
Button.Activated:Connect(function()
    if Busy then return end
    Busy = true
    Picker:OpenFile(function(File, OpenError)
        if not File then Busy = false; Output.Text = OpenError or "Selection canceled."; return end
        local Document = File
        local Start = Clock:GetTime()
        Document:ByteLength(function(Length, SizeError)
            if not Length then Document:Close(); Busy = false; Output.Text = SizeError or "Read failed."; return end
            Document:ReadRange(0, 32, function(Prefix, ReadError)
                Document:Close()
                Busy = false
                if not Prefix then Output.Text = ReadError or "Read failed."; return end
                Output.Text = string.format("%s\n%.0f bytes · %.1f ms\n%s", Document.Name, Length, Clock:GetTime() - Start,
                    Encoding.HexEncode(buffer.tostring(Prefix)))
            end)
        end)
    end)
end)
Window.CloseRequested:Connect(function() Picker:Dispose(); Window:Destroy() end)
