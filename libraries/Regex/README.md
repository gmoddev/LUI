# Regex

`Regex.Compile(Expression)` returns a reusable byte-pattern matcher for the headless Windows x64 and Linux x64 hosts. Select `--providers Regex` and vendor the target's Regex declaration, module, native image and PCRE2 license. The manifest requires HostServices and NativeExtensions.

~~~luau
local Regex = require("@Regex/Regex")
local Pattern = Regex.Compile("([A-Z]+)-([0-9]+)")
local Hit = Pattern:Find("release SDK-17")
if Hit then print(string.sub("release SDK-17", Hit.First, Hit.Last)) end
Pattern:Close()
~~~

`Find(Text, Start?)` returns the first match or nil. `Matches(Text, Start?)` returns a bounded array suitable for iteration. Byte offsets are one-based and inclusive. An empty match has Last equal to First minus one. Captures contains numbered spans for participating groups. Starting at the end plus one allows an empty match. Global matching follows PCRE2 empty-match iteration, including a nonempty alternative at the same position. `Replace(Text, Literal)` substitutes literal bytes for all matches. `Split(Text)` preserves empty fields. Close the compiled pattern when finished. Closing twice is safe. Provider shutdown closes outstanding patterns.

PCRE2 10.49 interprets byte patterns with automatic execution checkpoints, no JIT, a 100,000-checkpoint total per search, 50,000 match steps, depth 128 and a 256 KiB match-heap limit. UTF/UCP modes, callout syntax and pattern-start verbs are disabled. The compiler conservatively rejects the literal byte sequences `(?C` and `(*` anywhere in a pattern, including within character classes. Limits are 32 live patterns, 512 input pattern bytes, 32 KiB compiled code, 8 capture groups, 32 KiB subject, 256 results and 64 KiB replacement output. Malformed patterns, invalid positions and exhaustion raise a fixed provider or library error. Use pcall for untrusted expressions. The package includes PCRE2's BSD-3-Clause license with its redistribution exception.
