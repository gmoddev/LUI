# Choose the documentation you need

For application development, start with the
[SDK documentation website](https://gmoddev.github.io/LUI/). It has complete
examples, required setup, expected results and a version selector. You do not
need to build LUI, implement a provider or read the native ABI to make an app.

| I want to… | Read |
| --- | --- |
| Create and run an app | [First project](https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.2/learn/first-project/) and [SDK setup](SDK.md) |
| Understand a property, method or signal | [API reference](https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.2/api/) and [how to read its notation](https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.2/guides/reading-the-api/) |
| Use the optional app libraries | [Bundled libraries](https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.2/guides/libraries/) |
| Inspect a generated API inventory | [API.md](API.md); this machine-derived inventory is precise, but the website adds usage explanations/examples |
| Understand the framework’s design and invariants | [Architecture](ARCHITECTURE.md) and [application contract](SPEC.md) — deeper engineering reading |
| Build or test LUI itself | [Build](BUILD.md) and [testing](TESTING.md) — framework-contributor workflows, not SDK app setup |
| Implement a custom native provider or embedding host | [Owned native services](NATIVE-SERVICES.md) and ABI headers — advanced integration contracts |

Public Luau APIs and public integration interfaces serve different audiences.
An opaque NativeHandle is an app-visible resource with a defined lifetime; it is
not an exposed toolkit object or a pointer you can invent. Private support
sources bundled by an example are implementation details unless the library
explicitly lists them as public modules.

Use the docs version matching your project pin. Preview releases remain
experimental; platform support is limited to the profiles and gaps stated in
[the platform matrix](PLATFORMS.md). Proposed work and historical qualification
records do not promise that a feature ships in your SDK.
