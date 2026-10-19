local Button=require("../Components/Button")
return function(Parent:Window):Frame
    local Root=Instance.new("Frame",{Parent=Parent,Name="Library",Size=UDim2.new(1,-32,1,-100),Position=UDim2.fromOffset(16,88)})
    Instance.new("UIListLayout",{Parent=Root,Padding=UDim.new(0,12)})
    Instance.new("TextLabel",{Parent=Root,Text="Synthetic project library",Size=UDim2.fromOffset(550,48)})
    local Status=Instance.new("TextLabel",{Parent=Root,Name="Selection",Text="Select a project",Size=UDim2.fromOffset(550,38)})
    for _,Name in {"Aurora tools","Map viewer","Workshop"} do
        Button(Root,"Project_"..Name,Name,function() Status.Text="Selected: "..Name end)
    end
    return Root
end
