# Records

This command-line notebook uses SQLite and app-private storage. From the same project directory run `lui run lui.json --mode headless --stdio true --providers SQLite --grant Storage -- add "first note"`, then run the same command with `list` in place of `add "first note"`. Each successful invocation prints up to 64 recent records. Notes may contain up to 128 bytes. Tabs and line terminators are rejected so each output record occupies one TSV line. A project moved to a different physical directory has a different storage identity.

The application binds note text as a SQL parameter, executes schema creation and insertion inside one transaction, then atomically replaces its scoped snapshot. Select an existing private data directory with `--storage-root` for testing. Run one writer at a time. Independent processes can read the same earlier image and overwrite one another's later changes.
