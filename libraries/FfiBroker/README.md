# Isolate a native call in a selected child process

FfiBroker wraps the existing Process service. Install the broker from the experimental SDK's Tools directory as the operator-selected executable. The parent project requests HostServices and declares Process. It needs no in-process native extension. The child receives a fixed-signature request on standard input, then exits. The OS account, inherited OS privileges and allowed child environment determine what that executable can do.

```luau
local Broker = require("@FfiBroker/FfiBroker")
local Pending = Broker.Call({Library={Kind="System", Name="math"}, Symbol="sqrt",
    Params={"f64"}, Result="f64", Args={1764}}, 5000, function(Reply, Error)
    if Reply then print(Reply.Value) else print(Error) end
end)
```

Pass `--tool <absolute broker> --tool-cwd <directory>` to the headless host, with the usual stdio grant where needed. The one-shot transport copies scalars, exact 64-bit words and UTF-8 strings. It rejects raw pointers, callbacks and aggregate declarations. One JSON request is limited to 4 KiB. Launch another request to obtain a fresh child. An operating-system exit, timeout, malformed output or native declaration error is reported separately from the native return value. Process cancellation terminates a supervised child, including an uninterruptible *application-level* native call once the OS delivers termination. The Windows job and Linux process-group supervisor retain their existing limits. A deliberately detached Linux descendant can escape process-group termination. The subprocess supplies crash isolation. OS confinement requires a separate operator policy.

`CallBatch` takes the same library, symbol and signature fields as `Call`, with `Rows={{Arg1}, {Arg2}}` in place of `Args`. It binds once and executes up to 32 argument rows in order in one child. A successful callback receives the replies in that order. The entire request shares the 4 KiB input limit, the 64 KiB output limit and one timeout. If a later row fails, earlier native side effects may already have occurred. No partial reply or automatic retry is issued. A subsequent request starts a fresh child.
