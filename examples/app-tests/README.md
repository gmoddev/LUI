# Application tests

Copy this example out of the development SDK. From the project folder, run `lui check`, then `lui run --mode headless --diagnostics development`. The first case writes a two-byte header and checks its Base64 receipt. The second verifies cleanup. Change the expected receipt to exercise the failing headless exit status. The project vendors Spec, Binary and Encoding.
