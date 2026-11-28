# Atomic copy

From a copied SDK example, run `lui run --mode headless --stdio true --write-file destination.bin < input.bin`. The operator authorizes that single destination. The program reads at most 64 KiB per operation and publishes the replacement at EOF. Empty input creates or truncates the destination to zero bytes. The per-writer logical size limit is 128 MiB. The command returns zero after a successful commit and one after a reported read, write or commit failure.

A concurrent reader sees the old file or the committed replacement. A failure after the rename can still leave the new file visible. The program does not retry a failed commit.
