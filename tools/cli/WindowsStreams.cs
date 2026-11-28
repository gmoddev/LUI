using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Lui.Cli;

// Process.Start without a redirected .NET stream omits STARTF_USESTDHANDLES.
// Explicitly inherit just these three handles, preserving binary pipes and TTYs.
internal static class WindowsStreams
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Startup
    {
        public int Size;
        public nint Reserved, Desktop, Title;
        public uint X, Y, Width, Height, Columns, Rows, Fill, Flags;
        public ushort Show, ReservedBytes;
        public nint ReservedData, Input, Output, Error, Attributes;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInfo { public nint Process, Thread; public uint ProcessId, ThreadId; }
    [StructLayout(LayoutKind.Sequential)]
    private struct BasicLimits
    {
        public long ProcessTime, JobTime;
        public uint Flags;
        public nuint MinWorkingSet, MaxWorkingSet;
        public uint ActiveProcesses;
        public nuint Affinity;
        public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct ExtendedLimits
    {
        public BasicLimits Basic;
        public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
        public nuint ProcessMemory, JobMemory, PeakProcess, PeakJob;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern SafeFileHandle CreateJobObjectW(nint Security, string? Name);
    [DllImport("kernel32.dll")] private static extern bool SetInformationJobObject(SafeFileHandle Job, int Kind, ref ExtendedLimits Limits, int Size);

    [DllImport("kernel32.dll")] private static extern nint GetStdHandle(int Id);
    [DllImport("kernel32.dll")] private static extern nint GetCurrentProcess();
    [DllImport("kernel32.dll")] private static extern uint GetConsoleCP();
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint CreateFileW(string Name, uint Access, uint Share, nint Security, uint Creation, uint Flags, nint Template);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(nint Handle);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool DuplicateHandle(nint SourceProcess,
        nint Source, nint TargetProcess, out nint Target, uint Access, bool Inherit, uint Options);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool InitializeProcThreadAttributeList(
        nint List, int Count, uint Flags, ref nuint Bytes);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool UpdateProcThreadAttribute(
        nint List, uint Flags, nuint Attribute, nint Value, nuint Bytes, nint Previous, nint PreviousBytes);
    [DllImport("kernel32.dll")] private static extern void DeleteProcThreadAttributeList(nint List);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcessW(string Image, StringBuilder Command, nint ProcessSecurity, nint ThreadSecurity,
        bool Inherit, uint Flags, nint Environment, string Directory, ref Startup Start, out ProcessInfo Child);
    [DllImport("kernel32.dll")] private static extern uint WaitForSingleObject(nint Handle, uint Milliseconds);
    [DllImport("kernel32.dll")] private static extern bool GetExitCodeProcess(nint Process, out uint Code);

    // Windows CRT argv quoting, including runs of backslashes before a quote
    // or the closing delimiter. The executable path is separately specified.
    private static void AppendArgument(StringBuilder Text, string Argument)
    {
        Text.Append('"');
        int Slashes = 0;
        foreach (char Value in Argument)
        {
            if (Value == '\\') { ++Slashes; continue; }
            Text.Append('\\', Value == '"' ? Slashes * 2 + 1 : Slashes);
            Text.Append(Value);
            Slashes = 0;
        }
        Text.Append('\\', Slashes * 2).Append('"');
    }

    internal static int Run(ProcessStartInfo Start)
    {
        var Command = new StringBuilder();
        AppendArgument(Command, Start.FileName);
        foreach (var Argument in Start.ArgumentList)
        {
            Command.Append(' ');
            AppendArgument(Command, Argument);
        }
        if (Command.Length >= 32767) throw new ArgumentException("Application argument list is too long");
        nint[] Handles = new nint[3];
        nint List = 0, Values = 0, JobValue = 0;
        using var Job = CreateJobObjectW(0, null);
        var Limits = new ExtendedLimits { Basic = new BasicLimits { Flags = 0x2000 } };
        if (Job.IsInvalid || !SetInformationJobObject(Job, 9, ref Limits, Marshal.SizeOf<ExtendedLimits>()))
            throw new IOException("[LUI:CLI] ProcessOwnershipUnavailable");
        bool Initialized = false;
        ProcessInfo Child = default;
        try
        {
            for (int Index = 0; Index < Handles.Length; ++Index)
            {
                nint Source = GetStdHandle(-10 - Index);
                bool Missing = Source == 0 || Source == -1;
                if (Missing) Source = CreateFileW("NUL", Index == 0 ? 0x80000000u : 0x40000000u, 3, 0, 3, 0, 0);
                try
                {
                    if (Source == -1 || !DuplicateHandle(GetCurrentProcess(), Source, GetCurrentProcess(),
                        out Handles[Index], 0, true, 2)) throw new IOException("[LUI:CLI] StandardHandleUnavailable");
                }
                finally { if (Missing && Source != -1) CloseHandle(Source); }
            }
            nuint Bytes = 0;
            InitializeProcThreadAttributeList(0, 2, 0, ref Bytes);
            if (Bytes == 0) throw new IOException("[LUI:CLI] StandardHandleUnavailable");
            List = Marshal.AllocHGlobal(checked((nint)Bytes));
            Initialized = InitializeProcThreadAttributeList(List, 2, 0, ref Bytes);
            if (!Initialized) throw new IOException("[LUI:CLI] StandardHandleUnavailable");
            Values = Marshal.AllocHGlobal(Handles.Length * IntPtr.Size);
            Marshal.Copy(Handles, 0, Values, Handles.Length);
            if (!UpdateProcThreadAttribute(List, 0, 0x20002, Values, (nuint)(Handles.Length * IntPtr.Size), 0, 0))
                throw new IOException("[LUI:CLI] StandardHandleUnavailable");
            JobValue = Marshal.AllocHGlobal(IntPtr.Size);
            Marshal.WriteIntPtr(JobValue, Job.DangerousGetHandle());
            if (!UpdateProcThreadAttribute(List, 0, 0x2000d, JobValue, (nuint)IntPtr.Size, 0, 0))
                throw new IOException("[LUI:CLI] ProcessOwnershipUnavailable");
            var Info = new Startup { Size = Marshal.SizeOf<Startup>(), Flags = 0x100, Attributes = List,
                Input = Handles[0], Output = Handles[1], Error = Handles[2] };
            uint Flags = 0x80000u | (GetConsoleCP() == 0 ? 0x08000000u : 0u);
            if (!CreateProcessW(Start.FileName, Command, 0, 0, true, Flags, 0, Start.WorkingDirectory, ref Info, out Child))
                throw new IOException("[LUI:CLI] LaunchFailed");
        }
        finally
        {
            if (Initialized) DeleteProcThreadAttributeList(List);
            if (List != 0) Marshal.FreeHGlobal(List);
            if (Values != 0) Marshal.FreeHGlobal(Values);
            if (JobValue != 0) Marshal.FreeHGlobal(JobValue);
            foreach (nint Handle in Handles) if (Handle != 0) CloseHandle(Handle);
        }
        try
        {
            if (WaitForSingleObject(Child.Process, uint.MaxValue) != 0 || !GetExitCodeProcess(Child.Process, out uint Code))
                throw new IOException("[LUI:CLI] WaitFailed");
            return unchecked((int)Code);
        }
        finally { CloseHandle(Child.Thread); CloseHandle(Child.Process); }
    }
}
