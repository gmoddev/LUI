# One-process WebSocket echo (preview.3 candidate)

Copy this complete example from a prepared preview.3 candidate SDK to a sibling
directory. From that project, use the candidate SDK's CLI:

```powershell
lui check
lui run --mode headless --grant network.client,network.server --http-origin http://127.0.0.1:18083 --diagnostics development
```

Port18083 must be free. The app starts a real loopback server, upgrades a connection,
sends a text message, verifies its echo, closes both sides and exits. It prints a
success line only after receiving the matching message. No optional module,
private runtime API, native toolkit, Internet endpoint or Python fixture is needed.
The grants and exact origin are required even though client/server share a process.

This targets the unreleased candidate, not published preview.2. Integration source
qualification and SDK-recipient qualification are recorded separately.
