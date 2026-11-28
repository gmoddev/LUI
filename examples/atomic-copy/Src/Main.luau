--!strict
local Files = require("@LuiWorkbench/Files")
local Stdio = require("@Stdio/Stdio")
local Picker = Files.New(app:GetService("FileService"))
Stdio.SetExitStatus(1)
Picker:SaveFile("output.bin", function(File, Failure)
    if not File or Failure then Picker:Dispose(); return end
    local Document = File
    Document:BeginWriter(function(Stream, OpenError)
        if not Stream or OpenError then Picker:Dispose(); return end
        local Writer = Stream
        local function Next()
            Stdio.Read(65536,function(Status, Bytes)
                if Status=="Eof" then
                    Writer:Commit(function(Reason)
                        Picker:Dispose()
                        if not Reason then Stdio.SetExitStatus(0) end
                    end)
                elseif Status=="Ok" then
                    Writer:Write(buffer.fromstring(Bytes),function(Reason)
                        if Reason then Picker:Dispose() else Next() end
                    end)
                else Picker:Dispose() end
            end)
        end
        Next()
    end)
end)
