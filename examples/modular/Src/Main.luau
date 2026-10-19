local State=require("./State")
local Button=require("./Components/Button")
local Home=require("./Screens/Home")
local Library=require("./Screens/Library")
local Window=Instance.new("Window",{Name="ModularWorkspace",Title="LUI module workspace",Size=UDim2.fromOffset(760,560)})
local Navigation=Instance.new("Frame",{Parent=Window,Name="Navigation",Size=UDim2.new(1,-32,0,60),Position=UDim2.fromOffset(16,16)})
Instance.new("UIListLayout",{Parent=Navigation,FillDirection="Horizontal",Padding=UDim.new(0,12)})
local Current:Frame?=nil
local function Show(Screen:string)
    if Current then Current:Destroy() end
    State.Screen=Screen
    Current=if Screen=="Home" then Home(Window) else Library(Window)
end
Button(Navigation,"ShowHome","Home",function() Show("Home") end)
Button(Navigation,"ShowLibrary","Library",function() Show("Library") end)
Show("Home")
Window.Visible=true
