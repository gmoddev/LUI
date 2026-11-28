# Stdio

Use this optional package for command-line pipelines in the preview.3 headless owner on Windows and Linux. Add `HostServices` to the project capabilities and copy the package's `Host.json` into the project’s `ExtensionDeclarations`. The SDK example already has both. Launch it with `lui run --mode headless --stdio true`. Put application arguments after `--`. An explicit streams launch inherits the process's three standard handles. Startup failures use fixed messages on stderr. After entry, the host signals failure through the exit status. Development prints and the legacy exit summary are suppressed in this mode.

~~~luau
local Stdio = require("@Stdio/Stdio")
Stdio.ReadLine(4096, function(Status, Bytes)
    if Status == "Ok" then
        Stdio.Write(Bytes, function(Result)
            if Result ~= "Ok" then Stdio.SetExitStatus(1) end
        end)
    end
end)
~~~

`Read(MaxBytes, Callback)` returns at most the requested bytes. `ReadLine(MaxBytes, Callback)` includes the line feed when present and preserves carriage returns. A final unterminated line is returned before EOF. If a line fills the limit without a line feed, the callback receives `Limit` and the consumed fragment. A subsequent read continues at the next byte. Both methods preserve buffered read-ahead. Read limits are 1–65,536 bytes.

`Write(Bytes, Callback)` and `WriteError(Bytes, Callback)` accept up to 65,536 bytes including empty strings, NUL and arbitrary binary data. Completion follows the write to the operating system. The callback receives a status and a byte string, empty for writes. Status values are `Ok`, `Eof`, `Limit`, `Canceled` and `IoError`. Errors and cancellations can follow a partial transfer. Do not replay a write automatically.

Each operation returns a numeric request ID. One request may be outstanding per stream. A second operation on that stream raises a catchable error. `Cancel(Id)` requests cancellation and returns whether the request was still present. Completions run on the owner thread and may schedule another request. Runtime shutdown interrupts and joins outstanding workers. If a device driver ignores cancellation for two seconds, the headless process exits. Timeout and interrupt use process exit codes 124 and 130. Framework failures after entry are conveyed by exit status, so a saturated stderr stream cannot stall shutdown.

`Arguments()` returns a fresh array of up to 64 literal application arguments, 4 KiB per argument and 32 KiB total. `IsTerminal(Stream)` accepts `stdin`, `stdout` or `stderr`. `SetExitStatus(Code)` selects the success-path process exit code in 0–125. Runtime and host failures take precedence. The project checker derives the owner method and signal signatures from the sidecar. No extra binary dependency or grant for native extension loading is needed.
