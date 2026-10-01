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
                    "  lui new <directory>\n" +
                    "  lui check [manifest] --runtime <LuiRuntime library>\n" +
                    "  lui run [manifest] --host <WinUI host directory or exe>\n" +
                    "  lui build [manifest] --host <published WinUI host directory> --output <empty directory> [--extensions <directory>]\n" +
                    "Default manifest: lui.json. Check compiles syntax without running the script; it does not typecheck yet.");
                return 0;
            }

            string Command = Arguments[0];
            (string? Position, Dictionary<string, string> Options) = Parse(Arguments);
            switch (Command)
            {
                case "new":
                    if (Position is null || Options.Count != 0)
                        throw new ArgumentException("new requires one directory and no options");
                    CreateProject(Position);
                    Output.WriteLine("[LUI:CLI] Created project at " + Path.GetFullPath(Position));
                    return 0;
                case "check":
                {
                    RequireOptions(Options, "--runtime");
                    AppManifest Manifest = AppManifest.Load(Position ?? "lui.json");
                    string Runtime = ResolveRuntime(Options.GetValueOrDefault("--runtime"));
                    CheckSource(Manifest, Runtime);
                    Output.WriteLine("[LUI:CLI] Manifest and Luau syntax valid (types not checked)");
                    return 0;
                }
                case "run":
                {
                    RequireOptions(Options, "--host");
                    if (!OperatingSystem.IsWindows())
                        throw new PlatformNotSupportedException("The native UI runner currently requires Windows");
                    AppManifest Manifest = AppManifest.Load(Position ?? "lui.json");
                    string Host = ResolveHost(Required(Options, "--host"));
                    CheckSource(Manifest, Path.Combine(Path.GetDirectoryName(Host)!, "LuiRuntime.dll"));
                    using Process Child = Process.Start(CreateRunStartInfo(Host, Position ?? "lui.json"))
                        ?? throw new IOException("Unable to start the WinUI host");
                    Child.WaitForExit();
                    return Child.ExitCode;
                }
                case "build":
                {
                    RequireOptions(Options, "--host", "--output", "--extensions");
                    if (!OperatingSystem.IsWindows())
                        throw new PlatformNotSupportedException("Windows packaging currently requires Windows");
                    string ManifestPath = Path.GetFullPath(Position ?? "lui.json");
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
            BadImageFormatException or PlatformNotSupportedException or JsonException or Win32Exception)
        {
            Errors.WriteLine("[LUI:CLI] " + Error.Message);
            return 1;
        }
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
        if (Directory.Exists(Host)) Host = Path.Combine(Host, "Lui.WinUI.exe");
        if (!Path.GetFileName(Host).Equals("Lui.WinUI.exe", StringComparison.OrdinalIgnoreCase) ||
            !File.Exists(Host) || !File.Exists(Path.Combine(Path.GetDirectoryName(Host)!, "LuiRuntime.dll")))
            throw new FileNotFoundException("WinUI host with LuiRuntime.dll was not found", Host);
        return Host;
    }

    private static void CheckSource(AppManifest Manifest, string RuntimePath)
    {
        string Runtime = ResolveRuntime(RuntimePath);
        FileInfo Script = new(Manifest.ScriptPath);
        if (Script.Length > MaxScriptBytes)
            throw new InvalidDataException("Application script exceeds the CLI's 8 MiB check limit");
        string Source = File.ReadAllText(Manifest.ScriptPath, new UTF8Encoding(false, true));
        if (Source.Contains('\0'))
            throw new InvalidDataException("Application script contains a NUL character");
        string? Error = NativeScriptChecker.Check(Runtime, Source);
        if (Error is not null) throw new InvalidDataException(Error);
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
