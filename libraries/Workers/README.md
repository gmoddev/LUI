# Workers

Declare Workers, Messages and Json in a captured Windows x64 or Linux x64 desktop or headless project. Workers use independently owned Luau states on native threads. Each starts with pure data libraries and no inherited OS, UI, process, scheduler or native-service capabilities.

~~~luau
-- Src/Compute.luau
local Workers = require("@Workers/Workers")
return Workers.Handler(function(Value) return Value * Value end)
~~~

~~~luau
-- Src/Main.luau
local Workers = require("@Workers/Workers")
Workers.Start("./Compute", {}, function(Worker, Failure)
    if not Worker then print(Failure); return end
    Worker:Send(12, function(Square, Error)
        Worker:Close()
        if not Error then print(Square) end
    end)
end)
~~~

Start resolves a declared app module relative to the main entry, using the immutable captured project. Its callback receives a ready actor or a fixed failure. Each actor processes requests in order and retains its own module state. Send copies a value through Messages, and its asynchronous callback receives a reply or failure. The immediate NativeRequest supports Cancel. Closing an actor discards its VM and pending work. A failed dispatch retires that actor. An owner callback runs on the application's existing scheduler.

MemoryBytes selects 1–16 MiB (default 8 MiB). MaxInterrupts selects 1–100 million VM safepoints (default one million). Parent limits further narrow both. TimeoutMilliseconds selects 1–30,000 ms (default 5,000) from admission, including time in queue. The runtime checks cancellation and time on ordinary VM safepoints. Native library operations and scheduling can extend wall time. Up to four actors per project and eight per process may exist. Each actor admits eight running or queued requests and 256 KiB of input. Each message or reply is at most 64 KiB. The existing native service domain adds its own request, memory and session limits.

Invalid wrapper arguments raise a fixed error. Native failures include InvalidArgument, ProviderFailure, Closed, Canceled, LimitExceeded, WouldBlock and DeadlineExceeded. Close the actor after the last completion. A CPU thread shares the process address space. OS-level containment of untrusted native code requires a different host profile.

Reuse an actor for multiple requests to amortize VM and module startup. Batch
small calculations into one message and return compact results. Benchmark the
whole operation at the available CPU count, including owner delivery. A worker
can keep an interactive app responsive even when its aggregate compute time
exceeds the time to run the same jobs on the owner. Size batches to retain the
required cancellation and result latency.

`Workers.Pool("./Compute", 2)` keeps ordinary actors warm behind one FIFO.
`Pool:Send(Value, Complete)` returns a Job or `WouldBlock`/`Closed`.
The pool admits up to 16 queued jobs or 256 KiB of queued input, plus one
in-flight request per actor. `Job:Cancel()` settles the local job once.
`Pool:Close()` closes its actors and settles outstanding jobs. A native job
failure can retire an actor and reduce available pool capacity.

Use `Workers.PoolTrusted("./Compute", 2)` to reuse trusted FFI actors behind
the same bounded queue. Experimental headless FFI projects need both host
flags `--trusted-ffi` and `--trusted-ffi-workers` for this pool or
`Workers.StartTrusted`. Initialize reusable bindings lazily inside the
handler. Each actor owns its session and receives copied application data.
A blocking call can finish after cancellation, while the actor's session
and worker slot remain charged. Native code in these actors shares the host
process and its OS privileges.
