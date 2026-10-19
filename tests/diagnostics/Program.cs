using Lui.WinUI;
using System.Diagnostics;
if(!OperatingSystem.IsWindows()) return;
if(args.Length==2 && args[0]=="--writer") {
    Console.SetOut(TextWriter.Null); LuiDiagnostics.Initialize(false,true,true,args[1]);
    for(int Index=0;Index<1000;++Index) LuiDiagnostics.Log("PolicyTest","SYNTHETIC_PRIVATE_CANARY");
    return;
}
string Root=Path.Combine(Path.GetTempPath(),"LuiPolicyLogs-"+Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(Root);
var Original=Console.Out;
using StringWriter Captured=new();
using TextWriterTraceListener TraceSink=new(Captured);
Trace.Listeners.Add(TraceSink);
try {
    File.WriteAllText(Path.Combine(Root,"Unrelated.log"),"Preserve");
    for(int Index=0;Index<6;++Index) {
        string FilePath=Path.Combine(Root,$"LUI-Policy-Old-{Index}.log"); File.WriteAllText(FilePath,"Old");
        File.SetCreationTimeUtc(FilePath,DateTime.UtcNow.AddHours(-25));
    }
    Console.SetOut(Captured);
    LuiDiagnostics.Initialize(false,true,true,Root);
    for(int Index=0;Index<20000;++Index) LuiDiagnostics.Log("PolicyTest","SYNTHETIC_PRIVATE_CANARY");
    LuiDiagnostics.Error("Crash", "SYNTHETIC_CRASH_CANARY"); Trace.Flush();
    var Children=new List<Process>();
    try {
        for(int Index=0;Index<6;++Index) {
            ProcessStartInfo Start=new("dotnet"){UseShellExecute=false,CreateNoWindow=true};
            Start.ArgumentList.Add(typeof(LuiDiagnostics).Assembly.Location); Start.ArgumentList.Add("--writer"); Start.ArgumentList.Add(Root);
            Children.Add(Process.Start(Start)??throw new Exception("writer startup failed"));
        }
        foreach(var Child in Children) if(!Child.WaitForExit(15000) || Child.ExitCode!=0) throw new Exception("writer qualification failed");
    } finally {foreach(var Child in Children) {if(!Child.HasExited) {Child.Kill(true); Child.WaitForExit();} Child.Dispose();}}
    if(Captured.ToString().Contains("CANARY")) throw new Exception("canary in console/trace");
    var Files=Directory.GetFiles(Root,"LUI-Policy-*.log");
    if(Files.Length is 0 or >4 || Files.Any(Name=>new FileInfo(Name).Length>1024*1024 || File.ReadAllText(Name).Contains("CANARY"))) throw new Exception("policy disk bounds/redaction failed");
    if(!File.Exists(Path.Combine(Root,"Unrelated.log")) || Directory.GetFiles(Root,"LUI-Policy-Old-*.log").Length!=0) throw new Exception("owned age retention failed");
    Console.SetOut(Original); Console.WriteLine("[LUI:DiagnosticsTest] Raw console/trace/disk, rotation and owned retention passed");
} finally {Console.SetOut(Original); Trace.Listeners.Remove(TraceSink); Directory.Delete(Root,true);}
