# 0031 — Portable TLS and HTTPS

Status: accepted, 2026-10-04.

## Provider and trust

Use a private OpenSSL provider through pinned Asio's composed TLS I/O. This selects the proposal's portable-provider option; no custom cryptography or TLS record parser is introduced. Windows builds OpenSSL 3.5.9 from a SHA-256-verified upstream release into the CMake dependency cache, linking static C libraries without machine installation. Linux uses its distribution-maintained OpenSSL 3 package. The provider can be replaced behind the private settings/stream boundary; native SSL objects never enter Luau or the host ABI.

TLS 1.2 is the minimum and TLS 1.3 is allowed. Compression, renegotiation, tickets, and session caching are disabled. Peer certificate lists are capped at 128 KiB and chain depth at eight intermediate certificates. Providers select secure ciphers using their default security policy. No client certificate is selected automatically; mTLS is deferred. HTTP/2 ALPN is not advertised.

Clients always require a validated chain, validity dates, server purpose, and the URL's original DNS or IP identity. DNS names supply SNI; numeric endpoints use IP SAN validation. Neither a redirected dial endpoint nor a user-supplied HTTP Host header changes identity. There is no Luau validation bypass, name override, or TLS downgrade fallback. Handshake failure sends no HTTP application bytes. Invalid trust, identity, expiry, and other certificate failures return a stable `CertificateRejected` code.

Default roots are imported into an in-memory OpenSSL store from Windows' current ROOT store, or loaded from OpenSSL's default system trust paths on Linux. Windows verification also checks the current-user and machine Disallowed stores for chain certificates. Stores are opened read-only. This slice checks chain, name, purpose, and dates; live revocation, AIA downloading, pinning, and broader platform-specific enterprise chain policy remain future work. Trust roots are snapshotted per runtime; refreshing roots requires a new runtime. No system certificate store is modified.

## Host credential boundary

Add internal host API `Lui_SetTlsOptions(Runtime, LuiTlsOptionsV1*)`, requiring the owner thread, matching structure size/version, and configuration before scripts or network use. Configuration succeeds at most once; invalid attempts can be corrected before startup. Byte inputs are synchronously parsed into immutable provider settings, never retained as caller pointers. Fields are PEM CA trust roots, PKCS#12 server certificate/key bytes, and length-delimited UTF-8 password bytes. No file or native key handle is exposed to application Luau.

Supplied trust roots replace system roots rather than silently broadening them. Trust and credential inputs are each capped at 256 KiB, passwords at 1024 bytes, and PEM roots at 128 certificates. Embedded password nulls, malformed/trailing PEM data, non-CA trust entries, malformed PKCS#12, wrong passwords, and key/certificate mismatch fail without logging secrets. Passwords do not become runtime fields. Host-provided credentials and provider allocations are outside the Luau VM heap quota. Input bytes are trusted host configuration; this is not an untrusted remote credential import endpoint.

`HttpServerService:CreateServer({TLS = true, ...})` uses the configured host server credential. Missing credentials fail with `TlsCredentialRequired` before any bind; there is no plaintext fallback. `TLS` must be Boolean. The existing default is plain HTTP, and the HTTP direction grants remain unchanged. Named credentials, PEM keys, certificate-store/hardware key sources, and manifest integration are deferred. This host configuration is an implementation of the spec's explicit server-credential boundary without promising the proposal's future `TlsService` API.

## Async, resource, and shutdown behavior

HTTPS uses `HttpService:RequestAsync` / `GetAsync`, default port 443, and the same numeric/DNS dialer, host address/port policy, 32 outstanding request/result slots, bounded HTTP parser, and owner-thread materialization as plain HTTP. The existing request deadline covers resolution, TCP connection, TLS handshake, application I/O, and shutdown cleanup. A request never retries as plaintext. Hosted TLS handshakes occupy the existing 32 session slots and per-server connection limit; the existing `TimeoutMs` bounds each handshake, then independently bounds each subsequent HTTP request. Reads remain paused during route dispatch/writing, including TLS. Only scheduler tasks execute routes.

Native TLS EOF must be authenticated by close_notify for close-delimited HTTP content. An abrupt transport EOF is `UnexpectedEof`, preventing a truncated encrypted stream from being accepted as a complete close-delimited response. Length/chunk-framed content settles once its complete authenticated HTTP response has been parsed. The client then attempts TLS shutdown while retaining its original slot and deadline. A timeout, cancellation, or shutdown error during this cleanup closes the socket and delivers the already settled response. Before response settlement, timeout/cancellation fails the request. A server's close response attempts TLS shutdown within its existing request deadline; close/expiry/destroy aborts native I/O and prevents late route delivery. Runtime destruction joins the worker before freeing the VM. Shutdown cleanup never creates an unbounded detached operation.

OpenSSL calls run on the worker. Its async deadline bounds network waits; it does not preempt a single provider crypto call or host-time credential parsing. Peer certificate bounds and admission limits constrain work. There are no modal errors, automatic certificate dialogs, or automatic client key selection.

## Build and qualification

The Windows provider needs an existing Perl and the Visual Studio MSVC x64 CMake generator. NMake builds its cached static dependencies serially; the surrounding native build retains bounded parallelism. No provider DLL or machine installation is needed. The upstream Apache 2.0 license is retained in `third_party/licenses/OpenSSL.txt` and copied to native output and Windows host build/publish folders. Changing the pinned provider or build scripts invalidates the applicable dependency steps while preserving other caches.

`tests/network/Tls.cpp` covers configured and default trust, DNS SNI/IP SANs, wrong names, expiry, no application bytes before verification, TLS 1.2 interoperability and legacy refusal, handshake stalls, complete-response shutdown stalls, abrupt EOF, hosted IPv4/IPv6 binary exchanges, scheduler delivery, server close/handshake timeout, cancellation, runtime destruction, host configuration validation, and missing server credentials. Fixtures are public synthetic credentials, never production keys, with no certificate-store installation. Windows/Linux qualification is recorded in [Foundation C notes](../NETWORKING-FOUNDATION-C.md).

Provider references: [OpenSSL Windows build](https://github.com/openssl/openssl/blob/openssl-3.5.9/NOTES-WINDOWS.md), [peer identity checking](https://docs.openssl.org/3.5/man3/SSL_set1_host/), [certificate-list bounds](https://docs.openssl.org/3.5/man3/SSL_CTX_set_max_cert_list/), and [verification depth](https://docs.openssl.org/3.5/man3/SSL_CTX_set_verify/).
