# See your app

In a trusted workspace, use **LUI: Check Types**, then **LUI: Run App**. Click the button in the native window to change its greeting.

Edit `Src/Main.luau`, save, then use **LUI: Restart App**. Save any documents inside
the app first; restart discards its memory. The old app must stop before a new
one launches. **LUI: Stop App** also cancels pending launch/restart. Native apps
do not restart automatically on save. Restart requires development extension
0.1.4; older tools use Stop App followed by Run App.

Use **LUI: Inspect Layout** for the independent Instance tree and layout map. It runs a separate app generation, so it does not inspect the state of the native window.

Inspect Layout refuses privileged capabilities and native extensions. Use Run App
for network or storage projects. HttpService needs `network.client` and
HttpServerService needs `network.server` in `lui.json` Capabilities; service names
are not capability tokens. Read Output > LUI Preview when startup fails. Older
preview.1 tools can omit the underlying native reason; newer development tools
report fixed corrective advice without private source or arbitrary stderr.

Static checks, native runs and application tests answer different questions. Automated app test commands are planned; Run is the manual testing path in this preview.
