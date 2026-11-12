# Start small

Use **LUI: Create Project**, choose a parent folder and enter a name. LUI creates a new directory with `Src/Main.luau`, `lui.json` and an exact SDK pin.

Open `Src/Main.luau` in the new folder. The starter displays a greeting and a button. Existing folders are preserved.

Keep `LuiToolchain.json` in version control. Additional source files must be declared
in the manifest. In development extension 0.1.5, open a saved `.luau` file inside
`Modules.Root` and use **LUI: Add File to Project**. Review the unsaved manifest
edit, save it and run Check Types. The Problems warning provides the same action.
