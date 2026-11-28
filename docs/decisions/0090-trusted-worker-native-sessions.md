# 0090. Explicit worker-local FFI sessions

A policy-enabled host verifies the captured native project and FFI provider
and explicitly opts into trusted FFI workers. Only `Workers.StartTrusted`
requests a worker-local session. Ordinary actors keep their pure environment.

Each trusted actor initializes its own ABI-v1 Ffi session on its worker
thread. It shares the verified loader pin and immutable compiled graph.
The worker bridge exposes only Ffi, and bounded serialized messages carry
ordinary values. Callback registration and polling stay with the existing
owner-affine profile.

Blocking calls retain the actor, session, image and quota slot until return.
Close cancels delivery, and shutdown of the provider session runs on the
worker. Native globals, loader locks and OS privileges are process-wide.
Use the broker profile for process timeouts or fault isolation.

The headless x64 profile accepts both `--trusted-ffi` and
`--trusted-ffi-workers`. Custom hosts can opt in with
`LUI_PROJECT_NATIVE_TRUSTED_WORKERS` on the existing native load options.
