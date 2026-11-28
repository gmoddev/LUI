# Receive native notifications

Register a callback, emit from two threads and unregister. The example expects three notices, two accepted values totalling 42, one Luau handler error and zero live handles.

Create a project with `lui new Demo --template ffi-callback --sdk <sdk>`. Check it with `lui check Demo/lui.json --mode project --sdk <sdk>`. Build it with `lui build Demo/lui.json --mode headless --sdk <sdk> --output App`.

Run `Start-LUI.exe --stdio --trusted-ffi` on Windows or `./Start-LUI.sh --stdio --trusted-ffi` on Linux.
