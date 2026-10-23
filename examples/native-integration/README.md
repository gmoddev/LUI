# Native integration example

A synthetic notification and tray example using owned requests and handles.
Run `lui.json` in project mode with the Windows host. Notification submission
does not guarantee display: Windows notification settings still apply. Tray
activation arrives through an owner-delivered progress callback. Watches expire
after five minutes and can be rearmed. Closing the application retires all work.

The fixture uses Development diagnostics for explicit project-wide typechecking
and contains synthetic data. Select Restricted diagnostics before integrating
private application content; Restricted project checking is intentionally refused.

GTK currently reports Notifications/Tray unsupported, and the example disables
their buttons. Storage is an independent trusted-host installation; see
[the service guide](../../docs/NATIVE-SERVICES.md). No private directory is chosen
or created by this example.
