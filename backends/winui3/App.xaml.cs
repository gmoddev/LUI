using System.Diagnostics;
using Microsoft.UI.Xaml;

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
        LuiDiagnostics.Initialize(Arguments.Skip(1).Any(Argument => Argument == "--diagnostics"),
            Arguments.Skip(1).Any(Argument => Argument == "--native-qualification" || Argument == "--manifest"));
        UnhandledException += (_, Args) => LuiDiagnostics.Error("Xaml", Args.Exception.ToString());
        InitializeComponent();
    }

    protected override async void OnLaunched(LaunchActivatedEventArgs Args)
    {
        string[] Arguments = Environment.GetCommandLineArgs();
        bool Qualification = Arguments.Skip(1).Any(Argument => Argument == "--native-qualification");
        try
        {
            Backend = new WinUIBackend();
            Backend.AllWindowsClosed += ExitCleanly;
            string? ManifestArgument = null;
            string? ScriptArgument = null;
            for (int Index = 1; Index < Arguments.Length; ++Index)
            {
                if (Arguments[Index] is "--diagnostics" or "--native-qualification") continue;
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
