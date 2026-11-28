# JSON Lines

Run `lui run --mode headless --stdio true < records.jsonl > normalized.jsonl` from the packaged example. It reads one UTF-8 JSON value per line, sorts object keys, preserves null and empty containers, and emits compact JSON followed by LF. The last input record may omit its newline.

Each record has a 65,535-byte limit before LF, including the optional CR in a CRLF terminator, a depth limit of 32 and an 8,192-node limit. Numbers use Luau doubles. An invalid, blank or oversized record stops processing with status 2. The diagnostic reports the line number. Previously completed records remain on stdout. I/O failure exits with status 1. The project uses the existing Json library and the headless Stdio grant on Windows and Linux.
