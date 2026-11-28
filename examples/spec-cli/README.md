# Application tests and timing

Run `lui run lui.json --mode headless --stdio true -- --list` to list the declared cases. Use `-- --filter seeded --repeat 5` to run matching cases five times. The filter matches a literal, case-sensitive substring. Repetition accepts decimal integers from 1 through 100.

The JSON report includes matching names, run count, success and elapsed milliseconds. The workloads use independent Random generators. An empty list is valid. A run with no matching case or invalid arguments exits with status 2. Failed cases exit with status 1.
