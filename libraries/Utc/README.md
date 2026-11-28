# Utc

Convert whole Unix seconds to and from a portable Gregorian timestamp. Use the host's existing clocks for obtaining the time or measuring a duration. This module does no I/O and requires no capability.

~~~luau
local Utc = require("@Utc/Utc")
local Instant = Utc.Parse("2024-02-29T18:15:09+05:30")
assert(Utc.Format(Instant) == "2024-02-29T12:45:09Z")
~~~

`Parse(Text)` returns a signed integer Unix timestamp. Supply exactly `YYYY-MM-DDTHH:MM:SSZ` or a signed offset `±HH:MM`. The offset is applied before checking the supported UTC range of years 0001 through 9999. Negative Unix seconds represent instants before 1970. Year zero, unknown offset `-00:00`, fractional seconds, leap seconds, invalid dates, lowercase separators and surrounding whitespace fail.

`Format(Seconds)` returns the canonical 20-byte UTC spelling. It accepts finite whole seconds from -62135596800 through 253402300799. The proleptic Gregorian calendar applies throughout. Local daylight-saving rules and timezone databases are outside this format.

Both functions have bounded work, retain no caller data and raise fixed `[LUI:Utc]` errors catchable by `pcall`. Windows and Linux use the same module. Browser and mobile hosts have separate qualification.
