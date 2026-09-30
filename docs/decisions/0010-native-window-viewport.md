# 0010: Native window viewport updates

Foundation 1 layout originally used the requested `Window.Size` for every pass. Resizing or maximizing a WinUI window changed the native content area but left LUI's layout bounds unchanged, so a grid never gained or lost columns.

The native host now reports the window content size in logical units through the internal `Lui_WindowResized` callback. LUI stores that viewport separately from the requested `Size`, uses it for layout and `AbsoluteSize`, and keeps the requested property stable for application code. Backend resize notifications queue while Luau or a backend callback is active, then run on the authoritative scheduler thread. Arrangement tells the backend whether the bounds came from a script size request or a native viewport update, preventing a native resize from being sent back as another request.

A later `Window.Size` assignment or change to its `UISizeConstraint` clears the viewport override and requests a native size. Native window minimum and maximum enforcement and DPI qualification remain for production Windows work; a manually resized viewport can temporarily be outside a window constraint.
