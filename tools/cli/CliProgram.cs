using System.ComponentModel;
using System.Diagnostics;
using System.Text;
using System.Text.Json;
using Lui.Packaging;
using Lui.WinUI;

namespace Lui.Cli;

internal static class CliProgram
{
    private const long MaxScriptBytes = 8 * 1024 * 1024;

    public static int Run(string[] Arguments, TextWriter Output, TextWriter Errors)
    {
        try
        {
            if (Arguments.Length == 0 || Arguments[0] is "help" or "--help" or "-h")
            {
                Output.WriteLine("LUI developer CLI\n" +
                    "  lui new <directory> [--template basic|commands|workbench]\n" +
                    "  lui doctor [manifest] [--sdk <SDK directory>] [--format text|json]\n" +
                    "  SDK commands discover matched tools and honor LuiToolchain.json; --sdk selects an installation.\n" +
                    "  lui check [manifest] --checker <LuiTypeCheck executable> [--definitions <LUI.d.luau>] [--format text|json]\n" +
                    "  lui check [manifest] --mode syntax --runtime <LuiRuntime library>\n" +
                    "  lui check [manifest v2] --mode project --diagnostics development --checker <LuiTypeCheck executable> [--format text|json]\n" +
                    "  lui run [manifest] --host <WinUI host directory or exe>\n" +
                    "  lui preview [manifest] --host <preview-host dll or exe> --runtime <LuiRuntime library>\n" +
                    "  lui build [manifest] --host <published WinUI host directory> --output <empty directory> [--extensions <directory>]\n" +
                    "  lui stage [manifest v2] --output <new directory> [--project-tool <LuiProjectTool executable>]\n" +
                    "  lui verify <staged directory> [--project-tool <LuiProjectTool executable>]\n" +
                    "  lui run [manifest v2] --mode project --host <WinUI or GTK host directory or executable>\n" +
                    "  lui preview [manifest v2] --mode project --host <preview host dll or exe> --runtime <library>\n" +
                    "  lui build [manifest v2] --mode project --host <published WinUI directory or installed GTK prefix> --project-tool <executable> --output <new directory>\n" +
                    "Default manifest: lui.json. Check analyzes entry-script types without running application code.");
                return 0;
            }

            string Command = Arguments[0];
            (string? Position, Dictionary<string, string> Options) = Parse(Arguments);
            string? SdkRoot = Options.GetValueOrDefault("--sdk");
            Options.Remove("--sdk");
            SdkInstallation? Sdk = SdkInstallation.Resolve(SdkRoot, Command == "new" ? null : Position ?? "lui.json", Command == "doctor");
            if (Command == "doctor")
            {
                RequireOptions(Options, "--format");
                string Format = Options.GetValueOrDefault("--format", "text");
                if (Format is not ("text" or "json")) throw new ArgumentException("Format must be text or json");
                Output.WriteLine(Format == "json" ? JsonSerializer.Serialize(Sdk!.Describe()) : $"[LUI:SDK] Ready: {Sdk!.Version} ({Sdk.Target}); local integrity verified, publisher authenticity is separate");
                return 0;
            }
            if (Sdk is not null && Command is "check" or "run" or "preview" or "build")
            {
                if (new FileInfo(Position ?? "lui.json").Length > 256 * 1024) throw new InvalidDataException("[LUI:Project] LimitExceeded");
                using JsonDocument Metadata = JsonDocument.Parse(File.ReadAllText(Position ?? "lui.json"));
                if (!Options.ContainsKey("--mode") && Metadata.RootElement.GetProperty("SchemaVersion").GetInt32() == 2)
                    Options["--mode"] = "project";
                if (Command == "check" && Options.GetValueOrDefault("--mode") == "project" && !Options.ContainsKey("--diagnostics"))
                {
                    if (Metadata.RootElement.GetProperty("Diagnostics").GetProperty("Mode").GetString() != "Development")
                        throw new InvalidDataException("[LUI:TypeCheck] PolicyNotSupported");
                    Options["--diagnostics"] = "development";
                }
            }
            Sdk?.Configure(Options, Command);
            switch (Command)
            {
                case "stage":
                case "verify":
                {
                    RequireOptions(Options, Command == "stage" ? ["--project-tool", "--output"] : ["--project-tool"]);
                    string Tool = Path.GetFullPath(Options.GetValueOrDefault("--project-tool") ??
                        Environment.GetEnvironmentVariable("LUI_PROJECT_TOOL") ?? Path.Combine(AppContext.BaseDirectory,
                            OperatingSystem.IsWindows() ? "LuiProjectTool.exe" : "LuiProjectTool"));
                    if (!File.Exists(Tool)) throw new FileNotFoundException("Project tool missing; pass --project-tool");
                    JsonElement Summary = Command == "stage"
                        ? ProjectPackager.Run(Tool, "--stage", Path.GetFullPath(Position ?? "lui.json"), Path.GetFullPath(Required(Options, "--output")))
                        : ProjectPackager.Run(Tool, "--verify", Path.GetFullPath(Position ?? throw new ArgumentException("verify requires a staged directory")));
                    Output.WriteLine(Summary.GetRawText()); return 0;
                }
                case "new":
                    RequireOptions(Options,"--template");
                    if (Position is null) throw new ArgumentException("new requires a new project directory");
                    string Template=Options.GetValueOrDefault("--template","basic");
                    if (Sdk is null) {
                        if(Template!="basic") throw new ArgumentException("Choose the SDK with --sdk to use optional templates");
                        CreateProject(Position);
                    }
                    else CreateSdkProject(Position, Sdk,Template);
                    Output.WriteLine("[LUI:CLI] Created project at " + Path.GetFullPath(Position));
                    return 0;
                case "check":
                {
                    RequireOptions(Options, "--runtime", "--checker", "--definitions", "--format", "--mode", "--diagnostics");
                    string Mode = Options.GetValueOrDefault("--mode", "types");
                    string Format = Options.GetValueOrDefault("--format", "text");
                    if (Mode is not ("types" or "syntax" or "project") || Format is not ("text" or "json"))
                        throw new ArgumentException("Check mode must be types, syntax or project; format must be text or json");
                    if (Mode == "project")
                    {
                        if (Options.ContainsKey("--runtime")) throw new ArgumentException("Project type checking uses --checker");
                        string ProjectChecker = Path.GetFullPath(Options.GetValueOrDefault("--checker") ?? Environment.GetEnvironmentVariable("LUI_TYPECHECK") ??
                            Path.Combine(AppContext.BaseDirectory, OperatingSystem.IsWindows() ? "LuiTypeCheck.exe" : "LuiTypeCheck"));
                        string ProjectDefinitions = Path.GetFullPath(Options.GetValueOrDefault("--definitions") ?? Path.Combine(Path.GetDirectoryName(ProjectChecker)!, "LUI.d.luau"));
                        if (!File.Exists(ProjectChecker) || !File.Exists(ProjectDefinitions)) throw new FileNotFoundException("Project checker or generated definitions missing");
                        JsonElement ProjectResult = ProjectTypeChecker.Check(ProjectChecker, ProjectDefinitions, Path.GetFullPath(Position ?? "lui.json"), Required(Options, "--diagnostics"));
                        JsonElement ProjectDiagnostics = ProjectResult.GetProperty("diagnostics");
                        if (Format == "json") Output.WriteLine(ProjectResult.GetRawText());
                        else
                        {
                            foreach (JsonElement Issue in ProjectDiagnostics.EnumerateArray())
                            {
                                string Location = "project";
                                if (Issue.GetProperty("source").ValueKind == JsonValueKind.String)
                                {
                                    JsonElement Start = Issue.GetProperty("range").GetProperty("start");
                                    Location = $"{Issue.GetProperty("source").GetString()}:{Start.GetProperty("line").GetInt32() + 1}:{Start.GetProperty("column").GetInt32() + 1}";
                                }
                                Errors.WriteLine($"[LUI:CLI] {Location}: {Issue.GetProperty("message").GetString()}");
                            }
                            if (ProjectResult.GetProperty("truncated").GetBoolean()) Errors.WriteLine("[LUI:CLI] Diagnostics were truncated");
                            if (ProjectDiagnostics.GetArrayLength() == 0) Output.WriteLine("[LUI:CLI] Declared project sources typecheck");
                        }
                        return ProjectDiagnostics.GetArrayLength() == 0 && !ProjectResult.GetProperty("truncated").GetBoolean() ? 0 : 1;
                    }
                    if (Options.ContainsKey("--diagnostics")) throw new ArgumentException("--diagnostics requires --mode project");
                    AppManifest Manifest = AppManifest.Load(Position ?? "lui.json");
                    if (Mode == "syntax")
                    {
                        if (Format != "text" || Options.ContainsKey("--checker") || Options.ContainsKey("--definitions"))
                            throw new ArgumentException("Syntax mode accepts only --runtime and text output");
                        CheckSource(Manifest, ResolveRuntime(Options.GetValueOrDefault("--runtime")));
                        Output.WriteLine("[LUI:CLI] Manifest and Luau syntax valid (types not checked)");
                        return 0;
                    }
                    if (Options.ContainsKey("--runtime"))
                        throw new ArgumentException("--runtime requires --mode syntax; type checks use --checker");
                    string Checker = Path.GetFullPath(Options.GetValueOrDefault("--checker") ??
                        Environment.GetEnvironmentVariable("LUI_TYPECHECK") ?? Path.Combine(AppContext.BaseDirectory,
                            OperatingSystem.IsWindows() ? "LuiTypeCheck.exe" : "LuiTypeCheck"));
                    if (!File.Exists(Checker)) throw new FileNotFoundException("Luau type checker not found; pass --checker or set LUI_TYPECHECK");
                    string Definitions = Path.GetFullPath(Options.GetValueOrDefault("--definitions") ??
                        Path.Combine(Path.GetDirectoryName(Checker)!, "LUI.d.luau"));
                    if (!File.Exists(Definitions)) throw new FileNotFoundException("Generated LUI definitions were not found; pass --definitions");
                    JsonElement Result = NativeTypeChecker.Check(Checker, Definitions, ReadSource(Manifest));
                    JsonElement Diagnostics = Result.GetProperty("diagnostics");
                    string SourceName = Manifest.Script.Replace('\\', '/');
                    if (Format == "json")
                        Output.WriteLine(JsonSerializer.Serialize(new Dictionary<string, object>
                        {
                            ["version"] = 1, ["source"] = SourceName, ["diagnostics"] = Diagnostics,
                            ["truncated"] = Result.GetProperty("truncated").GetBoolean(),
                        }));
                    else
                    {
                        foreach (JsonElement Diagnostic in Diagnostics.EnumerateArray())
                        {
                            JsonElement Start = Diagnostic.GetProperty("range").GetProperty("start");
                            Errors.WriteLine($"[LUI:CLI] {SourceName}:{Start.GetProperty("line").GetInt32() + 1}:{Start.GetProperty("column").GetInt32() + 1}: " +
                                Diagnostic.GetProperty("message").GetString());
                        }
                        if (Result.GetProperty("truncated").GetBoolean()) Errors.WriteLine("[LUI:CLI] Diagnostics were truncated");
                        if (Diagnostics.GetArrayLength() == 0) Output.WriteLine("[LUI:CLI] Manifest and Luau types valid");
                    }
                    return Diagnostics.GetArrayLength() == 0 ? 0 : 1;
                }
                case "run":
                {
                    RequireOptions(Options, "--host", "--mode", "--allow-native");
                    if (!OperatingSystem.IsWindows() && !OperatingSystem.IsLinux())
                        throw new PlatformNotSupportedException("Native launch requires Windows or Linux");
                    if(OperatingSystem.IsLinux() && Options.GetValueOrDefault("--mode")!="project")
                        throw new InvalidDataException("[LUI:CLI] Linux launch requires --mode project");
                    string Host = ResolveHost(Required(Options, "--host"));
                    if(Options.GetValueOrDefault("--mode")=="project") {
                        ProcessStartInfo Start=new(Host){UseShellExecute=false,WorkingDirectory=Path.GetDirectoryName(Path.GetFullPath(Position??"lui.json"))!};
                        Start.ArgumentList.Add("--project"); Start.ArgumentList.Add(Path.GetFullPath(Position??"lui.json"));
                        if(Options.GetValueOrDefault("--allow-native")=="true") Start.ArgumentList.Add("--allow-native");
                        return RunNative(Start, Errors);
                    }
                    AppManifest Manifest = AppManifest.Load(Position ?? "lui.json");
                    CheckSource(Manifest, Path.Combine(Path.GetDirectoryName(Host)!, "LuiRuntime.dll"));
                    return RunNative(CreateRunStartInfo(Host, Position ?? "lui.json"), Errors);
                }
                case "preview":
                {
                    RequireOptions(Options, "--host", "--runtime", "--mode");
                    string ManifestPath = Path.GetFullPath(Position ?? "lui.json");
                    bool Modular=Options.GetValueOrDefault("--mode")=="project";
                    if(!Modular) AppManifest.Load(ManifestPath);
                    string Runtime = ResolveRuntime(Options.GetValueOrDefault("--runtime"));
                    string Host = Path.GetFullPath(Required(Options, "--host"));
                    if (!File.Exists(Host) || !(Host.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) ||
                        Host.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)))
                        throw new FileNotFoundException("Preview host DLL or executable was not found", Host);
                    ProcessStartInfo Start = new(Host.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) ? "dotnet" : Host)
                    {
                        UseShellExecute = false,
                        WorkingDirectory = Path.GetDirectoryName(ManifestPath)!,
                    };
                    if (Host.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)) Start.ArgumentList.Add(Host);
                    Start.ArgumentList.Add(Modular?"--project":"--manifest"); Start.ArgumentList.Add(ManifestPath);
                    Start.ArgumentList.Add("--runtime"); Start.ArgumentList.Add(Runtime);
                    using Process Child = Process.Start(Start) ?? throw new IOException("Unable to start the preview host");
                    Child.WaitForExit();
                    return Child.ExitCode;
                }
                case "build":
                {
                    RequireOptions(Options, "--host", "--output", "--extensions", "--mode", "--project-tool");
                    if (!OperatingSystem.IsWindows() && !OperatingSystem.IsLinux())
                        throw new PlatformNotSupportedException("Packaging requires Windows or Linux");
                    if(OperatingSystem.IsLinux() && Options.GetValueOrDefault("--mode")!="project")
                        throw new InvalidDataException("[LUI:CLI] Linux packaging requires --mode project");
                    string ManifestPath = Path.GetFullPath(Position ?? "lui.json");
                    if(Options.GetValueOrDefault("--mode")=="project") {
                        string ProjectHost=ResolveHost(Required(Options,"--host"));
                        string Tool=Path.GetFullPath(Required(Options,"--project-tool"));
                        string ProjectHostDirectory=Path.GetDirectoryName(ProjectHost)!;
                        if(OperatingSystem.IsLinux()) {
                            if(Path.GetFileName(ProjectHostDirectory)!="bin") throw new InvalidDataException("[LUI:Package] InstalledHostRequired");
                            ProjectHostDirectory=Path.GetDirectoryName(ProjectHostDirectory)!;
                        }
                        PackageBuilder.StageProject(ManifestPath,ProjectHostDirectory,Required(Options,"--output"),Tool); return 0;
                    }
                    AppManifest Manifest = AppManifest.Load(ManifestPath);
                    string Host = ResolveHost(Required(Options, "--host"));
                    string HostDirectory = Path.GetDirectoryName(Host)!;
                    CheckSource(Manifest, Path.Combine(HostDirectory, "LuiRuntime.dll"));
                    PackageBuilder.Stage(ManifestPath, HostDirectory, Required(Options, "--output"),
                        Options.GetValueOrDefault("--extensions"));
                    return 0;
                }
                default:
                    throw new ArgumentException("Unknown command: " + Command);
            }
        }
        catch (Exception Error) when (Error is ArgumentException or InvalidDataException or IOException or
            UnauthorizedAccessException or DllNotFoundException or EntryPointNotFoundException or
            BadImageFormatException or PlatformNotSupportedException or JsonException or Win32Exception or InvalidOperationException or KeyNotFoundException or FormatException)
        {
            Errors.WriteLine("[LUI:CLI] " + ApplicationDiagnostic.Describe(Error.Message));
            return 1;
        }
    }

    private static void CreateSdkProject(string DirectoryValue, SdkInstallation Sdk,string Template="basic")
    {
        string Root = Path.GetFullPath(DirectoryValue);
        string Name = Path.GetFileName(Path.TrimEndingDirectorySeparator(Root));
        if (Name.Length is < 1 or > 64 || !char.IsAsciiLetter(Name[0]) || Name.Any(Character => !char.IsAsciiLetterOrDigit(Character) && Character is not ('_' or '-')))
            throw new ArgumentException("Project name must start with a letter and use letters, digits, _ or -");
        if (Directory.Exists(Root)) throw new IOException("Choose a new project directory; existing directories are preserved");
        if(Template!="basic")
        {
            Sdk.CopyExample(Template,Root);
            File.WriteAllText(Path.Combine(Root,".gitignore"),"Dist/\n.lui/\n");
            File.WriteAllText(Path.Combine(Root,"README.md"),$"# {Name}\n\nRun `lui check`, then `lui run`. Edit Src/Main.luau, save, close and run again.\n\nThis {Template} example uses optional vendored libraries. Keep Modules.lock.json and LuiToolchain.json with the project.\n\nStorage, when requested, belongs to this project directory. Packaged and copied projects use separate data.\n");
            return;
        }
        Directory.CreateDirectory(Path.Combine(Root, "Src"));
        File.WriteAllText(Path.Combine(Root, "lui.json"), JsonSerializer.Serialize(new
        {
            SchemaVersion = 2, Script = "Src/Main.luau", Capabilities = Array.Empty<string>(), Extensions = Array.Empty<string>(),
            Assets = Array.Empty<string>(), ExtensionDeclarations = Array.Empty<string>(), Diagnostics = new { Mode = "Development" },
            Modules = new { Root = "Src", Files = new[] { "Main.luau" }, Requires = Array.Empty<string>(), Dependencies = Array.Empty<object>() },
        }, new JsonSerializerOptions { WriteIndented = true }) + "\n");
        File.WriteAllText(Path.Combine(Root, "Src", "Main.luau"), """
            local Window = Instance.new("Window", {
                Title = "MyApp",
                Size = UDim2.fromOffset(480, 240),
            })
            local Greeting = Instance.new("TextLabel", {
                Text = "Hello from LUI",
                Size = UDim2.fromOffset(440, 80),
                Parent = Window,
            })
            local Button = Instance.new("TextButton", {
                Text = "Click me",
                Position = UDim2.fromOffset(20, 100),
                Size = UDim2.fromOffset(160, 44),
                Parent = Window,
            })
            Button.Activated:Connect(function()
                Greeting.Text = "You made it work!"
            end)
            Window.Visible = true
            """.Replace("Title = \"MyApp\"", "Title = \"" + Name + "\"") + "\n");
        Sdk.WritePin(Root);
        File.WriteAllText(Path.Combine(Root, ".gitignore"), "Dist/\n.lui/\n");
        File.WriteAllText(Path.Combine(Root, "README.md"), $"# {Name}\n\nRun `lui check`, then `lui run` from this folder. In VS Code, use LUI: Run App.\n\nEdit Src/Main.luau, save, stop the app and run again. Clicking the button changes the greeting.\n\nUse `lui build --output Dist/{Name}` for an app folder. Keep LuiToolchain.json in version control.\n\nNew module files must be declared in lui.json. Automatic native restart and application-authored test commands are not available in this preview.\n");
    }

    private static (string? Position, Dictionary<string, string> Options) Parse(string[] Arguments)
    {
        string? Position = null;
        Dictionary<string, string> Options = new(StringComparer.Ordinal);
        for (int Index = 1; Index < Arguments.Length; ++Index)
        {
            string Argument = Arguments[Index];
            if (Argument.StartsWith("--", StringComparison.Ordinal))
            {
                if (Index + 1 >= Arguments.Length || Arguments[Index + 1].StartsWith("--", StringComparison.Ordinal) ||
                    !Options.TryAdd(Argument, Arguments[++Index]))
                    throw new ArgumentException("Option requires one value and cannot repeat: " + Argument);
            }
            else if (Position is null) Position = Argument;
            else throw new ArgumentException("Unexpected positional argument: " + Argument);
        }
        return (Position, Options);
    }

    private static void RequireOptions(Dictionary<string, string> Options, params string[] Allowed)
    {
        foreach (string Option in Options.Keys)
            if (!Allowed.Contains(Option, StringComparer.Ordinal))
                throw new ArgumentException("Unknown option: " + Option);
    }

    private static string Required(Dictionary<string, string> Options, string Name)
    {
        if (!Options.TryGetValue(Name, out string? Value) || string.IsNullOrWhiteSpace(Value))
            throw new ArgumentException("Required option is missing: " + Name);
        return Value;
    }

    private static void CreateProject(string DirectoryValue)
    {
        string DirectoryPath = Path.GetFullPath(DirectoryValue);
        string Name = Path.GetFileName(Path.TrimEndingDirectorySeparator(DirectoryPath));
        if (Name.Length is < 1 or > 64 || !char.IsAsciiLetter(Name[0]) ||
            Name.Any(Character => !char.IsAsciiLetterOrDigit(Character) && Character is not ('_' or '-')))
            throw new ArgumentException("Project directory name must start with a letter and contain only letters, digits, _ or -");
        if (Directory.Exists(DirectoryPath) && Directory.EnumerateFileSystemEntries(DirectoryPath).Any())
            throw new IOException("Project directory must be empty");
        Directory.CreateDirectory(Path.Combine(DirectoryPath, "src"));
        string ManifestJson = JsonSerializer.Serialize(new
        {
            SchemaVersion = 1,
            Script = "src/main.luau",
            Capabilities = Array.Empty<string>(),
            Extensions = Array.Empty<string>(),
            Assets = Array.Empty<string>(),
        }, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(Path.Combine(DirectoryPath, "lui.json"), ManifestJson + "\n", new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(DirectoryPath, "src", "main.luau"),
            "local Window = Instance.new(\"Window\", {\n" +
            "    Title = \"" + Name + "\",\n" +
            "    Size = UDim2.fromOffset(800, 500),\n" +
            "})\n\n" +
            "local Greeting = Instance.new(\"TextLabel\", {\n" +
            "    Text = \"Hello from LUI\",\n" +
            "    Size = UDim2.fromScale(1, 1),\n" +
            "    Parent = Window,\n" +
            "})\n\n" +
            "Window.Visible = true\n", new UTF8Encoding(false));
    }

    private static string ResolveRuntime(string? Value)
    {
        string? Candidate = Value ?? Environment.GetEnvironmentVariable("LUI_RUNTIME");
        Candidate ??= Path.Combine(AppContext.BaseDirectory,
            OperatingSystem.IsWindows() ? "LuiRuntime.dll" : "libLuiRuntime.so");
        string RuntimePath = Path.GetFullPath(Candidate);
        if (!File.Exists(RuntimePath))
            throw new FileNotFoundException("Native runtime not found; pass --runtime or set LUI_RUNTIME", RuntimePath);
        return RuntimePath;
    }

    private static string ResolveHost(string Value)
    {
        string Host = Path.GetFullPath(Value);
        if(OperatingSystem.IsLinux()) {
            if(Directory.Exists(Host)) Host=File.Exists(Path.Combine(Host,"lui-gtk"))?Path.Combine(Host,"lui-gtk"):Path.Combine(Host,"bin","lui-gtk");
            if(Path.GetFileName(Host)!="lui-gtk" || !File.Exists(Host)) throw new FileNotFoundException("[LUI:CLI] GtkHostUnavailable");
            return Host;
        }
        if (Directory.Exists(Host)) Host = Path.Combine(Host, "Lui.WinUI.exe");
        if (!Path.GetFileName(Host).Equals("Lui.WinUI.exe", StringComparison.OrdinalIgnoreCase) ||
            !File.Exists(Host) || !File.Exists(Path.Combine(Path.GetDirectoryName(Host)!, "LuiRuntime.dll")))
            throw new FileNotFoundException("WinUI host with LuiRuntime.dll was not found", Host);
        return Host;
    }

    private static void CheckSource(AppManifest Manifest, string RuntimePath)
    {
        string Runtime = ResolveRuntime(RuntimePath);
        string Source = ReadSource(Manifest);
        string? Error = NativeScriptChecker.Check(Runtime, Source);
        if (Error is not null) throw new InvalidDataException(Error);
    }

    private static string ReadSource(AppManifest Manifest)
    {
        using FileStream File = System.IO.File.OpenRead(Manifest.ScriptPath);
        using MemoryStream Bytes = new();
        byte[] Block = new byte[8192];
        int Count;
        while ((Count = File.Read(Block)) != 0)
        {
            if (Bytes.Length + Count > MaxScriptBytes)
                throw new InvalidDataException("Application script exceeds the CLI's 8 MiB check limit");
            Bytes.Write(Block, 0, Count);
        }
        string Source = new UTF8Encoding(false, true).GetString(Bytes.ToArray());
        if (Source.StartsWith('\uFEFF')) Source = Source[1..];
        if (Source.Contains('\0')) throw new InvalidDataException("Application script contains a NUL character");
        return Source;
    }

    internal static int RunNative(ProcessStartInfo Start, TextWriter Errors)
    {
        Start.RedirectStandardError = true;
        Start.StandardErrorEncoding = new UTF8Encoding(false, false);
        using Process Child = Process.Start(Start) ?? throw new IOException("[LUI:CLI] LaunchFailed");
        using CancellationTokenSource Stop = new();
        bool StartupReported = false;
        async Task DrainAsync()
        {
            StringBuilder Pending = new();
            char[] Buffer = new char[4096];
            bool Oversized = false;
            int Records = 0, Count;
            try {
                while ((Count = await Child.StandardError.ReadAsync(Buffer.AsMemory(), Stop.Token)) != 0) {
                    if (Records >= 256) continue;
                    for (int Index = 0; Index < Count; ++Index) {
                        if (Stop.IsCancellationRequested) return;
                        char Value = Buffer[Index];
                        if (Value == '\n') {
                            if (!Oversized && Records < 256 && ApplicationDiagnostic.TryReadRecord(Pending.ToString().TrimEnd('\r'), out string Message, out bool Startup)) {
                                ++Records; StartupReported |= Startup; Errors.WriteLine(Message);
                            }
                            Pending.Clear(); Oversized = false;
                        } else if (Pending.Length < 1025) Pending.Append(Value);
                        else Oversized = true;
                    }
                }
            } catch (Exception Error) when (Error is OperationCanceledException or ObjectDisposedException or IOException) { }
        }
        Task Drain = DrainAsync();
        Child.WaitForExit();
        try { Drain.WaitAsync(TimeSpan.FromSeconds(1)).GetAwaiter().GetResult(); }
        catch (TimeoutException) { Stop.Cancel(); }
        if (Child.ExitCode != 0 && !StartupReported)
            Errors.WriteLine("[LUI:CLI] App stopped without a startup reason. Run Check Types, check the SDK with lui doctor, and include the SDK version and reproduction steps in a bug report.");
        return Child.ExitCode;
    }

    internal static ProcessStartInfo CreateRunStartInfo(string Host, string ManifestPath)
    {
        ProcessStartInfo Start = new(Host)
        {
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(Path.GetFullPath(ManifestPath))!,
        };
        Start.ArgumentList.Add("--manifest");
        Start.ArgumentList.Add(Path.GetFullPath(ManifestPath));
        return Start;
    }
}
