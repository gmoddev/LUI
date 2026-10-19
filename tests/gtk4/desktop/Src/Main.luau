local Window=Instance.new("Window",{Name="Qualification",Title="LUI Linux desktop qualification",Size=UDim2.fromOffset(680,470)})
local Root=Instance.new("Frame",{Parent=Window,Position=UDim2.fromOffset(20,20),Size=UDim2.new(1,-40,1,-40)})
Instance.new("UIListLayout",{Parent=Root,Padding=UDim.new(0,10)})
local Theme=app:GetService("ThemeService")
local ThemeLabel=Instance.new("TextLabel",{Parent=Root,Text="Theme: "..Theme.CurrentTheme,Size=UDim2.fromOffset(600,30)})
Theme.ThemeChanged:Connect(function(Value) ThemeLabel.Text="Theme: "..Value end)
local Input=Instance.new("TextBox",{Parent=Root,Name="Input",Text="Synthetic input",AccessibilityLabel="Qualification input",AccessibilityDescription="Synthetic text used for native input checks",Size=UDim2.fromOffset(600,38)})
local Edits=0
local Status=Instance.new("TextLabel",{Parent=Root,Name="Status",Text="Ready",Size=UDim2.fromOffset(600,30)})
Input.TextChanged:Connect(function() Edits+=1; Status.Text="Text edits: "..Edits end)
local function Button(Name:string,Text:string,Callback:()->())
    local Value=Instance.new("TextButton",{Parent=Root,Name=Name,Text=Text,AccessibilityLabel=Text,Size=UDim2.fromOffset(600,38)})
    Value.Activated:Connect(Callback)
    return Value
end
local Clipboard=app:GetService("ClipboardService")
Button("Copy","Copy synthetic clipboard value",function()
    Clipboard:WriteText("LUI_VM_CLIPBOARD_😀")
    Status.Text="Clipboard written"
end)
Button("Read","Read clipboard",function()
    Clipboard:ReadText(function(Value,Error)
        Status.Text=Error and "Clipboard failed" or Value=="LUI_VM_CLIPBOARD_😀" and "Clipboard matched" or "External clipboard received"
    end)
end)
Button("Open","Open file",function()
    Status.Text="Chooser pending"
    app:GetService("DialogService"):OpenFile(function(Path,Error)
        Status.Text=Error and "File chooser failed" or Path and "File selected" or "File chooser dismissed"
    end)
end)
local Check=Instance.new("CheckBox",{Parent=Root,Text="Native check box",AccessibilityLabel="Native check box",Size=UDim2.fromOffset(600,34)})
Check.CheckedChanged:Connect(function() Status.Text=Check.Checked and "Checked" or "Unchecked" end)
Window.Visible=true
