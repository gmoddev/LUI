# Toml

`Toml.Decode(Document)` reads a TOML 1.0 table into Luau using toml++ 3.4.0. `Toml.Encode(Table)` serializes the supported values to TOML and returns a string. `Toml.Settings(Document, Defaults)` applies the existing Config key, type and default rules to a top-level settings table.

~~~luau
local Toml = require("@Toml/Toml")
local Settings = Toml.Settings('Name = "LUI"', {Name = "app", Copies = 1})
local Saved = Toml.Encode({Project = {Name = Settings.Name}})
assert(Toml.Decode(Saved).Project.Name == "LUI")
~~~

Select `--providers Toml` in a Windows x64 or Linux x64 headless application. Vendor the pinned target image, declaration, MIT license and the Toml, Config and Json modules. The manifest requests NativeExtensions and HostServices. Calls read and return in-memory values. The caller owns returned strings and tables.

Tables, arrays, strings, booleans, exact integers within Luau's safe range and finite floats are supported. Date/time values, infinities, NaN, nulls and larger integers raise a fixed error. The parser accepts up to 32 KiB of TOML, a 16-level document and 2048 nodes. The parser limits dotted key paths to 16 segments and the document to 64 dotted separators. Quoted periods are ordinary key bytes. Luau represents integers and floats as numbers. Encode accepts 15 levels and 2048 JSON nodes including object keys, limits the intermediate JSON to 32,000 bytes, applies its JSON conversion limits before formatting, and can reject a document whose escaped representation exceeds the budget. Use Json.Array and Json.Object to distinguish empty arrays and tables. Serialization preserves supported values. Formatting and comments can change. Syntax failures and limit failures are catchable with pcall.
