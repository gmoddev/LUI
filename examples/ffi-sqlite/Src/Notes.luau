--!strict
local Ffi = require("@Ffi/Ffi")
local Notes = {}
-- These prototypes follow sqlite3.h. SQLite owns the connection until sqlite3_close.
function Notes.CountInserted(): number
    local Sql = Ffi.System("sqlite")
    local Open = Sql:Bind("sqlite3_open", {"utf8","pointer"}, "i32")
    local Execute = Sql:Bind("sqlite3_exec", {"pointer","utf8","pointer","pointer","pointer"}, "i32")
    local Changes = Sql:Bind("sqlite3_changes", {"pointer"}, "i32")
    local Close = Sql:Bind("sqlite3_close", {"pointer"}, "i32")
    local Slot = Ffi.Allocate(Ffi.Platform().PointerBytes)
    local Status = Open:Call({":memory:", Slot})
    local Db = Slot:Get(0, "pointer")
    local Count = 0
    if Status == 0 then
        Status = Execute:Call({Db, "CREATE TABLE notes(body TEXT); INSERT INTO notes VALUES ('first'),('second');",
            Ffi.Null, Ffi.Null, Ffi.Null})
        if Status == 0 then Count = Changes:Call({Db}) end
    end
    if Db ~= Ffi.Null then
        local Closed = Close:Call({Db})
        Db:Close()
        assert(Closed == 0, "[LUI:FfiSQLite] CloseFailed")
    end
    Slot:Close()
    for _, Fn in {Open, Execute, Changes, Close} do Fn:Close() end
    Sql:Close()
    assert(Status == 0, "[LUI:FfiSQLite] QueryFailed")
    return Count
end
return table.freeze(Notes)
