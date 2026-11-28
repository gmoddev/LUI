--!strict
local Language = require("@Language/Language")
local Json = require("@Json/Json")
local Stdio = require("@Stdio/Stdio")
local Parts: {string} = {}
local Count = 0
Stdio.SetExitStatus(2)
local function Read()
    Stdio.Read(4096,function(Status,Text)
        if Status=="Ok" and Text then
            Count+=#Text
            if Count<=4096 then table.insert(Parts,Text); Read() end
        elseif Status=="Eof" then
            Language.Analyze(table.concat(Parts),function(Report,Failure)
                if not Report then return end
                Stdio.Write(Json.Encode(Report).."\n",function(Written)
                    if Written=="Ok" then Stdio.SetExitStatus(if Report.Valid then 0 else 1) end
                end)
            end)
        end
    end)
end
Read()
