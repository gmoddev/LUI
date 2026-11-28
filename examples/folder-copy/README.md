# Folder copy

Run `lui run lui.json --mode headless --stdio true --write-directory DIRECTORY -- input.bin output.bin`. Both names refer to direct children of the selected directory. The utility streams 64 KiB chunks and atomically publishes the completed copy. Existing destination bytes remain visible until commit.
