--!strict
local Ffi = require("@Ffi/Ffi")
local Stdio = require("@Stdio/Stdio")
local Fixture = require("./Binding").Open()
local Register = Fixture:Bind("LuiFfiRegister", {"pointer"}, "u32")
local Emit = Fixture:Bind("LuiFfiEmit", {"i32"}, "u32")
local Unregister = Fixture:Bind("LuiFfiUnregister", {}, "u32")
local OnThread = Fixture:Bind("LuiFfiNotifyOnThread", {"pointer", "i32"}, "void")
local Total = 0
local Notice = Ffi.Callback({"i32"}, "void", function(Args)
    if Args[1] < 0 then error("negative notification") end
    Total += Args[1]
end)
assert(Register:Call({Notice}) == 1)
assert(Emit:Call({12}) == 1)
OnThread:Call({Notice, 30})
OnThread:Call({Notice, -1})
assert(Total == 0)
local Count, Dropped, Errors = Ffi.Poll()
assert(Count == 3 and Total == 42 and Dropped == 0 and #Errors == 1)
assert(Unregister:Call({}) == 1)
Notice:Close()
for _, Fn in {Register, Emit, Unregister, OnThread} do Fn:Close() end
Fixture:Close()
assert(Ffi.Stats().Live == 0)
Stdio.Write("notifications=3 sum=42 handler-errors=1 live=0\n", function() end)
