# Selected files

An unreleased preview.3 desktop example: open and read a UTF-8 text document,
save a copy through the native Save as picker, or count entries in a chosen folder.
The optional LuiWorkbench Files module keeps provider buffers and handles out of
ordinary application code. It does not grant access to arbitrary native paths.

Stage with `python scripts/Stage-LibraryExample.py --example selected-files
--output build/selected-files`, or use the SDK's complete Examples/selected-files
project. Check with `lui check PATH/TO/lui.json --sdk PATH/TO/SDK`; launch with
`lui run PATH/TO/lui.json --sdk PATH/TO/SDK --mode project`. The manifest declares
Dialogs. Use the candidate desktop host with a visible window.

Dismissal returns nil without an error. Read/write/list failures go to the
completion callback. Writes are bounded atomic data replacements; cancellation
does not undo a committed save, and an error after replacement can leave the new
file in place. This sample keeps editor text when completion is uncertain.
It is intentionally small: it does not add an unsaved-document management system;
File Desk demonstrates dirty-close recovery and app-private scratchpad storage.

The native service supports local regular files, a selected read-only folder and
single-leaf child reads. It refuses links/reparse paths, hardlinked files, device
paths, direct UNC paths and traversal. Network-mounted filesystems are unqualified.
Native limits are4MiB per file,256 listed
entries and32 open resources; the example editor uses a smaller16,000-byte limit.
Close files/folders promptly and dispose the picker on application retirement.
Current target hosts are Windows WinUI3 and Linux GTK4.10+; qualify actual native
input/pickers on the intended desktop. Browser/mobile and persisted grants are
unsupported. Application state belongs in Preferences/Storage instead.
