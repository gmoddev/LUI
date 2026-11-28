# Create an in-memory SQLite notebook

The Notes module creates a table, inserts two rows, prints inserted=2 and closes the connection through the system SQLite C API.

Create a project with `lui new Demo --template ffi-sqlite --sdk <sdk>`. Check it with `lui check Demo/lui.json --mode project --sdk <sdk>`. Build it with `lui build Demo/lui.json --mode headless --sdk <sdk> --output App`.

Run `Start-LUI.exe --stdio --trusted-ffi` on Windows or `./Start-LUI.sh --stdio --trusted-ffi` on Linux.
