--!strict
local Files = require("@LuiWorkbench/Files")
local Stdio = require("@Stdio/Stdio")
local Args = Stdio.Arguments()
Stdio.SetExitStatus(2)
if #Args ~= 2 then return end
local Picker = Files.New(app:GetService("FileService"))
Picker:OpenFolder(function(Folder)
    if not Folder then return end
    local Root = Folder
    Root:Open(Args[1],function(Input)
        if not Input then Picker:Dispose(); return end
        local Source = Input
        Root:OpenOutput(Args[2],function(Target)
            if not Target then Picker:Dispose(); return end
            Files.Copy(Source,Target,function(Failure)
                Picker:Dispose()
                if not Failure then Stdio.SetExitStatus(0) end
            end)
        end)
    end)
end)
