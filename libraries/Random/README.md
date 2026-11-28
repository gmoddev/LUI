# Random

`local Stream = Random.New(42)` creates an independent deterministic generator using the Park–Miller recurrence with multiplier 48271. `Stream:Number()` returns a value in [0,1). `Stream:Integer(1,6)` returns an inclusive integer using rejection sampling. Separate generators with the same seed reproduce the same values when called in the same order.

Seeds are integers 1 through 2147483646. Integer endpoints must be within ±2147483647 and span at most 2147483646 distinct values. Bad inputs raise fixed errors. The Lua collector owns the generator. This pure Luau library needs no grant. Use Crypto.RandomBytes for secrets.
