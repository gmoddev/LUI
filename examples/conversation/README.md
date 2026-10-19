# Synthetic conversation workload

This application exercises 10,000 stable items, bounded variable-height rows, insert/edit/delete/append, bottom following, resize, inherited typography/themes, multiline composition, secret settings and asynchronous declared avatars. All data is synthetic and local.

Run `./examples/conversation/Prepare.ps1` on Windows or `python3 examples/conversation/Prepare.py` on Linux to generate the small ignored PNG and pin the reusable modules. Then check/run/build `examples/conversation/lui.json` using the schema v2 [CLI flow](../modular/README.md) and a current native host. Evaluation never generates or downloads assets.

Insert above preserves the visible stable row. Editing changes row heights; deleting an anchor preserves/clamps the old offset. Append/Send follow the bottom only when already there. Settings exercise font changes and masked sticky-sensitive input. The footer reports counts and layout timing without message keys or content.

See [LuiUI](Vendor/LuiUI/README.md), [performance budgets](../../docs/PERFORMANCE.md) and [platform limits](../../docs/PLATFORMS.md). A working example alone does not establish performance or cross-platform qualification.
