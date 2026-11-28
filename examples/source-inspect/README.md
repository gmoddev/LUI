# Source inspection

Pipe up to 4096 bytes into `lui run lui.json --mode headless --stdio true --tool SDK/Tools/LuiTypeCheck --tool-cwd DIRECTORY`. Use LuiTypeCheck.exe on Windows. A valid input returns JSON and exit 0. A syntax error returns diagnostics and exit 1.
