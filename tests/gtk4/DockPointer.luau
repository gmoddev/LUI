--!strict
-- Driven by XTest on the outer, private Weston X11 backend. GTK itself uses Wayland.
local Model=require("@LuiWorkbench/DockModel").New()
local DockHost=require("@LuiWorkbench/DockHost")
local Window=Instance.new("Window",{Name="PointerDesk",Title="Pointer desk",Size=UDim2.fromOffset(900,600)})
local Root=Instance.new("Frame",{Parent=Window,Size=UDim2.fromScale(1,1)})
local Host=DockHost.New(Model,{Root=Root,Window=Window})
local Draft=Instance.new("TextBox",{Parent=Root,Text="Preserve this draft",Multiline=true})
for _,Id in {"Sources","Plot","Notes"} do
    local Content=if Id=="Notes" then Draft else Instance.new("TextLabel",{Parent=Root,Text=Id})
    assert(Host:Register(Id,{Title=Id,Content=Content}))
end
assert(Host:Restore({Version=1,Main={Id="Desk",Axis="X",Ratio=.33,First={Id="Left",Tabs={"Sources"},Active="Sources"},
    Second={Id="Right",Tabs={"Plot","Notes"},Active="Plot"}},Floats={}}))
Window.Visible=true
task.delay(1,function()
    Host:RefreshGeometry()
    local Left=assert(Root:FindFirstChild("DockNodeLeft")) :: any
    local Right=assert(Root:FindFirstChild("DockNodeRight"))
    local Tab=assert(Right:FindFirstChild("DockTabNotes")) :: any
    local A,B=Tab.AbsolutePosition,Tab.AbsoluteSize
    local C,D=Left.AbsolutePosition,Left.AbsoluteSize
    print(string.format("[LUI:DockQualification] Gesture %.0f %.0f %.0f %.0f",A.X+B.X/2,A.Y+B.Y/2,C.X+D.X/2,C.Y+D.Y/2+30))
end)
task.delay(7,function()
    local Tree=Model:View().Main :: any
    if Tree and Tree.First and Tree.First.Axis=="Y" and Tree.First.Second.Tabs[1]=="Notes" and Draft.Text=="Preserve this draft" then
        print("[LUI:DockQualification] PointerPassed")
    else print("[LUI:DockQualification] PointerFailed") end
    Host:Dispose()
    Window:RequestClose()
end)
