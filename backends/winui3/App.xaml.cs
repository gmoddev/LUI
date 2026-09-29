using System.Diagnostics;
using Microsoft.UI.Xaml;

namespace Lui.WinUI;

public partial class App : Application
{
    private WinUIBackend? Backend;
    private DispatcherTimer? PumpTimer;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs Args)
    {
        try
        {
            Backend = new WinUIBackend();
            string[] Arguments = Environment.GetCommandLineArgs();
            string ScriptPath = Arguments.Length > 1
                ? Path.GetFullPath(Arguments[1])
                : Path.Combine(AppContext.BaseDirectory, "examples", "hello.luau");
            if (!Backend.RunFile(ScriptPath))
            {
                Trace.TraceError("[LUI:WinUI] Script failed: {0}", Backend.LastError);
                Exit();
                return;
            }

            PumpTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
            PumpTimer.Tick += (_, _) => Backend.Pump();
            PumpTimer.Start();
        }
        catch (Exception Error)
        {
            Trace.TraceError("[LUI:WinUI] Startup failed: {0}", Error);
            Exit();
        }
    }
}
