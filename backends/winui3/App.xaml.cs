using System.Diagnostics;
using Microsoft.UI.Xaml;

namespace Lui.WinUI;

public partial class App : Application
{
    private WinUIBackend? Backend;
    private DispatcherTimer? PumpTimer;

    public App()
    {
        LuiDiagnostics.Initialize(Environment.GetCommandLineArgs().Skip(1).Any(Argument => Argument == "--diagnostics"));
        UnhandledException += (_, Args) => LuiDiagnostics.Error("Xaml", Args.Exception.ToString());
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs Args)
    {
        string[] Arguments = Environment.GetCommandLineArgs();
        try
        {
            Backend = new WinUIBackend();
            string? ScriptArgument = Arguments.Skip(1).FirstOrDefault(Argument => Argument != "--diagnostics");
            string ScriptPath = ScriptArgument is not null
                ? Path.GetFullPath(ScriptArgument)
                : Path.Combine(AppContext.BaseDirectory, "examples", "hello.luau");
            LuiDiagnostics.Log("App", "Loading " + ScriptPath);
            if (!Backend.RunFile(ScriptPath))
            {
                LuiDiagnostics.Error("App", "Script failed: " + Backend.LastError);
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
            Exit();
        }
    }
}
