# Persistent notebook

Create a dedicated private directory (mode 0700 on Linux). From this project, run `lui run lui.json --mode headless --stdio true --providers SQLite --database-root <directory> -- add "first record"`. Run the same command with `list` or `check` to view recent records or check database integrity. Two LUI processes can append to the same notebook concurrently. Each transaction holds the directory's exclusive lock until commit or rollback. Reserve this directory for LUI and grant access only to the operator and the application account.
