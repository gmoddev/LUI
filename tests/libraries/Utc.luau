--!strict
local Utc = require("@Utc/Utc")
local Vectors: {[string]: number} = {
    ["1970-01-01T00:00:00Z"] = 0, ["1969-12-31T23:59:59Z"] = -1,
    ["0001-01-01T00:00:00Z"] = -62135596800, ["9999-12-31T23:59:59Z"] = 253402300799,
    ["2000-02-29T00:00:00Z"] = 951782400, ["2100-03-01T00:00:00Z"] = 4107542400,
}
for Text, Expected in Vectors do assert(Utc.Parse(Text) == Expected and Utc.Format(Expected) == Text) end
assert(Utc.Parse("1970-01-01T05:30:00+05:30") == 0)
assert(Utc.Parse("1969-12-31T16:00:00-08:00") == 0)
for _, Bad in {"1900-02-29T00:00:00Z","2001-04-31T00:00:00Z","2000-01-01T24:00:00Z",
    "2000-01-01T00:00:60Z","2000-01-01T00:00:00-00:00","2000-01-01T00:00:00+24:00",
    "0001-01-01T00:00:00+00:01","9999-12-31T23:59:59-00:01","0000-12-31T23:59:59Z","now"} do
    assert(not pcall(Utc.Parse, Bad))
end
for _, Bad in {0/0, math.huge, -math.huge, 1.5, -62135596801, 253402300800} do assert(not pcall(Utc.Format, Bad)) end
for Year = 1, 9999, 19 do
    local Date = string.format("%04d-12-31T23:59:59Z", Year)
    assert(Utc.Format(Utc.Parse(Date)) == Date)
end
assert(table.isfrozen(Utc))
