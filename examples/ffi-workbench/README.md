# Compute a native checksum

Press Run, then edit the text field while a bounded native job computes its checksum. A 16 ms timer polls for completion. Cancel signals the job. Closing the window joins the job and retires its callback.

Create a project with `lui new Demo --template ffi-workbench --sdk <sdk>`. Check it with `lui check Demo/lui.json --mode project --sdk <sdk>`. Build it with `lui build Demo/lui.json --mode project --sdk <sdk> --output App`.

From the output directory run `Lui.WinUI.exe --project Application/lui.json --allow-native` or `bin/lui-gtk --project Application/lui.json --allow-native`.
