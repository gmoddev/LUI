using System.Diagnostics;
using Microsoft.UI.Xaml;
using Lui.Project;

namespace Lui.WinUI;

public partial class App : Application
{
    private WinUIBackend? Backend;
    private DispatcherTimer? PumpTimer;

    private void ExitCleanly()
    {
        PumpTimer?.Stop();
        try { Backend?.Dispose(); }
        catch (Exception Error) { LuiDiagnostics.Error("App", "Shutdown failed: " + Error); }
        Exit();
    }

    public App()
    {
        string[] Arguments = Environment.GetCommandLineArgs();
        bool Modular=Arguments.Contains("--project");
        LuiDiagnostics.Initialize(Arguments.Skip(1).Any(Argument => Argument == "--diagnostics"),
            Modular?Arguments.Contains("--diagnostics-log"):Arguments.Skip(1).Any(Argument => Argument == "--native-qualification" || Argument == "--manifest"),Modular);
        UnhandledException += (_, Args) =>
        {
            LuiDiagnostics.Error("Xaml", Args.Exception.ToString());
            Args.Handled = true;
            Environment.ExitCode = 1;
            ExitCleanly();
        };
        InitializeComponent();
    }

    protected override async void OnLaunched(LaunchActivatedEventArgs Args)
    {
        // Canonical close acknowledgement and resource retirement may continue
        // after the final native Window closes. The host owns dispatcher exit.
        DispatcherShutdownMode = DispatcherShutdownMode.OnExplicitShutdown;
        string[] Arguments = Environment.GetCommandLineArgs();
        bool Qualification = Arguments.Skip(1).Any(Argument => Argument == "--native-qualification");
        bool ConversationQualification = Arguments.Contains("--conversation-qualification");
        bool FrameQualification = Arguments.Contains("--frame-qualification");
        bool OwnerWakeQualification=Arguments.Contains("--owner-wake-qualification");
        bool WindowLifecycleQualification=Arguments.Contains("--window-lifecycle-qualification");
        bool NativeTextHostQualification=Arguments.Contains("--native-text-host-qualification");
        bool CodeEditorQualification=Arguments.Contains("--code-editor-qualification");
        bool CodeEditorObservation=Arguments.Contains("--code-editor-observation");
        bool CodeEditorPerformance=Arguments.Contains("--code-editor-performance");
        bool NativeTextVisualQualification=Arguments.Contains("--native-text-visual-qualification");
        try
        {
            string? ManifestArgument = null;
            string? ProjectArgument=null;
            string? QualificationReport=null;
            string? NativeProvider=null;
            string? ScriptArgument = null;
            for (int Index = 1; Index < Arguments.Length; ++Index)
            {
                if (Arguments[Index] is "--diagnostics" or "--native-qualification" or "--conversation-qualification" or "--frame-qualification" or "--frame-observation" or "--owner-wake-qualification" or "--window-lifecycle-qualification" or "--native-text-host-qualification" or "--code-editor-qualification" or "--code-editor-observation" or "--code-editor-performance" or "--native-text-visual-qualification" or "--allow-native" or "--diagnostics-log") continue;
                if(Arguments[Index]=="--native-provider"){
                    if(NativeProvider is not null||++Index>=Arguments.Length)throw new ArgumentException("Invalid native provider argument");
                    NativeProvider=Arguments[Index];continue;
                }
                if(Arguments[Index]=="--project") {
                    if(ProjectArgument is not null || ++Index>=Arguments.Length) throw new ArgumentException("Invalid project argument");
                    ProjectArgument=Arguments[Index]; continue;
                }
                if(Arguments[Index]=="--qualification-report") {
                    if(QualificationReport is not null || ++Index>=Arguments.Length) throw new ArgumentException("Invalid qualification report");
                    QualificationReport=Arguments[Index]; continue;
                }
                if (Arguments[Index] == "--manifest")
                {
                    if (ManifestArgument is not null || ++Index >= Arguments.Length)
                        throw new ArgumentException("--manifest requires exactly one path");
                    ManifestArgument = Arguments[Index];
                }
                else if (ScriptArgument is null) ScriptArgument = Arguments[Index];
                else throw new ArgumentException("Unexpected additional application argument");
            }
            if (ManifestArgument is not null && (ScriptArgument is not null || Qualification))
                throw new ArgumentException("Manifest launch cannot combine with a script or native qualification");
            if(NativeTextVisualQualification){
                if(ProjectArgument is not null || ManifestArgument is not null || ScriptArgument is not null || Qualification || ConversationQualification || FrameQualification || OwnerWakeQualification || WindowLifecycleQualification || NativeTextHostQualification || CodeEditorQualification || CodeEditorObservation || CodeEditorPerformance || QualificationReport is null || NativeProvider is not null)
                    throw new ArgumentException("Visual text qualification requires only a report");
                if(!Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread().TryEnqueue(async ()=>{
                    Environment.ExitCode=await NativeTextVisualQualificationRunner.RunAsync(QualificationReport)?0:1;
                    ExitCleanly();
                })) throw new InvalidOperationException("Visual text dispatch unavailable");
                return;
            }
            if(CodeEditorQualification){
                if(ProjectArgument is not null || ManifestArgument is not null || ScriptArgument is not null || Qualification || ConversationQualification || FrameQualification || OwnerWakeQualification || WindowLifecycleQualification || NativeTextHostQualification || CodeEditorObservation || CodeEditorPerformance || QualificationReport is null || NativeProvider is not null)
                    throw new ArgumentException("Code editor qualification requires only a report");
                Backend=new WinUIBackend();
                Environment.ExitCode=await Backend.RunCodeEditorQualificationAsync(QualificationReport)?0:1;ExitCleanly();return;
            }
            if(CodeEditorObservation){
                if(ProjectArgument is not null || ManifestArgument is not null || ScriptArgument is not null || Qualification || ConversationQualification || FrameQualification || OwnerWakeQualification || WindowLifecycleQualification || NativeTextHostQualification || CodeEditorPerformance || QualificationReport is null || NativeProvider is not null)
                    throw new ArgumentException("Code editor observation requires only a report");
                Backend=new WinUIBackend();
                Environment.ExitCode=await Backend.RunCodeEditorObservationAsync(QualificationReport)?0:1;ExitCleanly();return;
            }
            if(CodeEditorPerformance){
                if(ProjectArgument is not null || ManifestArgument is not null || ScriptArgument is not null || Qualification || ConversationQualification || FrameQualification || OwnerWakeQualification || WindowLifecycleQualification || NativeTextHostQualification || CodeEditorQualification || CodeEditorObservation || QualificationReport is null || NativeProvider is not null)
                    throw new ArgumentException("Code editor performance requires only a report");
                Backend=new WinUIBackend();
                Environment.ExitCode=await Backend.RunCodeEditorPerformanceAsync(QualificationReport)?0:1;ExitCleanly();return;
            }
            if(NativeTextHostQualification){
                if(ProjectArgument is not null || ManifestArgument is not null || ScriptArgument is not null || Qualification || ConversationQualification || FrameQualification || OwnerWakeQualification || WindowLifecycleQualification || QualificationReport is null || NativeProvider is not null)
                    throw new ArgumentException("Native text host qualification requires only a report");
                Environment.ExitCode=await NativeTextHostQualificationRunner.RunAsync(QualificationReport)?0:1;ExitCleanly();return;
            }
            if(WindowLifecycleQualification){
                if(ProjectArgument is not null || ManifestArgument is not null || ScriptArgument is not null || Qualification || ConversationQualification || FrameQualification || OwnerWakeQualification || QualificationReport is null || NativeProvider is not null)
                    throw new ArgumentException("Window lifecycle qualification requires only a report");
                Backend=new WinUIBackend();
                Environment.ExitCode=await Backend.RunWindowLifecycleQualificationAsync(QualificationReport)?0:1;ExitCleanly();return;
            }
            if(OwnerWakeQualification){
                if(ProjectArgument is not null||ManifestArgument is not null||ScriptArgument is not null||Qualification||ConversationQualification||FrameQualification||QualificationReport is null||NativeProvider is null)
                    throw new ArgumentException("Owner wake qualification requires a provider and report");
                Backend=new WinUIBackend();Backend.AllWindowsClosed+=ExitCleanly;
                Environment.ExitCode=await Backend.RunOwnerWakeQualificationAsync(NativeProvider,QualificationReport)?0:1;ExitCleanly();return;
            }
            if(NativeProvider is not null)throw new ArgumentException("Native provider argument requires owner wake qualification");
            if (FrameQualification) {
                if (ProjectArgument is not null || ManifestArgument is not null || ScriptArgument is not null || Qualification || ConversationQualification || QualificationReport is null)
                    throw new ArgumentException("Frame qualification requires only a report destination");
                Backend=new WinUIBackend(); Backend.AllWindowsClosed+=ExitCleanly;
                PumpTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(16)};PumpTimer.Tick+=(_,_)=>{if(!Backend.PauseFrameQualificationPump)Backend.Pump();};PumpTimer.Start();
                Environment.ExitCode=await Backend.RunFrameQualificationAsync(QualificationReport,Arguments.Contains("--frame-observation"))?0:1;ExitCleanly();return;
            }
            if(ProjectArgument is not null) {
                if(ManifestArgument is not null || ScriptArgument is not null || Qualification) throw new ArgumentException("Invalid project launch");
                using PreparedProject Project=new(ProjectArgument,Path.Combine(AppContext.BaseDirectory,"LuiRuntime.dll"));
                Backend=new WinUIBackend(Project); Backend.AllWindowsClosed+=ExitCleanly;
                if(!Backend.RunProject(Project,Arguments.Contains("--allow-native"))) throw new InvalidOperationException("[LUI:App] StartupFailed");
                PumpTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(16)}; PumpTimer.Tick+=(_,_)=>Backend.Pump(); PumpTimer.Start();
                if(QualificationReport is not null && !ConversationQualification) throw new ArgumentException("Report requires conversation qualification");
                if(ConversationQualification) {Environment.ExitCode=await Backend.RunConversationQualificationAsync(QualificationReport)?0:1; ExitCleanly();}
                return;
            }
            if(ConversationQualification) throw new ArgumentException("Conversation qualification requires a project");
            Backend = new WinUIBackend(); Backend.AllWindowsClosed += ExitCleanly;
            AppManifest? Manifest = ManifestArgument is null ? null : AppManifest.Load(ManifestArgument);
            if (Manifest is not null) Backend.ConfigureManifest(Manifest);
            string ScriptPath = Manifest?.ScriptPath ?? (ScriptArgument is not null
                ? Path.GetFullPath(ScriptArgument)
                : Qualification
                    ? Path.Combine(AppContext.BaseDirectory, "tests", "winui", "NativeMapping.luau")
                    : Path.Combine(AppContext.BaseDirectory, "examples", "hello.luau"));
            LuiDiagnostics.Log("App", "Loading " + ScriptPath);
            if (!Backend.RunFile(ScriptPath))
            {
                LuiDiagnostics.Error("App", "Script failed: " + Backend.LastError);
                Environment.ExitCode = 1;
                ExitCleanly();
                return;
            }

            if (Qualification)
            {
                Environment.ExitCode = await Backend.RunNativeQualificationAsync() ? 0 : 1;
                ExitCleanly();
                return;
            }

            PumpTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
            PumpTimer.Tick += (_, _) => Backend.Pump();
            PumpTimer.Start();
            LuiDiagnostics.Log("App", "Ready");
        }
        catch (Exception Error)
        {
            LuiDiagnostics.Error("App", "Startup failed: " + Error);
            Environment.ExitCode = 1;
            ExitCleanly();
        }
    }
}
