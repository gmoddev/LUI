# Local JSON application

Stage the complete application with the preview.2 development tools.

```text
python scripts/Stage-LibraryExample.py --example json-client --output build/JsonApp
cd build/JsonApp
lui check
lui run
```

The staged application pins both optional libraries with exact module hashes.
It requests network.client and network.server, binds a temporary loopback port,
serves JSON and fetches it asynchronously into a native status window. Its
successful status reads `Local JSON status: Ready`. Closing the window retires
its server and pending client work. Restarting creates a fresh local server.

The example uses existing Windows/Linux HTTP and window contracts. Browser
hosting cannot provide this native loopback server workflow. Native toolkit
availability follows the SDK platform matrix.
