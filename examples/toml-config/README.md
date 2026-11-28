# TOML configuration

Pipe a TOML document to `lui run lui.json --mode headless --stdio true --providers Toml`. A document containing `Name = "Ada"` and `Copies = 2` on separate lines prints two greetings. Config validates known keys, types and defaults. The application limits Copies to 0 through 8.
