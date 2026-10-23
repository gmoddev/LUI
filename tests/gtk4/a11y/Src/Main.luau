local Window=Instance.new("Window",{Title="LUI accessibility qualification",Size=UDim2.fromOffset(400,200)})
local Frame=Instance.new("Frame",{Parent=Window,Size=UDim2.fromScale(1,1)})
Instance.new("TextBox",{Parent=Frame,Text="Synthetic",AccessibilityLabel="Qualification input",AccessibilityDescription="Synthetic accessibility description",Position=UDim2.fromOffset(10,10),Size=UDim2.fromOffset(350,40)})
local Action=Instance.new("TextButton",{Parent=Frame,Text="Action",AccessibilityLabel="Qualification action",Position=UDim2.fromOffset(10,60),Size=UDim2.fromOffset(350,40)})
Action.Text="Updated action"
Window.Visible=true
Action.Activated:Connect(function() Action.AccessibilityLabel=""; Action.Text="Fallback action" end)
task.delay(15,function() Window:Destroy() end)
