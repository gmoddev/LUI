using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Lui.WinUI;

internal static class LuiDiagnostics
{
    private static readonly object Gate = new();
    private static string? LogPath;
    private static bool Enabled;
    private static bool PolicyEnabled;
    private static string? PolicyDirectory;
    private static Mutex? FileGate;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AllocConsole();
    [DllImport("kernel32.dll")]
    private static extern uint SetErrorMode(uint Mode);

    public static void Initialize(bool ShowDiagnostics, bool SaveLog = false,bool Policy=false,string? OwnerLogDirectory=null)
    {
        // Keep OS crash and critical-error dialogs out of the user's foreground session.
        SetErrorMode(0x0001 | 0x0002 | 0x8000);
        PolicyEnabled=Policy;
        if (!ShowDiagnostics && !SaveLog) return;
        Enabled = true;
        try
        {
            string DirectoryPath = OwnerLogDirectory ?? Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LUI", "Logs");
            Directory.CreateDirectory(DirectoryPath);
            if((File.GetAttributes(DirectoryPath)&FileAttributes.ReparsePoint)!=0) throw new IOException("Linked log directory");
            if(Policy && SaveLog) {
                PolicyDirectory=DirectoryPath;
                string Identity=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(Path.GetFullPath(DirectoryPath).ToUpperInvariant())));
                FileGate=new Mutex(false,"Local\\LUI-PolicyLogs-"+Identity);
            }
            if(SaveLog || !Policy) LogPath = Path.Combine(DirectoryPath, $"LUI-{(Policy?"Policy-":"")}{DateTime.Now:yyyyMMdd-HHmmss}-{Environment.ProcessId}.log");
        }
        catch (Exception Error) { Trace.TraceError(Policy?"[LUI:Diagnostics] SinkUnavailable":"[LUI:Diagnostics] Could not create log file: "+Error); }

        try
        {
            if (ShowDiagnostics && AllocConsole())
            {
                Console.SetOut(new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = true });
                Console.SetError(new StreamWriter(Console.OpenStandardError()) { AutoFlush = true });
                Console.Title = "LUI Diagnostics";
            }
        }
        catch (Exception Error) { Trace.TraceError(Policy?"[LUI:Diagnostics] SinkUnavailable":"[LUI:Diagnostics] Could not open console: "+Error); }

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
        if(PolicyEnabled) Message="[LUI:Diagnostics] FrameworkEvent";
        if(Message.Length>4080) Message=Message[..4080]+"[Truncated]";
        string Line = $"{DateTime.Now:HH:mm:ss.fff} [LUI:{Subsystem}] {Message}";
        lock (Gate)
        {
            try { Console.WriteLine(Line); }
            catch { }
            if (LogPath is not null)
            {
                bool Acquired=false;
                try {
                    if(PolicyEnabled) {
                        try {Acquired=FileGate!.WaitOne(0);} catch(AbandonedMutexException) {Acquired=true;}
                        if(!Acquired) return; // A busy sink drops this projected record without blocking UI.
                        Prune(3);
                        if(File.Exists(LogPath) && ((File.GetAttributes(LogPath)&FileAttributes.ReparsePoint)!=0)) throw new IOException("Linked log");
                        int Bytes=System.Text.Encoding.UTF8.GetByteCount(Line+Environment.NewLine);
                        if(File.Exists(LogPath) && new FileInfo(LogPath).Length+Bytes>1024*1024) File.WriteAllText(LogPath,string.Empty);
                    }
                    File.AppendAllText(LogPath, Line + Environment.NewLine);
                }
                catch {LogPath=null; Trace.TraceError("[LUI:Diagnostics] SinkUnavailable");}
                finally {if(Acquired) FileGate!.ReleaseMutex();}
            }
        }
    }

    private static void Prune(int Maximum)
    {
        if(PolicyDirectory is null) return;
        var Owned=Directory.EnumerateFiles(PolicyDirectory,"LUI-Policy-*.log").Select(Path=>new FileInfo(Path))
            .Where(File=>(File.Attributes&FileAttributes.ReparsePoint)==0).OrderByDescending(File=>File.CreationTimeUtc).ToArray();
        foreach(var File in Owned.Where(File=>File.FullName!=LogPath).Skip(Maximum)
            .Concat(Owned.Where(File=>DateTime.UtcNow-File.CreationTimeUtc>TimeSpan.FromHours(24))).DistinctBy(File=>File.FullName)) File.Delete();
    }

    public static void Error(string Subsystem, string Message)
    {
        if(PolicyEnabled) Message="[LUI:Diagnostics] OperationFailed";
        Trace.TraceError("[LUI:{0}] {1}", Subsystem, Message);
        Log(Subsystem, "ERROR " + Message);
    }
}
