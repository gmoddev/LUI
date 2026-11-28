# Parallel viewer

Open this project with the Preview.3 desktop host. Two isolated workers compute successive blocks of 240,000 deterministic integer updates. Type in the text field while the completed batch count advances. Stop closes both actors and cancels pending starts. Closing the window also retires the workers.

Run `lui check Examples/parallel-viewer/lui.json --sdk <sdk>`, then `lui run Examples/parallel-viewer/lui.json --sdk <sdk> --mode project`. The project captures Workers, Messages and Json. Only the owner VM has access to the window.
