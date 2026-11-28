--!strict
local DockModel=require("@LuiWorkbench/DockModel")
local DockHost=require("@LuiWorkbench/DockHost")
local Json=require("@Json/Json")
local Window=Instance.new("Window",{Name="AnalysisWindow",Title="Signal analysis",Size=UDim2.fromOffset(960,620)})
local Root=Instance.new("Frame",{Name="AnalysisDesk",Parent=Window,Position=UDim2.fromOffset(0,38),Size=UDim2.new(1,0,1,-38)})
local Record=Instance.new("TextBox",{Name="LayoutRecord",Parent=Window,Position=UDim2.fromOffset(360,0),
    Size=UDim2.new(1,-360,0,36),AccessibilityLabel="Saved layout JSON"})
local Presentation=require("@LuiWorkbench/Presentation")
local Plot=Instance.new("Frame",{Name="SignalPlot",Parent=Root,BackgroundColor3=Color3.fromRGB(24,36,48)})
local Width,Height=512,192
local Data=buffer.create(Width*Height*4)
local Channel=Presentation.New()
local Ticket: Presentation.Ticket?=nil
local Surface: FrameSurface?=nil
local Native=app:GetService("PlatformService"):Supports("FrameSurface")
if Native then
    Surface=Instance.new("FrameSurface",{Name="SpectrumSurface",Parent=Plot,Size=UDim2.fromScale(1,1),ScaleMode="Stretch"})
end
for I=1,64 do
    local Level=.08+.52*math.abs(math.sin(I*.16)*math.cos(I*.24))
    if Native then
        for X=(I-1)*8,I*8-1 do
            for Y=0,Height-1 do
                local Bar=X%8~=7 and Y>=Height*(1-Level)
                buffer.writeu32(Data,(Y*Width+X)*4,if Bar then 0xFF58B2B8 else 0xFF182430)
            end
        end
    else
        Instance.new("Frame",{Parent=Plot,Name="Band"..I,Position=UDim2.fromScale((I-1)/64,1-Level),
            Size=UDim2.new(1/64,-1,Level,0),BackgroundColor3=Color3.fromRGB(88,178,184)})
    end
end
local Metrics=require("@LuiWorkbench/WindowMetrics").New(Window)
local Host=DockHost.New(DockModel.New(),{Window=Window,Root=Root,WindowMetrics=Metrics,
    OnWindow=if Metrics then Metrics.OnWindow else nil,
    OnAttachment=function(Id,_,Visible)
        local Target=Surface
        if Id~="Spectrum" or not Target then return end
        if Ticket then Channel:Detach(Ticket); Ticket=nil end
        if Visible then
            local Bound=Channel:Attach(Target,Width,Height)
            Ticket=Bound
            local Ok,Reason=Channel:Submit(Bound,1,Data,Width*4,"BGRA8Premultiplied")
            if not Ok then Record.AccessibilityDescription=tostring(Reason) end
        end
    end})
Window.Destroying:Connect(function() Channel:Destroy() end)
local Notes=Instance.new("TextBox",{Parent=Root,Name="AnalysisNotes",Multiline=true,Text="Acquisition notes"})
local Channels=Instance.new("TextLabel",{Parent=Root,Name="Channels",Text="Input channels\nLeft\nRight"})
local Readings=Instance.new("TextLabel",{Parent=Root,Name="Readings",Text="Sample rate 48 kHz\nWindow 2048 samples"})
local History=Instance.new("TextLabel",{Parent=Root,Name="History",Text="Acquisition ready"})
assert(Host:Register("Spectrum",{Title="Spectrum",Content=Plot,MinWidth=160,MinHeight=72}))
assert(Host:Register("Notes",{Title="Notes",Content=Notes,Focus=Notes}))
assert(Host:Register("Channels",{Title="Channels",Content=Channels}))
assert(Host:Register("Readings",{Title="Readings",Content=Readings}))
assert(Host:Register("History",{Title="History",Content=History}))
local Default={Version=1,Main={Id="Layout",Axis="X",Ratio=.23,First={Id="Inputs",Tabs={"Channels"},Active="Channels"},
    Second={Id="Analysis",Axis="Y",Ratio=.7,First={Id="Views",Tabs={"Spectrum","Notes"},Active="Spectrum"},
        Second={Id="Data",Tabs={"Readings","History"},Active="Readings"}}},Floats={}}
local function Action(Title:string,X:number,Fn:()->())
    Instance.new("TextButton",{Parent=Window,Name=Title,Text=Title,Position=UDim2.fromOffset(X,0),Size=UDim2.fromOffset(118,36)}).Activated:Connect(Fn)
end
Action("Save layout",0,function() Record.Text=Json.Encode(Host:Snapshot()) end)
Action("Load layout",120,function()
    local Ok,Data=pcall(function() return Json.Decode(Record.Text) end)
    if Ok and Host:Restore(Data) then Record.AccessibilityDescription="Layout restored"
    else Record.AccessibilityDescription="Invalid layout" end
end)
Action("Reset layout",240,function() Host:Restore(Default) end)
assert(Host:Restore(Default))
Window.Closed:Connect(function() Host:Dispose(); Window:Destroy() end)
Window.Visible=true
