# TLS dependency and build

LUI's private TLS provider uses OpenSSL via Asio. The provider is implementation detail; ordinary Luau exposes HTTPS calls and hosted HTTP's Boolean `TLS` option.

## Windows

Use the existing Visual Studio MSVC x64 CMake generator and an existing Perl on PATH. The build downloads upstream OpenSSL 3.5.9 with SHA-256 `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a` and builds static libraries under `<build>/_deps/lui-openssl`. Release source commit: `45e844fa2a14ec92d146bd8f5778ac130b6625fb`. No machine installation or OpenSSL DLL is required. Compression, loadable modules, legacy provider, assembly, vendor tests, and vendor documentation are disabled in this initial Windows profile. Library builds use OpenSSL's standard CRT-neutral static C configuration.

The native build preserves this cache across incremental builds and Debug/Release consumers of the same compiler/architecture tree. OpenSSL's NMake build is serial; the surrounding CMake build uses its selected job bound. Source, build scripts, and archive hash changes trigger the relevant steps. Logs are inside the dependency's stamp directory. Native build output and the WinUI host build/publish folder carry `THIRD-PARTY-LICENSES/OpenSSL.txt`.

## Linux

CMake requires the distribution's OpenSSL 3 development package (`libssl-dev` on Ubuntu). The distribution owns security updates; LUI links its OpenSSL libraries and uses its default trust paths. This does not install a Linux UI backend. Windows CI checks the pinned static provider; Linux CI checks the distribution provider with the same tests.

## Trust and credentials

See [Decision 0031](decisions/0031-portable-tls-and-https.md) for verification, limits, host credentials, and deferred policy features. `Lui_SetTlsOptions` consumes explicit in-memory host data; neither ordinary Luau nor the current manifest loads private keys. Test bundles are public synthetic fixtures and never modify OS certificate stores.

## Maintenance

Track upstream OpenSSL 3.5 LTS security releases and update the Windows URL, hash, commit record, and build cache namespace together. Requalify Windows/Linux tests after a provider update. Retain the Apache 2.0 notice in distributed runtime packages. Linux security maintenance follows the selected supported distribution.
