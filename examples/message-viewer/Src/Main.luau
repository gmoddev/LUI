--!strict
local Json = require("@Json/Json")
local Messages = require("@Messages/Messages")
local Window = Instance.new("Window", {Title="Message viewer", Size=UDim2.fromOffset(510,170), Visible=true})
local Input = Instance.new("TextBox", {Parent=Window, Text='{"Job":"index","Items":[1,2,3]}',
    Position=UDim2.fromOffset(12,16), Size=UDim2.new(1,-24,0,36)}) :: TextBox
local Output = Instance.new("TextLabel", {Parent=Window, TextWrapped=true, Position=UDim2.fromOffset(12,64),
    Size=UDim2.new(1,-24,0,70)}) :: TextLabel
local Queue = Messages.Mailbox(1,65536)
local function Update()
    local Ok, Description = pcall(function()
        local Value = Json.Decode(Input.Text,{MaxBytes=8192,MaxDepth=16,MaxNodes=512})
        local Sent = Queue:Send(Value)
        assert(Sent)
        local _, Bytes = Queue:Usage()
        local Copy = Queue:Receive()
        return string.format("%d bytes · %s",Bytes,Json.Encode(Copy))
    end)
    Output.Text = if Ok then Description else "Enter a supported JSON document."
end
Input.TextChanged:Connect(Update)
Update()
Window.CloseRequested:Connect(function() Queue:Close(); Window:Destroy() end)
