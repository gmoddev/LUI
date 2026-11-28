# Binary pipe copy

From this staged example run `lui run --mode headless --stdio true`. Redirect stdin from a file and stdout to another file in a byte-preserving shell. The application copies 32 KiB at a time and waits for each write before requesting more bytes. It returns 1 on an I/O failure. On Windows use cmd.exe or a PowerShell version with byte-preserving native redirection.
