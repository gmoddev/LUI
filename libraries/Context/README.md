# Context

A Windows x64 or Linux x64 headless application can declare the Context host sidecar, require HostServices and select `--context true`. `Context.Platform()` returns Windows or Linux. `Context.Elapsed()` returns monotonic seconds since this service was installed. `Context.Get(Key)` returns the explicitly granted value or nil when absent. No operation enumerates the parent environment.

~~~luau
local Context = require("@Context/Context")
local Project = Context.Get("LUI_PROJECT_NAME") or "unnamed"
print(Context.Platform(), Project)
~~~

Use `--env-keys LUI_PROJECT_NAME,OTHER_NAME` to capture up to 16 named values once at startup. Names contain up to 64 uppercase ASCII letters, digits or underscores and begin with a letter or underscore. Values are limited to 4096 bytes each. An oversized grant fails before script entry with a fixed error. The service and its snapshot retire with the runtime. Script code that receives a value can disclose it. Child-process variables have their own independent grant. Existing Stdio.Arguments supplies the application's command line.
