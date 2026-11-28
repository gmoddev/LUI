# Build applications with an extracted LUI SDK

Use Python 3.10 or later to copy the sample modules and selected SDK libraries
into separate projects. Preparation uses the ordinary SDK CLI and public
manifests. After preparation, run and build with the SDK executable.

```text
python Prepare.py --sdk <extracted SDK> --app InventoryDesk --output InventoryDesk
python Prepare.py --sdk <extracted SDK> --app LogBundle --output LogBundle
python Prepare.py --sdk <extracted SDK> --app TicketService --output TicketService
python Prepare.py --sdk <extracted SDK> --app SourceWorkbench --output SourceWorkbench
python Prepare.py --sdk <extracted SDK> --app NativeDigest --output NativeDigest
```

## Inventory Desk

Run `lui run InventoryDesk/lui.json --allow-native true`. Enter an item and
click **Save item**. Use **Search inventory** to filter the bounded results.
Close and reopen the app to check that the item persists. SQLite runs
parameterized statements and stores a snapshot in the project's app-scoped
storage. Build with `lui build InventoryDesk/lui.json --output Dist/InventoryDesk`.
Start the resulting launcher with `--allow-native` on the desktop host.

## Log Bundle

Read newline-delimited input and keep the lines that match a regular
expression. Supply up to 48 KiB of input with lines up to 4 KiB. The program
writes a gzip stream to stdout and input errors to stderr.

```sh
cat service.log | lui run LogBundle/lui.json --mode headless --stdio true --providers Compression,Regex -- 'ERROR|WARN' > important.log.gz
```

Build with `lui build LogBundle/lui.json --mode headless --output Dist/LogBundle`.
Use the standalone launcher with `--stdio --provider Compression --provider
Regex -- 'ERROR|WARN'`. Inspect the exit code before using the output.

## Ticket Service

Create a private storage directory, then start a loopback HTTP service.
Choose an available local port. The service admits at most 64 ticket titles.

```sh
lui run TicketService/lui.json --mode headless --stdio true --grant network.server,Storage --storage-root <directory> -- 18082
curl -X POST http://127.0.0.1:18082/tickets -d '{"Title":"Gather logs"}'
curl http://127.0.0.1:18082/tickets
```

POST returns 202 when a write is accepted. Read the collection to confirm
that an update finished. Invalid input receives a 400 response. Press Enter
in the service console to close the listener. Preserve the same storage
directory across restarts. Build it in headless mode and use the standalone
launcher with the same `--stdio`, separate `--grant` flags and explicit
storage root. Keep the service on loopback.

## Source Workbench

Run `lui run SourceWorkbench/lui.json`. Edit the source and wait for the
line and TODO counts to update. Use the search field to find exact text.
The editor uses one reusable worker to scan the newest saved values as you
type. A small runtime-generated marker identifies the analysis pane.
Build with `lui build SourceWorkbench/lui.json --output Dist/SourceWorkbench`.

## Native Digest

Preparation creates the reusable `DigestBindings` package in an adjacent
catalog. The package carries the Crypto facade, the selected SDK Crypto image,
and its notice. Supply `--other-sdk` during preparation to include both x64
bindings in the same archive. Consumer code imports the package's simple
`Hex` function.

```sh
lui package inspect NativeDigest/lui.json
printf abc | lui run NativeDigest/lui.resolved.json --mode headless --stdio true --providers Crypto
lui build NativeDigest/lui.resolved.json --mode headless --output Dist/NativeDigest
```

The standalone launcher takes `--stdio --provider Crypto`. The authored
capabilities, restore-time consent, and launch-time provider choice are
separate. Offline restoration uses the cached archive contents.
