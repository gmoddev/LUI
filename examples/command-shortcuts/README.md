# Editor command shortcut (preview.3 candidate)

This complete example uses the optional Commands library's `AllowWhileEditing`
option. It is not included in the published preview.2 SDK. From a prepared
preview.3 candidate SDK, copy `Examples/command-shortcuts` to a sibling directory,
then run `lui check` and `lui run` there using that candidate SDK.

Focus the text box, type and press Ctrl+S. The status reports a byte count and
the text stays in the editor. The command captures text in memory for this
session; it does not claim to save a file. The example requires no capability.
Composition, repeat, reserved editing keys and Ctrl+Alt remain protected.

Repository qualification can stage the same project without downloads:
`python scripts/Stage-LibraryExample.py --example command-shortcuts --output <new-directory>`.
That is source qualification; SDK recipients use the complete packaged example
and its existing lock/vendor files.
