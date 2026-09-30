using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Lui.WinUI;

internal static class LuiDiagnostics
{
    private static readonly object Gate = new();
    private static string? LogPath;
    private static bool Enabled;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AllocConsole();

    public static void Initialize(bool ShowDiagnostics, bool SaveLog = false)
    {
        if (!ShowDiagnostics && !SaveLog) return;
        Enabled = true;
        try
        {
            string DirectoryPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LUI", "Logs");
            Directory.CreateDirectory(DirectoryPath);
            LogPath = Path.Combine(DirectoryPath, $"LUI-{DateTime.Now:yyyyMMdd-HHmmss}-{Environment.ProcessId}.log");
        }
        catch (Exception Error) { Trace.TraceError("[LUI:Diagnostics] Could not create log file: {0}", Error); }

        try
        {
            if (ShowDiagnostics && AllocConsole())
            {
                Console.SetOut(new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = true });
                Console.SetError(new StreamWriter(Console.OpenStandardError()) { AutoFlush = true });
                Console.Title = "LUI Diagnostics";
            }
        }
        catch (Exception Error) { Trace.TraceError("[LUI:Diagnostics] Could not open console: {0}", Error); }

        Log("Diagnostics", $"Started; log: {LogPath ?? "unavailable"}");
        AppDomain.CurrentDomain.UnhandledException += (_, Args) =>
            Error("Crash", Args.ExceptionObject?.ToString() ?? "Unhandled exception");
        TaskScheduler.UnobservedTaskException += (_, Args) =>
            Error("Task", Args.Exception.ToString());
        AppDomain.CurrentDomain.ProcessExit += (_, _) => Log("Diagnostics", "Process exiting");
    }

    public static void Log(string Subsystem, string Message)
    {
        if (!Enabled) return;
        string Line = $"{DateTime.Now:HH:mm:ss.fff} [LUI:{Subsystem}] {Message}";
        lock (Gate)
        {
            try { Console.WriteLine(Line); }
            catch { }
            if (LogPath is not null)
            {
                try { File.AppendAllText(LogPath, Line + Environment.NewLine); }
                catch { }
            }
        }
    }

    public static void Error(string Subsystem, string Message)
    {
        Trace.TraceError("[LUI:{0}] {1}", Subsystem, Message);
        Log(Subsystem, "ERROR " + Message);
    }
}
