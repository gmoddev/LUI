--!strict
local Notes = require("./Notes")
local Stdio = require("@Stdio/Stdio")
local Inserted = Notes.CountInserted()
assert(Inserted == 2)
Stdio.Write("inserted=" .. tostring(Inserted) .. "\n", function() end)
