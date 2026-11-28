# Regex line filter

Pipe a text stream to `lui run lui.json --mode headless --stdio true --providers Regex -- "WARN|ERROR"`. Matching input lines are written unchanged, including their line terminators. Each line is limited to 4096 bytes. The pattern is compiled once per command.
