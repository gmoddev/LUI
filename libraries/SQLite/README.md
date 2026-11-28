# SQLite

`SQLite.Open(Image?)` creates an in-memory connection. An optional buffer supplies an existing snapshot. `Connection:Execute(Sql, Parameters, Callback)` executes one parameterized statement. The owner receives an array with one result per execution, or a fixed failure string. Each result contains positional `Rows` and corresponding `Columns`.

~~~luau
local SQLite = require("@SQLite/SQLite")
local Db = SQLite.Open()
Db:Execute("SELECT ? AS message", {"hello"}, function(Results, Failure)
    assert(Results and not Failure)
    print(Results[1].Rows[1][1])
    Db:Close()
end)
~~~

Bind numbers, text, buffers, `SQLite.Null` and signed 64-bit integers made with `SQLite.Integer(DecimalString)`. Large result integers retain their exact digits. Buffers represent BLOBs. Iterate the returned rows with an ordinary Luau loop. `Prepare(Sql)` returns a reusable statement. Call `Statement:RunMany({Params1, Params2}, Callback)` to compile once on the worker and bind multiple times. `Statement:Close()` releases the statement text. The next run prepares against the connection's current schema.

`Db:Transaction({{Sql = Query, Bindings = {Params1, Params2}}}, Callback, Mode?)` commits all executions together. The optional mode accepts `Deferred`, `Immediate` and `Exclusive`. An error or cancellation retains the last committed image. One operation may be outstanding per Luau connection. Independent connections own independent snapshots. The returned request supports cancellation. `Db:Close()` cancels pending work, suppresses its callback and releases the Luau snapshot.

Use `Db:Snapshot()` while idle to copy the committed SQLite database. The records example persists it with the existing app-scoped StorageService. A snapshot has at most 1 MiB and uses 4 KiB pages. Applications sharing a durable name must coordinate writes. Use live mode for concurrent processes.

The native provider accepts up to 32 statement groups, 64 executions, 32 parameters, 16 result columns and 256 rows per transaction. Command metadata and result data each have a 64 KiB ceiling. SQL text is limited to 8 KiB per statement. The worker enforces a VM instruction budget and checks cancellation. Applications may create ordinary tables and indexes and perform parameterized selects, inserts, updates and deletes. Available SQL functions include count, min, max, avg, sum, total, coalesce, ifnull, length, abs, round, lower, upper, substr and typeof. Policy failures report `CapabilityDenied`; syntax and constraint errors report `InvalidArgument`. Bounds report `LimitExceeded`.

Snapshot mode opens a private in-memory database. Temporary storage stays in memory. The provider admits a restricted set of SQL actions and disables native extension loading at compile time. ATTACH, arbitrary PRAGMAs, virtual tables, triggers, views, SQL transaction control and external journal/WAL activation are outside this profile. A native connection and all its prepared statements close before the worker returns a result.

The headless host checks the target-specific native image when launched with `--providers SQLite`. Declare `HostServices` and `NativeExtensions` and vendor the SQLite declaration, its native image and the Binary module. Persisting snapshots also requires an explicit Storage grant. SQLite 3.54.1 is distributed under its public-domain dedication. [SQLite copyright](https://sqlite.org/copyright.html)

## Live mode

Start the packaged headless application with `--providers SQLite --database-root DIRECTORY`. Create a dedicated directory owned by the running account. Linux requires mode 0700. On Windows restrict the directory ACL to the application account and trusted operators. Use a local disk with operating-system file locking. The native embedding API grants one directory before script entry. The script never receives its path.

`SQLite.OpenLive(WaitMilliseconds?)` returns a connection with the same Execute, Transaction, Prepare and Close methods. It validates the directory grant on first use. The optional wait for the database lock defaults to 2000 ms and accepts integers from 0 to 5000. A contended transaction returns `WouldBlock`. All LUI processes take an exclusive OS lock on `lui.lock` until their native transaction closes. Keep this file for the life of the database. Only LUI clients using the same directory may open it. A process with direct write access to the directory is trusted. Snapshot remains the independent in-memory API, and calling Snapshot on a live connection raises `LiveDatabase`.

The VFS admits one 128 MiB database with 4096-byte pages and its DELETE rollback journal. Temporary data stays in memory. SQL cannot select paths, attach databases, load extensions, or activate WAL. Reads and writes use the existing statement, result, progress and memory limits. Use parameterized keyset pagination when a result would exceed 256 rows. File and integrity failures report `ProviderFailure`. `SQLite.CheckLive(function(Healthy, Failure) ... end, WaitMilliseconds?)` checks an existing database (a missing database reports failure) and schedules SQLite's integrity check with the same lock and work budget. The packaged live-records application demonstrates checking and concurrent appends.

A successful transaction callback acknowledges commit. A canceled request, missing callback, process exit or failure while delivering a result can leave a committed transaction. Use a unique application request ID and check its result before retrying a mutation. Each native connection and its statements close before the worker unlocks the directory. Process-interruption qualification covers rollback-journal recovery. Power-loss durability depends on the OS, filesystem and storage device.
