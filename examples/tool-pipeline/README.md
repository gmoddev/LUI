# Fingerprint tracked Git paths

This utility prints a Git blob ID for the repository's tracked path list. Use it to compare index path sets between checkouts. The first Git process streams NUL-delimited names from the index. The second process hashes those exact bytes, including paths containing spaces or line breaks. File contents do not enter the hash.

From the example directory, select Git and the repository to inspect.

~~~text
lui run lui.json --mode headless --stdio true --tool <absolute-git-executable> --tool-cwd <repository>
~~~

Both stages use the same executable and working-directory grant. The example treats any Git diagnostic on stderr as a failure. The source stage accepts up to 128 KiB of path-list output and diagnostics, and both stages have a 10-second deadline.
