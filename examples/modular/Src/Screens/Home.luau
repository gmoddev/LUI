local State=require("../State")
local Button=require("../Components/Button")
return function(Parent:Window):Frame
    local Root=Instance.new("Frame",{Parent=Parent,Name="Home",Size=UDim2.new(1,-32,1,-100),Position=UDim2.fromOffset(16,88)})
    Instance.new("UIListLayout",{Parent=Root,Padding=UDim.new(0,12)})
    Instance.new("TextLabel",{Parent=Root,Text="LUI modular workspace",Size=UDim2.fromOffset(550,48)})
    local Status=Instance.new("TextLabel",{Parent=Root,Name="CounterStatus",Text="Actions: "..State.Count,Size=UDim2.fromOffset(550,38)})
    Button(Root,"Increment","Add action",function() State.Count+=1; Status.Text="Actions: "..State.Count end)
    Instance.new("TextBox",{Parent=Root,Name="PrivateInput",Sensitive=true,Text="SYNTHETIC_PRIVATE_CANARY",AccessibilityLabel="SYNTHETIC_ACCESS_CANARY",Size=UDim2.fromOffset(550,42)})
    return Root
end
