# See your app

In a trusted workspace, use **LUI: Check Types**, then **LUI: Run App**. Click the button in the native window to change its greeting.

Edit `Src/Main.luau`, save, use **LUI: Stop App** and run again. This preview does not restart the native app on save.

Use **LUI: Inspect Layout** for the independent Instance tree and layout map. It runs a separate app generation, so it does not inspect the state of the native window.

Static checks, native runs and application tests answer different questions. Automated app test commands are planned; Run is the manual testing path in this preview.
