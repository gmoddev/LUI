--!strict
local SQLite = require("@SQLite/SQLite")
local Stdio = require("@Stdio/Stdio")
local Args = Stdio.Arguments()
Stdio.SetExitStatus(2)
if #Args == 1 and Args[1] == "check" then
    SQLite.CheckLive(function(Healthy, Failure)
        if Healthy and not Failure then
            Stdio.Write("ok\n", function(Status) if Status == "Ok" then Stdio.SetExitStatus(0) end end)
        end
    end)
    return
end
local Add = #Args == 2 and Args[1] == "add" and #Args[2] <= 128 and not string.find(Args[2], "[\t\r\n]")
if not Add and not (#Args == 1 and Args[1] == "list") then return end
Stdio.SetExitStatus(1)
local Db = SQLite.OpenLive()
local Groups: {SQLite.Batch} = {{Sql = "CREATE TABLE IF NOT EXISTS note (id INTEGER PRIMARY KEY, body TEXT NOT NULL)", Bindings = {{}}}}
if Add then
    local Insert: SQLite.Batch = {Sql = "INSERT INTO note(body) VALUES (?)", Bindings = {{Args[2]}}}
    table.insert(Groups, Insert)
end
local Select: SQLite.Batch = {Sql = "SELECT id, body FROM note ORDER BY id DESC LIMIT 64", Bindings = {{}}}
table.insert(Groups, Select)
Db:Transaction(Groups, function(Results, Failure)
    Db:Close()
    if not Results or Failure then return end
    local Lines: {string} = {}
    for _, Row in Results[#Results].Rows do
        table.insert(Lines, tostring(Row[1]) .. "\t" .. tostring(Row[2]) .. "\n")
    end
    Stdio.Write(table.concat(Lines), function(Status) if Status == "Ok" then Stdio.SetExitStatus(0) end end)
end)
