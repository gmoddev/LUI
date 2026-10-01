# 0021 — Explicit native preview launcher

**Date:** 2026-10-01

## Context

The isolated preview provides authoritative Instance and layout data but no Windows rendering. `lui run` already launches the real WinUI host from the CLI. The editor needs a way to open that rendering without running application Luau inside the extension host.

## Decision

The VS Code extension adds **LUI: Open Native Preview** and **LUI: Stop Native Preview**. On Windows and in a trusted workspace, it resolves an explicit or repository-built `Lui.WinUI.exe`, requires `LuiRuntime.dll` beside it, and passes the manifest to the WinUI host for validation in a separate process. The editor tracks that process and reports failures through its Output channel and a temporary status-bar message. The native window remains independent of the headless Explorer preview. Saving source does not restart the native process; users reopen the window when they want updated rendering.

## Consequences

Native preview uses the manifest's actual capabilities and extensions, like `lui run`. The command depends on a built Windows host and is unavailable on other platforms. The WinUI host keeps its own nonmodal log file under local application data. Exact native rendering is available without changing the version 1 headless preview protocol. Automatic native reload, synchronized selection, and native input forwarding are separate future work.
