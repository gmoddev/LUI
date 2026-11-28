--!strict
local SQLite = require("@SQLite/SQLite")
local Stdio = require("@Stdio/Stdio")
local Storage = app:GetService("StorageService")
local Args = Stdio.Arguments()
if not ((#Args == 2 and Args[1] == "add" and #Args[2] <= 128 and not string.find(Args[2], "[\t\r\n]")) or (#Args == 1 and Args[1] == "list")) then
    Stdio.SetExitStatus(2)
    return
end
Stdio.SetExitStatus(1)
Storage:Open(buffer.fromstring("records"), nil, function(_, Opened, OpenFailure)
    if not Opened or OpenFailure then return end
    local Handle = Opened
    Storage:Read(buffer.create(0), Handle, function(Image, _, ReadFailure)
        if ReadFailure then Handle:Close(); return end
        local Db = SQLite.Open(Image)
        local Groups: {SQLite.Batch} = {{Sql = "CREATE TABLE IF NOT EXISTS note (id INTEGER PRIMARY KEY, body TEXT NOT NULL)", Bindings = {{}}}}
        if Args[1] == "add" then
            local Insert: SQLite.Batch = {Sql = "INSERT INTO note(body) VALUES (?)", Bindings = {{Args[2]}}}
            table.insert(Groups, Insert)
        end
        local Select: SQLite.Batch = {Sql = "SELECT id, body FROM note ORDER BY id DESC LIMIT 64", Bindings = {{}}}
        table.insert(Groups, Select)
        Db:Transaction(Groups, function(Results, Failure)
            if not Results or Failure then Handle:Close(); Db:Close(); return end
            local Lines: {string} = {}
            for _, Row in Results[#Results].Rows do
                table.insert(Lines, tostring(Row[1]) .. "\t" .. tostring(Row[2]) .. "\n")
            end
            local function Finish()
                Handle:Close()
                Db:Close()
                Stdio.Write(table.concat(Lines), function(Status) if Status == "Ok" then Stdio.SetExitStatus(0) end end)
            end
            if Args[1] == "add" then
                Storage:Write(Db:Snapshot(), Handle, function(_, _, WriteFailure)
                    if WriteFailure then Handle:Close(); Db:Close(); return end
                    Finish()
                end)
            else Finish() end
        end)
    end)
end)
