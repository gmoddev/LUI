--!strict
local DockModel=require("@LuiWorkbench/DockModel")
local DockHost=require("@LuiWorkbench/DockHost")
local Window=Instance.new("Window",{Name="Notebook",Title="Field notebook",Size=UDim2.fromOffset(880,560)})
local Root=Instance.new("Frame",{Parent=Window,Position=UDim2.fromOffset(0,36),Size=UDim2.new(1,0,1,-36)})
local Host=DockHost.New(DockModel.New(),{Root=Root,Window=Window})
local Text=Instance.new("TextBox",{Parent=Root,Name="Draft",Multiline=true,Text="Observation\n"})
local Index=Instance.new("TextLabel",{Parent=Root,Name="Index",Text="Field notes\nObservation"})
assert(Host:Register("Draft",{Title="Observation",Content=Text,Focus=Text,MinWidth=220}))
assert(Host:Register("Index",{Title="Notebook",Content=Index,MinWidth=120}))
local Default={Version=1,Floats={},Main={Id="Desk",Axis="X",Ratio=.25,
    First={Id="Outline",Tabs={"Index"},Active="Index"},Second={Id="Editors",Tabs={"Draft"},Active="Draft"}}}
assert(Host:Restore(Default))
local function Button(Name:string,X:number,Fn:()->())
    Instance.new("TextButton",{Parent=Window,Text=Name,Position=UDim2.fromOffset(X,0),Size=UDim2.fromOffset(136,34)}).Activated:Connect(Fn)
end
Button("Float draft",0,function() Host:Float("Draft") end)
Button("Show notebook",140,function() Host:Show("Index") end)
Button("Restore layout",280,function() Host:Restore(Default) end)
Window.Closed:Connect(function() Host:Dispose(); Window:Destroy() end)
Window.Visible=true
