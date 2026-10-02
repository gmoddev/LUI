# 0023: TCP first slice and coroutine resumption

**Status:** accepted for the first Networking Foundation A slice.

## Decision

Run scheduled Luau callbacks as scheduler-owned coroutines. An asynchronous operation registers exactly one waiting task reference; the transport worker posts a result, and `Lui_Pump` resumes it on the owner thread. Unexpected bare coroutine yields are reported and released. Runtime destruction stops and joins the worker, discards queued results, and releases suspended references before closing Luau.

Pin standalone Asio 1.36.0 at commit `231cb29bab30f82712fcd54faaea42424cc6e710`. Its Boost Software License 1.0 permits the intended use. Keep transport handles private and require host-declared `network.client`, `network.server`, and `network.raw` grants, with raw plus direction required for TCP. A listener defaults to IPv4 loopback, supports explicit IPv6 or numeric addresses, and rejects `DualStack` until a paired same-port implementation is qualified. The first implementation bounds resources and applies a ten-second outbound connection deadline.

## Rationale and follow-up

The previous `task` scheduler called functions with `lua_pcall`, which could not yield. The coroutine bridge gives TCP a shared owner-thread resumption model without letting a worker call Luau. Explicitly rejecting dual-family listening avoids divergent Windows/Linux IPv6 defaults and partial binds. Future Foundation A work should add paired dual-family listeners, parallel family dialing, connection state signals, and configurable host address/port policy before claiming the full proposed contract.
