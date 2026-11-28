--!strict
local Files = require("@LuiWorkbench/Files")
local Stdio = require("@Stdio/Stdio")
local Picker = Files.New(app:GetService("FileService"))

local function Failed()
    Picker:Dispose()
    Stdio.SetExitStatus(1)
end

Picker:OpenFile(function(File, OpenError)
    if not File or OpenError then Failed(); return end
    local Document = File
    Document:ByteLength(function(Size, SizeError)
        if Size == nil or SizeError then Failed(); return end
        local Expected = Size
        local Reader = Files.Reader(Document)
        local Newlines = 0
        local function Next()
            local Position = Reader:Position()
            if Position == Expected then
                Reader:Close()
                Document:Close()
                Picker:Dispose()
                Stdio.Write(string.format("%d bytes, %d line feeds\n", Position, Newlines), function(Status)
                    if Status ~= "Ok" then Stdio.SetExitStatus(1) end
                end)
                return
            end
            Reader:Read(math.min(65536, Expected - Position), function(Chunk, ReadError)
                if not Chunk or ReadError or buffer.len(Chunk) == 0 then Failed(); return end
                local _, Count = string.gsub(buffer.tostring(Chunk), "\n", "")
                Newlines += Count
                Next()
            end)
        end
        Next()
    end)
end)
