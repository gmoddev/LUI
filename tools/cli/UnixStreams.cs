using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Lui.Cli;

internal static class UnixStreams
{
    [DllImport("libc", SetLastError = true)]
    private static extern int execv([MarshalAs(UnmanagedType.LPUTF8Str)] string Path, nint Arguments);

    // An exec preserves pipes and PID. A signal sent only to the SDK CLI
    // reaches the owner that cancels and joins its I/O workers.
    internal static int Run(ProcessStartInfo Start)
    {
        nint[] Strings = new nint[Start.ArgumentList.Count + 2];
        nint Vector = 0;
        string Previous = Directory.GetCurrentDirectory();
        try
        {
            Strings[0] = Marshal.StringToCoTaskMemUTF8(Start.FileName);
            for (int Index = 0; Index < Start.ArgumentList.Count; ++Index)
                Strings[Index+1] = Marshal.StringToCoTaskMemUTF8(Start.ArgumentList[Index]);
            Vector = Marshal.AllocHGlobal(Strings.Length * IntPtr.Size);
            Marshal.Copy(Strings, 0, Vector, Strings.Length);
            Directory.SetCurrentDirectory(Start.WorkingDirectory);
            execv(Start.FileName, Vector);
            throw new IOException("[LUI:CLI] LaunchFailed");
        }
        finally
        {
            Directory.SetCurrentDirectory(Previous);
            if (Vector != 0) Marshal.FreeHGlobal(Vector);
            foreach (nint Value in Strings) Marshal.FreeCoTaskMem(Value);
        }
    }
}
