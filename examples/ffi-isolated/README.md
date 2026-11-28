# Run a supervised native call

Call sqrt for two inputs in one FfiBroker child and print `isolated=42`. The parent needs the selected Process service.

Create a project with `lui new Demo --template ffi-isolated --sdk <sdk>`. Check it with `lui check Demo/lui.json --mode project --sdk <sdk>`. Build it with `lui build Demo/lui.json --mode headless --sdk <sdk> --output App`.

Run the standalone launcher with `--stdio --tool <absolute broker> --tool-cwd <absolute working directory>`. The broker is in the SDK Tools directory.
