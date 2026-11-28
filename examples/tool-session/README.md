# Inspect Git objects interactively

This utility keeps a single Git process running. Give it one revision, object ID or revision-qualified path per input line. It prints Git's object ID, type and size after each query. Git reports missing objects on the corresponding output line.

From the example directory, select your Git executable and a repository it may inspect.

~~~text
lui run lui.json --mode headless --stdio true --tool <absolute-git-executable> --tool-cwd <repository>
~~~

Try the following two lines in one session.

~~~text
HEAD
HEAD^{tree}
~~~

Send EOF to finish, or redirect a text file containing queries into the command. Each response is written before the utility reads another query. Queries accept up to 4,095 bytes. The session has a 30-second deadline and a 64 KiB combined output limit.
