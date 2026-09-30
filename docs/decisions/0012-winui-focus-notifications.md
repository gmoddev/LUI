# 0012: WinUI focus notifications

WinUI raises `GotFocus` and `LostFocus` asynchronously. Template children can produce routed notifications after focus has already moved. Forwarding each notification directly gave Luau repeated focus gain and loss events for a single click.

The WinUI host now asks `FocusManager` for the focused element in the source element's `XamlRoot`, walks its visual ancestors to find the nearest LUI view, and forwards a transition only when that view differs from the last one reported. It sends the previous view's loss before the new view's gain. Destroyed views are removed from tracking. The runtime remains authoritative for one focused GUI object and still rejects focus on hidden or disabled objects.

A dedicated Windows qualification script verified a single `FocusLost` then `Focused` when moving between text boxes, Tab navigation around a disabled button, and activation of the enabled button. Native pointer cancellation and screen reader behavior remain to be qualified.
