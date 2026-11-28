# Path

Use paths for display names, build inputs and logical file trees. All six functions operate on UTF-8 text. They perform no I/O and require no grant.

~~~luau
local Path = require("@Path/Path")
local Asset = Path.Resolve("C:/project", "art/../logo.png")
assert(Path.Parent(Asset) == "C:/project")
assert(Path.Name(Asset) == "logo.png" and Path.Extension(Asset) == ".png")
assert(Path.Join("/exports", "today/data.json") == "/exports/today/data.json")
~~~

* `Normalize(Text)` accepts relative paths, POSIX, drive and UNC roots. Both slashes separate components. Output uses forward slashes, an uppercase drive letter and a trailing slash only for roots. Empty input becomes `.`. Repeated interior separators and `.` disappear. `..` pops a component, stays in a relative leading position or stops at an absolute root.
* `Join(Base, Relative)` combines two paths. A rooted second argument fails.
* `Resolve(Base, Text)` requires an explicit absolute base. A rooted input replaces the base. Resolution has no dependency on the process working directory.
* `Parent(Text)` removes the last normalized component. `Name(Text)` returns that component or an empty string for a root or `.`. `Extension(Text)` returns the final dot and suffix. A lone leading dot or a trailing dot produces an empty extension.

Inputs, results and the intermediate joined spelling are limited to 4,096 bytes. Invalid UTF-8, NUL, incomplete UNC roots, Windows device prefixes, single backslash roots and drive-relative paths fail with a fixed `[LUI:Path]` error catchable by `pcall`. Two leading separators denote UNC on every platform. Filesystem access still uses storage or selected-document handles. Lexical normalization cannot establish containment across links.

The frozen module has no native resources. Windows and Linux use the same source. Browser and mobile hosts have separate qualification.
