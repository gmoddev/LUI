# Digest

Print the SHA-256 digest of a binary record of up to 65,536 bytes. Run `lui run lui.json --mode headless --stdio true --providers Crypto < record`. Stdout contains one lowercase hexadecimal digest and a newline. Input or write errors exit with status 1. The app uses the target's Crypto provider and the optional Encoding and Stdio modules.
