--!strict
local Json = require("@Json/Json")
local function Reject(Callback: () -> any, Code: string?)
    local Ok, Error = pcall(Callback)
    assert(not Ok)
    if Code then assert(Error == "[LUI:Json] " .. Code) end
    assert(not string.find(tostring(Error), "SENSITIVE_RESPONSE_CANARY", 1, true))
end
assert(Json.Version == "0.1.0")
assert(#Json.RequiredCapabilities == 0 and table.isfrozen(Json.RequiredCapabilities))
assert(Json.Encode(Json.Array()) == "[]" and Json.Encode(Json.Object()) == "{}")
assert(Json.Encode(Json.Decode("[]")) == "[]" and Json.Encode(Json.Decode("{}")) == "{}")
assert(Json.Encode({B = 2, A = 1}) == '{"A":1,"B":2}')
assert(Json.Decode("null") == Json.Null and Json.Encode(Json.Null) == "null")
assert(Json.Decode("false") == false and Json.Decode("true") == true)
assert(Json.Decode(" \n\t\r[-0,1.5,2e2,-3E-2] ")[3] == 200)
local Text = "\0\b\f\n\r\t\"\\/" .. utf8.char(0x20ac, 0x1f642)
assert(Json.Decode(Json.Encode(Text)) == Text)
assert(Json.Decode('"\\u20ac\\ud83d\\ude42"') == utf8.char(0x20ac, 0x1f642))
assert(Json.Decode('"\\u0000"') == "\0")
local Sample = Json.Decode('{"Name":"OK","Items":[1,true,null],"Empty":{},"Array":[]}')
assert(Sample.Name == "OK" and Sample.Items[3] == Json.Null)
assert(Json.Encode(Sample) == '{"Array":[],"Empty":{},"Items":[1,true,null],"Name":"OK"}')
for _, Invalid in {
    "", " ", "01", "-01", "+1", "1.", ".1", "1e", "1e+", "1e9999", "NaN", "Infinity", "0x10",
    "true false", "nullx", "[1,]", "[,1]", "{,}", '{"A":}', '{"A":1,}', "[", "{", '"',
    '"\\x41"', '"\\u123"', '"\\uZZZZ"', '"\\ud800"', '"\\udc00"', '"\\ud800\\u1234"',
    '"\n"', '"\0"', '{1:2}', '"SENSITIVE_RESPONSE_CANARY\\q"',
} do Reject(function() return Json.Decode(Invalid) end, "InvalidJson") end
Reject(function() return Json.Decode('{"A":1,"\\u0041":2}') end, "DuplicateKey")
for _, Invalid in {
    string.char(0xc0, 0xaf), string.char(0xed, 0xa0, 0x80), string.char(0xf4, 0x90, 0x80, 0x80),
    string.char(0x80), string.char(0xc2), string.char(0xf5, 0x80, 0x80, 0x80),
} do
    Reject(function() return Json.Decode('"' .. Invalid .. '"') end, "InvalidUtf8")
    Reject(function() return Json.Encode(Invalid) end, "InvalidUtf8")
end
for _, Value in { math.huge, -math.huge, 0 / 0, function() end, { [0] = 1 }, { [2] = 1 }, { [1] = 1, A = 2 } } do
    Reject(function() return Json.Encode(Value) end, "InvalidValue")
end
Reject(function() return Json.Encode(nil) end, "InvalidValue")
Reject(function() return Json.Array("SENSITIVE_RESPONSE_CANARY" :: any) end, "InvalidValue")
Reject(function() return Json.Object(7 :: any) end, "InvalidValue")
Reject(function() return Json.Decode(7 :: any) end, "InvalidValue")
local Cycle: any = {}; Cycle.Self = Cycle
Reject(function() return Json.Encode(Cycle) end, "Cycle")
Reject(function() return Json.Encode(setmetatable({}, { __index = function() error("UNEXPECTED") end })) end, "InvalidValue")
Reject(function() return Json.Encode(Json.Array({ A = 1 } :: any)) end, "InvalidValue")
Reject(function() return Json.Encode(Json.Object({ [1] = 1 } :: any)) end, "InvalidValue")
assert(Json.Encode({1, 2, 3}) == "[1,2,3]")
local Shared = {A=1}; assert(Json.Encode({Shared, Shared}) == '[{"A":1},{"A":1}]')
assert(Json.Decode('"123"', {MaxBytes=5}) == "123")
assert(Json.Encode("123", {MaxBytes=5}) == '"123"')
Reject(function() return Json.Decode('"123"', {MaxBytes=4}) end, "LimitExceeded")
Reject(function() return Json.Encode("123", {MaxBytes=4}) end, "LimitExceeded")
assert(Json.Decode("[1]", {MaxDepth=2})[1] == 1)
assert(Json.Encode({1}, {MaxDepth=2}) == "[1]")
Reject(function() return Json.Decode("[1]", {MaxDepth=1}) end, "LimitExceeded")
Reject(function() return Json.Encode({1}, {MaxDepth=1}) end, "LimitExceeded")
assert(Json.Decode('{"A":1}', {MaxNodes=3}).A == 1)
assert(Json.Encode({A=1}, {MaxNodes=3}) == '{"A":1}')
Reject(function() return Json.Decode('{"A":1}', {MaxNodes=2}) end, "LimitExceeded")
Reject(function() return Json.Encode({A=1}, {MaxNodes=2}) end, "LimitExceeded")
for _, Options in { {MaxBytes=0}, {MaxDepth=65}, {MaxNodes=100001}, {MaxBytes=1.5}, {MaxDepth=0/0}, {Other=1} } do
    Reject(function() return Json.Decode("null", Options :: any) end, "InvalidOptions")
    Reject(function() return Json.Encode(Json.Null, Options :: any) end, "InvalidOptions")
end
local Boundary = string.rep("a", 1048574)
assert(#Json.Encode(Boundary) == 1048576)
assert(#Json.Decode('"' .. Boundary .. '"') == 1048574)
Reject(function() return Json.Encode(Boundary .. "a") end, "LimitExceeded")
Reject(function() return Json.Decode('"' .. Boundary .. 'a"') end, "LimitExceeded")
local Deep = string.rep("[", 63) .. "0" .. string.rep("]", 63)
assert(Json.Decode(Deep))
Reject(function() return Json.Decode("[" .. Deep .. "]") end, "LimitExceeded")
for Index = 1, 100 do
    local Number = (Index - 50) / 7
    assert(Json.Decode(Json.Encode(Number)) == Number)
    local Source = {Number = Number, Word = "Test\n" .. tostring(Index), Items = {true, Json.Null, Index}}
    assert(Json.Encode(Json.Decode(Json.Encode(Source))) == Json.Encode(Source))
end
print("LIBRARY_JSON_PASS")
local Checkpoints = 0
local LongText = string.rep(utf8.char(0x1f642), 1000) .. string.rep(" ", 1000)
local EncodedLong = Json.Encode({Text=LongText,Items={1,2,3}})
local function DecodeInterleaved(Expected: string)
    local Result=Json.Decode(Json.Encode({Text=Expected}),nil,function()
        Checkpoints+=1;coroutine.yield()
    end)
    assert(Result.Text==Expected)
end
local First=coroutine.create(function()DecodeInterleaved(LongText)end)
local Second=coroutine.create(function()DecodeInterleaved(string.rep("other",400))end)
while coroutine.status(First)~="dead" or coroutine.status(Second)~="dead" do
    for _,Thread in {First,Second}do
        if coroutine.status(Thread)~="dead" then local Ok,Problem=coroutine.resume(Thread);assert(Ok,tostring(Problem))end
    end
end
assert(Checkpoints>10)
assert(Json.Decode(EncodedLong,nil,function()Checkpoints+=1 end).Text==LongText)
Reject(function()return Json.Decode(EncodedLong,{MaxBytes=20},function()error("must not run")end)end,"LimitExceeded")
Reject(function()return Json.Decode("null",nil,7 :: any)end,"InvalidOptions")
local Cancelled=pcall(function()Json.Decode(EncodedLong,nil,function()error("cancelled",0)end)end)
assert(not Cancelled)
local BadAfterYield='"'..string.rep("a",1000)..'\\q"'
Reject(function()return Json.Decode(BadAfterYield,nil,function()Checkpoints+=1 end)end,"InvalidJson")
print("LIBRARY_JSON_COOPERATIVE_PASS")
