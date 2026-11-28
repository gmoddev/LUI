# Process

Use Process from a Windows x64 or Linux x64 headless project to start the operator-selected executable. Vendor Process and Json, declare `HostServices`, and include the Process host declaration. The CLI requires both `--tool <executable>` and `--tool-cwd <directory>`. Select a trusted executable, its dependencies and a working directory appropriate for the application.

~~~luau
local Process = require("@Process/Process")
local Request = Process.Run({Arguments={"--version"}, TimeoutMilliseconds=3000}, function(Result)
    if Result.Status == "Exited" then print(Result.ExitCode, Result.Stdout) end
end)
~~~

## Choosing an operation

| Operation | Input and lifetime |
| --- | --- |
| `Run(Options, Complete)` | Starts when the host is idle. Supplies `Input` as a read-only file with EOF after the final byte. Returns a request with `:Cancel()`. |
| `Open(Options, Complete)` | Opens a persistent session. Optional `Input` is queued first. The child can reply between writes. Two sessions may run concurrently. |
| `Pipeline(Stages, Options, Complete)` | Starts two concurrent instances of the selected executable. Producer stdout feeds consumer stdin. The pipeline returns consumer stdout, the combined stderr and the final stage's exit code. Returns a cancelable request. |

`Arguments` accepts up to 32 UTF-8 strings of 2,048 bytes each, including empty arguments. The initial `Input` accepts up to 4,096 arbitrary bytes. `Environment` starts empty and accepts explicit values for uppercase ASCII names granted by `--child-env-keys NAME,OTHER`. The encoded options packet fits within 16 KiB.

`TimeoutMilliseconds` defaults to 5,000 and accepts 1 through 30,000. It covers the entire invocation, including idle time. `MaxOutputBytes` defaults to 65,536 and accepts 0 through 131,072. It bounds combined stdout and stderr per invocation. `OnOutput(Stream, Bytes)` receives chunks on the runtime owner, in order within each stream. Slow consumption may reach the limit of 256 pending output chunks per invocation.

The completion receives `Status`, `Stdout`, `Stderr` and an `ExitCode` for `Exited`, including nonzero exits. Other statuses are `Canceled`, `TimedOut`, `OutputLimit`, `RetirementFailed` and `Failed`. They retain accepted partial output. `Request:Cancel()` reports whether cancellation was accepted before completion.

## Interactive sessions

- `Session:Write(Bytes)` accepts a chunk of up to 4,096 bytes. A true result acknowledges queue admission. The child can still close its input before consuming the chunk. The queue holds at most 64 KiB and 64 pending chunks. On a full queue, retain the unaccepted chunk and retry later from the scheduler.
- `Session:CloseInput()` schedules EOF after accepted writes and closes admission. The session can continue delivering output. `Session:Terminate()` uses the same cooperative EOF protocol. Select a timeout for tools that may continue after EOF.
- `Session:Kill()` requests forced termination. Terminal handles decline further operations. All callbacks execute on the runtime owner.

For a persistent Git object inspector, see the SDK project in `Examples/tool-session`.

## Pipelines

Pass two stage tables, each containing `Arguments` and an optional `Environment`. A stage-specific environment replaces the pipeline environment. Supply initial producer bytes through `Options.Input`. The producer receives EOF after those bytes. The relay drains accepted producer output before closing consumer stdin.

Each stage has the configured timeout and output limit. The relay can retain up to 128 KiB awaiting downstream admission. The configured output limit also applies to combined public output across both stages. Consumer stdout and both stderr streams reach `Options.OnOutput`. Stderr ordering across the independently scheduled stages can vary. The terminal exit code is the consumer's. Applications that need the producer's exact exit code should use separate sessions. See the SDK project in `Examples/tool-pipeline`.

## Authorization and retirement

The child runs with the OS user's filesystem and network access. Selecting an interpreter, a plugin loader or a tool that launches other programs delegates its native authority. The host supplies literal arguments without a shell or executable search. Windows uses the Microsoft C runtime quoting convention. The child receives its three standard handles and an explicit environment block.

Windows assigns every invocation to a kill-on-close Job Object during creation and checks for zero active members during orderly retirement. `RetirementFailed` reports unconfirmed retirement and closes the job. An abrupt host exit closes the host's job handle.

Linux pins the ELF executable and working directory by descriptor. A supervisor creates a new session and gives the tool its own process group. The tool runs with no-new-privileges and closes unrelated descriptors at exec. On tool exit, cancellation or loss of the host's control pipe, the supervisor kills the original process group and reaps its leader. Descendants that leave that group can survive. Deploy tools requiring stronger containment in an OS-managed cgroup or container with an independent retirement owner. Timeout begins termination, and pending kernel I/O can delay final retirement.
