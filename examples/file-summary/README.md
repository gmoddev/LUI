# File summary

Run `lui run --mode headless --stdio true --read-file PATH` from this staged SDK example. The application receives a read-only FileService handle for the file selected by the caller. It reports the byte count and number of LF characters, processing at most 64 KiB at a time. FileService keeps native paths private.

The example uses `Files.Reader(File)` to keep track of its position. An empty buffer marks EOF. It closes the cursor and its selected file after the final chunk.
