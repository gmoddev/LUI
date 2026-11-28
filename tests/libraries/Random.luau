--!strict
local Random = require("@Random/Random")
local A, B=Random.New(1),Random.New(1)
for _,Expected in {48271,182605794,1291394886,1914720637} do
    assert(A:Integer(1,2147483646)==Expected)
    assert(B:Number()==(Expected-1)/2147483646)
end
assert(not pcall(Random.New,0))
assert(not pcall(function() A:Integer(4,3) end))
for _, Seed in {0, -1, 2147483647, 1.5, math.huge, 0/0} do
    assert(not pcall(Random.New, Seed))
end
local Sample = Random.New(2147483646)
local Control = Random.New(2147483646)
for _, Bounds in {{-2147483648, 0}, {-2147483647, 0}, {0, 2147483647}, {1, 1.5}, {0, math.huge}} do
    assert(not pcall(function() Sample:Integer(Bounds[1], Bounds[2]) end))
end
assert(Sample:Number() == Control:Number())
assert(Sample:Integer(2147483647, 2147483647) == 2147483647)
assert(Sample:Integer(-2147483647, -2147483647) == -2147483647)
-- The first draw from the maximum seed is outside this range's acceptance limit.
local Rejection = Random.New(2147483646)
assert(Rejection:Integer(0, 1073741823) == 856088760)
return true
