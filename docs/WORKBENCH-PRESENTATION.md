# Presenting a changing image

`LuiWorkbench/Presentation` binds a producer to a LUI `FrameSurface`. The application owns the surface, the source of its frames, and the window. Each call to `Attach(Surface, Width, Height)` returns a new ticket, even if those arguments are identical. Capture that ticket in each asynchronous producer callback. Pass it to `Submit(Ticket, Sequence, Pixels, Stride, Format)` with an increasing safe-integer sequence. The first sequence may be zero.

Attach again when the producer session, window attachment, or requested buffer dimensions change. This clears the previous image and fences outstanding submissions. A late `Detach(OldTicket)` leaves the current attachment intact. On hide, detach and pause the producer. After restore or native-device failure, create a fresh attachment and resubmit the latest content. Destroying a surface invalidates its ticket. Calling `Destroy()` on the controller clears its current target and retires the controller. The application retains ownership of each control.

Frame dimensions and stride are pixels. LUI lays out the control in logical units. Choose the requested resolution for the current surface size and display scale within the FrameSurface envelope (1920 × 1080, 8 MiB). The existing FrameSurface contract owns validation, buffer copying, bounded replacement of pending frames and backend synchronization. A successful submission means admission. Check `PlatformService:Supports("FrameSurface")` for native presentation capability and `FrameState` for backend failure. A headless runtime can accept a frame without displaying it.

The signal-analysis example submits its spectrum to a real FrameSurface on supported backends, detaches while hidden and replaces the ticket when moved. Its ordinary controls and keyboard focus remain with the dock panels. A backend without native surface support renders the example using ordinary LUI controls. Neither interface exports a raw OS or graphics handle to application scripts.

## Window metrics for dock hosts

A trusted desktop host can register the optional `DesktopWindowMetrics.json` declaration. Pass `WindowMetrics.New(MainWindow)` and the adapter's `OnWindow` callback to `DockHost.New`. Give every participating window a unique name. The provider resolves only live windows in this runtime. On Windows, logical client coordinates use the native client origin and per-window DPI. Platform outer bounds are in signed physical desktop pixels. Restoring a placement clamps it to a current monitor's working area. Secondary native windows belong to their main owner. A minimized window returns unavailable bounds to avoid overwriting its restored placement.

On Wayland, transient ownership can be supplied without a global desktop position. Use compositor placement and the dock host's window-local targeting and redock action when global coordinates are unavailable. Portable layouts store logical structure independently of optional platform placement.
