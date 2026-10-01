using System.Diagnostics;
using Microsoft.UI.Xaml;

namespace Lui.WinUI;

public partial class App : Application
{
    private WinUIBackend? Backend;
    private DispatcherTimer? PumpTimer;

    public App()
    {
        string[] Arguments = Environment.GetCommandLineArgs();
        LuiDiagnostics.Initialize(Arguments.Skip(1).Any(Argument => Argument == "--diagnostics"),
            Arguments.Skip(1).Any(Argument => Argument == "--native-qualification"));
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
            string? ScriptArgument = Arguments.Skip(1).FirstOrDefault(Argument =>
                Argument != "--diagnostics" && Argument != "--native-qualification");
            string ScriptPath = ScriptArgument is not null
                ? Path.GetFullPath(ScriptArgument)
                : Qualification
                    ? Path.Combine(AppContext.BaseDirectory, "tests", "winui", "NativeMapping.luau")
                    : Path.Combine(AppContext.BaseDirectory, "examples", "hello.luau");
            LuiDiagnostics.Log("App", "Loading " + ScriptPath);
            if (!Backend.RunFile(ScriptPath))
            {
                LuiDiagnostics.Error("App", "Script failed: " + Backend.LastError);
                if (Qualification) Environment.ExitCode = 1;
                Exit();
                return;
            }

            if (Qualification)
            {
                Environment.ExitCode = await Backend.RunNativeQualificationAsync() ? 0 : 1;
                Exit();
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
            if (Qualification) Environment.ExitCode = 1;
            Exit();
        }
    }
}
