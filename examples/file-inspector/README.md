# File inspector

Launch the project in the native desktop host and choose a local regular file. The window displays its name, exact byte length, the first 32 bytes in hexadecimal, and elapsed read time. It reads the same bounded prefix even when the file is several gigabytes long. At startup it displays the operating system and architecture. The project needs the Dialogs capability. The selected file closes immediately after reading. Closing the window disposes pending picker requests.
