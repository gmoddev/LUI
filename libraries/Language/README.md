# Language

Use the checker packaged in the Windows x64 or Linux x64 SDK as the operator-selected executable for Process. Grant its working directory and declare HostServices. The app gains the Process authority described in that library. Source inspection runs asynchronously in the separate checker process.

~~~luau
local Language = require("@Language/Language")
Language.Analyze("return require('./Colors')", function(Report, Failure)
    if Report then print(Report.Valid, Report.BytecodeBytes) end
end)
~~~

Analyze accepts up to 4096 UTF-8 bytes. It parses with the pinned Luau parser and compiles valid source in memory. The callback receives validity, compiled size, up to 16 diagnostics and 32 literal import sites with one-based UTF-16 ranges (exclusive end). Truncated records omitted or shortened fields. A rebound global require prevents static import classification. Syntax errors arrive in the report. The checker neither executes source nor returns bytecode or resolves paths. Use the existing project checker for type analysis across declared modules. Bad options fail at admission. Cancellation, tool failures and invalid replies yield a fixed failure. The returned Process request can be canceled.
