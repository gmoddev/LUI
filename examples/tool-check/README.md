# Check a project with a selected tool

The operator selects the executable for this project command. For example, choose the SDK's native type checker as `--tool`, select the project directory with `--tool-cwd`, and pass the tool's arguments following `--`. Run with `lui run lui.json --mode headless --stdio true --tool <absolute checker> --tool-cwd <directory> -- --help`. The application forwards the selected tool's stdout, stderr and exit status. Arguments are passed individually.
