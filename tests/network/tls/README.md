# Public synthetic TLS fixtures

These files contain intentionally public test credentials. Never deploy their keys or trust their CA in production. Tests load everything into runtime memory; they do not install certificates or modify OS stores.

- `Root.pem`: synthetic CA, valid 2025-01-01 through 2036-01-01.
- `Valid.p12`: server certificate/key, DNS `localhost`, IP `127.0.0.1` / `::1`, valid through 2035-01-01.
- `WrongName.p12`: same issuer with only `wrong.example.test` as its identity.
- `Expired.p12`: server certificate that expired on 2026-01-01.

PKCS#12 passwords are `test-only`. Containers use PBES2 AES-256/SHA-256. `Generate-Fixtures.ps1` recreates fixtures using PowerShell 7.6 / .NET certificate APIs; generated keys are random, so regeneration intentionally changes fixture bytes. Generate only when replacing fixtures, after checking checkout edit claims. No generation tool is required to run the committed tests.
