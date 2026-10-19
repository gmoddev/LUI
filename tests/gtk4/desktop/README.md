# GTK desktop qualification

Run this synthetic schema v2 application with the GTK host or package it through CLI build --mode project. It requests Clipboard and Dialogs, has no native provider, and never prints file paths or clipboard/input payloads.

Record OS/backend/GTK version, desktop/window manager, display server, renderer, scale and theme before observations. A VM desktop qualifies its declared configuration; it does not qualify bare-metal input devices or Wayland.

1. Launch the same examples/modular app used by Windows. Activate Add action, select Library/Workshop, return Home and verify retained count. Edit synthetic text. Navigate with Tab/Shift+Tab and activate buttons using Space/Enter.
2. Move/resize/maximize the native window. Verify layout remains usable, close it and confirm process exit. Relaunch and verify a fresh generation.
3. Run this service fixture. Copy/read the emoji canary, then exchange synthetic text with a separate desktop application and verify callback status. Never use real clipboard data.
4. Open/cancel the file chooser; open again and select a synthetic file. Open another chooser then close the application: no later callback or orphan chooser may survive retirement.
5. Change the test desktop theme through light/dark/high contrast. Verify ThemeService labels and native appearance. Relaunch at scale 2 and record geometry/input/usability separately from scale 1.
6. Inspect the live AT-SPI tree for roles, names, descriptions and focus. Record actual screen-reader usage separately; metadata presence alone does not qualify speech or navigation usability.
7. Build, verify and relocate the package to a folder with spaces. Launch Start-LUI.sh from a different working directory. Confirm MIT/dependency notices and executable modes. Exercise native editor launch and close/relaunch in trusted VS Code.

Screenshots/logs remain in ignored build/evidence output. Publish synthetic observations and immutable code/CI identifiers against the [platform matrix](../../../docs/PLATFORMS.md). Record unsupported device/scale/theme/editor cases explicitly.
