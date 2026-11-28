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
                    "  lui new <directory> [--template basic|commands|workbench|spec-cli|parallel-data|ffi-platform|ffi-sqlite|ffi-callback|ffi-workbench|ffi-isolated] [--storage-id <application-id>]\n" +
                    "  lui package init <directory> --name <Alias> --version <major.minor.patch> [--license <identifier>]\n" +
                    "  lui package pack <directory> --output <archive.luipkg>\n" +
                    "  lui package verify <directory-or-archive>\n" +
                    "  lui package add|remove <alias> [--manifest <manifest>] [--version <requirement>] [--optional true|--platform <target>]\n" +
                    "  lui package restore|update [manifest] [--sdk <SDK directory>] [--source <catalog directory>] [--locked true] [--include <aliases>]\n" +
                    "  lui package inspect [manifest] [--include <aliases>] [--lock <name.lock.json>]\n" +
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
                    "  lui run [manifest v2] --mode project --host <WinUI or GTK host directory or executable> [--allow-native true]\n" +
                    "  lui run [manifest v2] --mode headless [--grant network.client,network.server] [--http-origin <origin>|--http-public-only true] [--trusted-ffi true [--trusted-ffi-workers true]] [--stdio true [-- application arguments]]\n" +
                    "  lui preview [manifest v2] --mode project --host <preview host dll or exe> --runtime <library>\n" +
                    "  lui build [manifest v2] --mode headless --sdk <candidate SDK> --output <new directory>\n" +
                    "  lui build [manifest v2] --mode project --host <published WinUI directory or installed GTK prefix> --project-tool <executable> --output <new directory>\n" +
                    "Default manifest: lui.json. Check analyzes entry-script types without running application code.");
                return 0;
            }

            string Command = Arguments[0];
            if (Command == "package") return RunPackage(Arguments[1..], Output);
            int Separator = Command == "run" ? Array.IndexOf(Arguments, "--", 1) : -1;
            (string? Position, Dictionary<string, string> Options) = Parse(Separator < 0 ? Arguments : Arguments[..Separator]);
            bool Streams = Options.GetValueOrDefault("--stdio") == "true";
            if ((Options.ContainsKey("--stdio") || Separator >= 0) &&
                (Command != "run" || Options.GetValueOrDefault("--mode") != "headless" || !Streams))
                throw new ArgumentException("Application streams and arguments require --mode headless --stdio true");
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
                    RequireOptions(Options, "--template", "--storage-id");
                    if (Position is null) throw new ArgumentException("new requires a new project directory");
                    string Template=Options.GetValueOrDefault("--template","basic");
                    string? StorageId = Options.GetValueOrDefault("--storage-id");
                    if (StorageId is not null) {
                        if (Sdk is null || Template != "basic") throw new ArgumentException("--storage-id requires the basic SDK template");
                        if (StorageId.Length is < 1 or > 64 || StorageId[0] is < 'a' or > 'z' ||
                            StorageId.Any(Byte => Byte is not (>= 'a' and <= 'z' or >= '0' and <= '9' or '.' or '_' or '-')))
                            throw new ArgumentException("Storage ID must start with a lowercase ASCII letter and contain up to 64 lowercase letters, digits, dots, underscores or hyphens");
                    }
                    if (Sdk is null) {
                        if(Template!="basic") throw new ArgumentException("Choose the SDK with --sdk to use optional templates");
                        CreateProject(Position);
                    }
                    else CreateSdkProject(Position, Sdk, Template, StorageId);
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
                    RequireOptions(Options, "--host", "--mode", "--allow-native", "--grant", "--http-origin", "--http-public-only", "--diagnostics", "--timeout-ms", "--poll-ms", "--stdio", "--read-file", "--write-file", "--read-directory", "--write-directory", "--database-root", "--storage-root", "--providers", "--trusted-ffi", "--trusted-ffi-workers", "--tool", "--tool-cwd", "--context", "--env-keys", "--child-env-keys");
                    if (Options.GetValueOrDefault("--mode") == "headless")
                    {
                        if (Options.ContainsKey("--allow-native")) throw new ArgumentException("--allow-native applies to desktop launches. Use --providers or --trusted-ffi for headless execution.");
                        var Start = CreateHeadlessStartInfo(Required(Options,"--host"), Path.GetFullPath(Position ?? "lui.json"), Options);
                        if (Streams) {
                            Start.ArgumentList.Add("--stdio");
                            if (Separator >= 0) {
                                Start.ArgumentList.Add("--");
                                foreach (var Argument in Arguments[(Separator + 1)..]) Start.ArgumentList.Add(Argument);
                            }
                            if (OperatingSystem.IsWindows()) return WindowsStreams.Run(Start);
                            return UnixStreams.Run(Start);
                        }
                        return RunNative(Start, Errors, Headless: true);
                    }
                    if (Options.Keys.Any(Key => Key is "--grant" or "--http-origin" or "--http-public-only" or "--diagnostics" or "--timeout-ms" or "--poll-ms" or "--read-file" or "--write-file" or "--read-directory" or "--write-directory" or "--database-root" or "--storage-root" or "--providers" or "--trusted-ffi" or "--trusted-ffi-workers" or "--tool" or "--tool-cwd" or "--context" or "--env-keys" or "--child-env-keys"))
                        throw new ArgumentException("Headless options require --mode headless");
                    if (!OperatingSystem.IsWindows() && !OperatingSystem.IsLinux())
                        throw new PlatformNotSupportedException("Native launch requires Windows or Linux");
                    bool HeadlessBuild=Options.GetValueOrDefault("--mode")=="headless";
                    if(OperatingSystem.IsLinux() && Options.GetValueOrDefault("--mode")!="project" && !HeadlessBuild)
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
                    bool AllowNative=Options.GetValueOrDefault("--allow-native")=="true";
                    if (Manifest.AllowNativeExtensions && !AllowNative) throw new InvalidDataException("[LUI:Project] CapabilityDenied");
                    return RunNative(CreateRunStartInfo(Host, Position ?? "lui.json",AllowNative), Errors);
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
                        Host.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ||
                        (OperatingSystem.IsLinux() && Path.GetFileName(Host)=="lui-preview-host")))
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
                    bool HeadlessBuild=Options.GetValueOrDefault("--mode")=="headless";
                    if(OperatingSystem.IsLinux() && Options.GetValueOrDefault("--mode")!="project" && !HeadlessBuild)
                        throw new InvalidDataException("[LUI:CLI] Linux packaging requires project or headless mode");
                    string ManifestPath = Path.GetFullPath(Position ?? "lui.json");
                    if(Options.GetValueOrDefault("--mode")=="project" || HeadlessBuild) {
                        string ProjectHost=HeadlessBuild?Path.GetFullPath(Required(Options,"--host")):ResolveHost(Required(Options,"--host"));
                        string Tool=Path.GetFullPath(Required(Options,"--project-tool"));
                        string ProjectHostDirectory=Path.GetDirectoryName(ProjectHost)!;
                        if(OperatingSystem.IsLinux()) {
                            if(Path.GetFileName(ProjectHostDirectory)!="bin") throw new InvalidDataException("[LUI:Package] InstalledHostRequired");
                            ProjectHostDirectory=Path.GetDirectoryName(ProjectHostDirectory)!;
                        }
                        PackageBuilder.StageProject(ManifestPath,ProjectHostDirectory,Required(Options,"--output"),Tool,HeadlessBuild); return 0;
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

    private static int RunPackage(string[] Arguments, TextWriter Output)
    {
        if (Arguments.Length == 0 || Arguments[0] is "help" or "--help" or "-h") {
            Output.WriteLine("LUI packages\n" +
                "  init <directory> --name <alias> --version <version> [--license <identifier>]\n" +
                "  add <alias> [--version <requirement>] [--optional true|--platform <LinuxX64|WindowsX64>]\n" +
                "  remove <alias> [--optional true|--platform <LinuxX64|WindowsX64>]\n" +
                "  pack <directory> --output <archive.luipkg>\n" +
                "  verify <directory-or-archive>\n" +
                "  fetch <https-url> --sha256 <archive SHA-256> --source <catalog>\n" +
                "  import-git <repository> --revision <full commit ID> --git <executable> --source <catalog> [--subdir <package root>]\n" +
                "  restore [manifest] [--source <catalog>] [--locked true] [--include <comma-separated aliases>]\n" +
                "  update [manifest] --source <catalog> [--replace-output true]\n" +
                "  inspect [manifest] [--include <comma-separated aliases>] [--lock <name.lock.json>]\n" +
                "Use --manifest with add or remove to select a project. Use --lock and --output to keep distinct resolved profiles.");
            return 0;
        }
        (string? Position, Dictionary<string, string> Options) = Parse(Arguments);
        bool Flag(string Name)
        {
            if (!Options.TryGetValue(Name, out string? Value)) return false;
            if (Value != "true") throw new ArgumentException(Name + " accepts true");
            return true;
        }
        string RequestsPath(string Manifest) => Options.TryGetValue("--requests", out var RequestPath) ? Path.GetFullPath(RequestPath) :
            Path.Combine(Path.GetDirectoryName(Manifest)!, PackageRequests.FileName);
        switch (Arguments[0])
        {
            case "init":
            {
                RequireOptions(Options, "--name", "--version", "--sdk-version", "--license");
                string Root = Path.GetFullPath(Position ?? throw new ArgumentException("package init requires a directory"));
                if (Path.Exists(Root)) throw PackageSource.Invalid("PackageOutputExists");
                string Name = PackageSource.Alias(Required(Options, "--name"));
                string Version = PackageSource.ParseVersion(Required(Options, "--version")).ToString(3);
                string Sdk = PackageSource.SdkVersion(Options.GetValueOrDefault("--sdk-version", "*"));
                string License = Options.GetValueOrDefault("--license", "UNLICENSED");
                if (License.Length is 0 or > 128)
                    throw PackageSource.Invalid("InvalidPackageMetadata");
                PackageSource.WriteNew(Path.Combine(Root, PackageSource.Descriptor), PackageSource.Encode(new
                {
                    SchemaVersion = 1, Name, Version, Sdk, License,
                    Modules = new[] { "Main.luau" }, Dependencies = new Dictionary<string, string>(),
                }));
                PackageSource.WriteNew(Path.Combine(Root, "Main.luau"), Encoding.UTF8.GetBytes("return table.freeze({})\n"));
                Output.WriteLine("[LUI:Packages] Created " + Name);
                break;
            }
            case "add":
            case "remove":
            {
                bool Add = Arguments[0] == "add";
                RequireOptions(Options, Add ? ["--manifest", "--requests", "--version", "--optional", "--platform"] :
                    ["--manifest", "--requests", "--optional", "--platform"]);
                string Name = Position ?? throw new ArgumentException("Specify a package alias");
                string Manifest = Path.GetFullPath(Options.GetValueOrDefault("--manifest", "lui.json"));
                PackageRequests.Edit(Manifest, RequestsPath(Manifest), Name, Add ? Options.GetValueOrDefault("--version", "*") : null,
                    Flag("--optional"), Options.GetValueOrDefault("--platform"));
                Output.WriteLine($"[LUI:Packages] {(Add ? "Declared" : "Removed")} {Name}. Run package restore for a new lock or package update for an existing lock.");
                break;
            }
            case "pack":
            case "verify":
            {
                bool Pack = Arguments[0] == "pack";
                RequireOptions(Options, Pack ? ["--output"] : []);
                string Source = Path.GetFullPath(Position ?? throw new ArgumentException("Select a package directory or archive"));
                if (Pack && !Directory.Exists(Source)) throw PackageSource.Invalid("PackageDirectoryRequired");
                var Package = PackageSource.Read(Source);
                if (Pack) Package.WriteArchive(Path.GetFullPath(Required(Options, "--output")));
                Output.WriteLine(JsonSerializer.Serialize(new
                {
                    Package.Name, Version = Package.Version.ToString(3), Package.Sdk,
                    Package.License, Package.Digest, Package.Modules, Package.Types, Package.Capabilities, Package.TrustedNative,
                    Dependencies = Package.Requirements, OptionalDependencies = Package.OptionalRequirements, Package.FeatureModules, Package.Platforms,
                    NativeProviders = Package.NativeProviders.Select(Provider => Provider.Name)
                }));
                break;
            }
            case "fetch":
            case "import-git":
            {
                bool Fetch = Arguments[0] == "fetch";
                RequireOptions(Options, Fetch ? ["--sha256", "--source"] : ["--revision", "--source", "--subdir", "--git"]);
                string Source = Position ?? throw new ArgumentException("Select an archive URL or local Git repository");
                string Catalog = Path.GetFullPath(Required(Options, "--source"));
                string Installed = Fetch ? PackageImport.Fetch(Source, Required(Options, "--sha256"), Catalog) :
                    PackageImport.FromGit(Path.GetFullPath(Source), Required(Options, "--revision"), Options.GetValueOrDefault("--subdir"),
                        Required(Options, "--git"), Catalog);
                Output.WriteLine("[LUI:Packages] Imported " + Installed);
                break;
            }
            case "restore":
            case "update":
            case "inspect":
            {
                bool Inspect = Arguments[0] == "inspect";
                RequireOptions(Options, Inspect ? ["--requests", "--sdk", "--sdk-version", "--lock", "--include"] :
                    ["--source", "--requests", "--output", "--sdk", "--sdk-version", "--update", "--trusted-native", "--replace-output",
                        "--locked", "--lock", "--include"]);
                bool Update = Flag("--update") || Arguments[0] == "update";
                bool Locked = Flag("--locked");
                if (Update && Locked) throw new ArgumentException("Choose either update or locked restoration");
                string Manifest = Path.GetFullPath(Position ?? "lui.json");
                var Installation = SdkInstallation.Resolve(Options.GetValueOrDefault("--sdk"), Manifest);
                string? RequestedSdk = Options.GetValueOrDefault("--sdk-version");
                if (Installation is not null && RequestedSdk is not null && Installation.Version != RequestedSdk)
                    throw PackageSource.Invalid("SdkVersionMismatch");
                string Sdk = PackageSource.SdkVersion(Installation?.Version ?? RequestedSdk ?? PackageRestore.DefaultSdk, AllowWildcard: false);
                string Requests = RequestsPath(Manifest);
                string Lock = Options.GetValueOrDefault("--lock", PackageRestore.PinsName);
                var Included = PackageRequests.Include(Options.GetValueOrDefault("--include"));
                if (Inspect) {
                    Output.WriteLine(JsonSerializer.Serialize(PackageRestore.Inspect(Manifest, Requests, Lock, Sdk, Included), PackageSource.JsonOptions));
                    break;
                }
                string? Source = Options.TryGetValue("--source", out var Catalog) ? Path.GetFullPath(Catalog) : null;
                string Restored = PackageRestore.Restore(Manifest, Requests, Source, Options.GetValueOrDefault("--output", "lui.resolved.json"),
                    Sdk, Update, Flag("--trusted-native"), Flag("--replace-output"), Locked, Lock, Included);
                Output.WriteLine("[LUI:Packages] Restored " + Restored);
                break;
            }
            default: throw new ArgumentException("Unknown package operation");
        }
        return 0;
    }

    private static void CreateSdkProject(string DirectoryValue, SdkInstallation Sdk, string Template, string? StorageId)
    {
        string Root = Path.GetFullPath(DirectoryValue);
        string Name = Path.GetFileName(Path.TrimEndingDirectorySeparator(Root));
        if (Name.Length is < 1 or > 64 || !char.IsAsciiLetter(Name[0]) || Name.Any(Character => !char.IsAsciiLetterOrDigit(Character) && Character is not ('_' or '-')))
            throw new ArgumentException("Project name must start with a letter and use letters, digits, _ or -");
        if (Directory.Exists(Root)) throw new IOException("Choose a new project directory; existing directories are preserved");
        if(Template!="basic")
        {
            string Parent = Path.GetDirectoryName(Root)!;
            string Stage = Path.Combine(Parent, ".lui-new-" + Guid.NewGuid().ToString("N"));
            try
            {
                Sdk.CopyExample(Template, Stage);
                File.WriteAllText(Path.Combine(Stage, ".gitignore"), "Dist/\n.lui/\n");
                if (Template is "commands" or "workbench")
                {
                    File.WriteAllText(Path.Combine(Stage,"README.md"),$"# {Name}\n\nRun `lui check`, then `lui run`. Edit Src/Main.luau, save, close and run again.\n\nThis {Template} example uses optional vendored libraries. Keep Modules.lock.json and LuiToolchain.json with the project.\n\nStorage, when requested, belongs to this project directory. Packaged and copied projects use separate data.\n");
                }
                Directory.Move(Stage, Root);
            }
            finally
            {
                if (Directory.Exists(Stage)) Directory.Delete(Stage, recursive: true);
            }
            return;
        }
        Directory.CreateDirectory(Path.Combine(Root, "Src"));
        File.WriteAllText(Path.Combine(Root, "lui.json"), JsonSerializer.Serialize(new
        {
            SchemaVersion = 2, Script = "Src/Main.luau",
            Capabilities = StorageId is null ? Array.Empty<string>() : new[] { "Storage" }, Extensions = Array.Empty<string>(),
            Assets = Array.Empty<string>(), ExtensionDeclarations = Array.Empty<string>(), Diagnostics = new { Mode = "Development" },
            Modules = new { Root = "Src", Files = new[] { "Main.luau" }, Requires = Array.Empty<string>(), Dependencies = Array.Empty<object>() },
            Storage = StorageId is null ? null : new { ApplicationId = StorageId },
        }, new JsonSerializerOptions { WriteIndented = true, DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull }) + "\n");
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
        File.WriteAllText(Path.Combine(Root, "README.md"), $"# {Name}\n\nRun `lui check`, then `lui run` from this folder. In VS Code, use LUI: Run App.\n\nEdit Src/Main.luau, save, stop the app and run again. Clicking the button changes the greeting.\n\nUse `lui build --output Dist/{Name}` for an app folder. Keep LuiToolchain.json in version control.\n\nDeclare new module files in lui.json. Use the SDK's spec-cli template for examples of application-authored tests.\n");
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

    internal static ProcessStartInfo CreateHeadlessStartInfo(string Host, string Manifest, Dictionary<string, string> Options)
    {
        string Project = Path.GetFullPath(Manifest);
        ProcessStartInfo Start = new(Path.GetFullPath(Host)) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = Path.GetDirectoryName(Project)! };
        Start.ArgumentList.Add("--project"); Start.ArgumentList.Add(Project);
        if (Options.TryGetValue("--grant", out string? Names))
        {
            foreach (string Name in Names.Split(','))
            {
                if (Name is not ("network.client" or "network.server" or "network.raw" or "Storage")) throw new ArgumentException("Unsupported headless capability");
                Start.ArgumentList.Add("--grant"); Start.ArgumentList.Add(Name);
            }
        }
        foreach (string Key in new[] {"--http-origin", "--diagnostics", "--timeout-ms", "--poll-ms"})
            if (Options.TryGetValue(Key, out string? Value)) { Start.ArgumentList.Add(Key); Start.ArgumentList.Add(Value); }
        foreach (string Key in new[] {"--read-file", "--write-file", "--read-directory", "--write-directory", "--database-root", "--storage-root", "--tool", "--tool-cwd"})
            if (Options.TryGetValue(Key, out string? Value)) { Start.ArgumentList.Add(Key); Start.ArgumentList.Add(Path.GetFullPath(Value)); }
        if (Options.TryGetValue("--context", out string? Context))
        {
            if (Context != "true") throw new ArgumentException("--context accepts true");
            Start.ArgumentList.Add("--context");
        }
        foreach (string Key in new[] {"--env-keys", "--child-env-keys"})
            if (Options.TryGetValue(Key, out string? Keys))
                foreach (string Name in Keys.Split(',')) { Start.ArgumentList.Add(Key[..^1]); Start.ArgumentList.Add(Name); }
        if (Options.TryGetValue("--trusted-ffi", out string? Trust))
        {
            if (Trust!="true") throw new ArgumentException("--trusted-ffi accepts true");
            Start.ArgumentList.Add("--trusted-ffi");
        }
        if (Options.TryGetValue("--trusted-ffi-workers", out string? Workers))
        {
            if (Workers!="true"||Trust!="true") throw new ArgumentException("--trusted-ffi-workers true requires --trusted-ffi true");
            Start.ArgumentList.Add("--trusted-ffi-workers");
        }
        if (Options.TryGetValue("--providers", out string? Providers))
            foreach (string Name in Providers.Split(',')) {
                if (Name is not ("Compression" or "Crypto" or "SQLite" or "Regex" or "Toml")) throw new ArgumentException("Unknown standard provider");
                Start.ArgumentList.Add("--provider"); Start.ArgumentList.Add(Name);
            }
        if (Options.TryGetValue("--http-public-only", out string? PublicOnly))
        {
            if (PublicOnly != "true") throw new ArgumentException("--http-public-only accepts true");
            Start.ArgumentList.Add("--http-public-only");
        }
        return Start;
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

    internal static bool TryHeadlessDiagnostic(string Line, out string Message)
    {
        const string Prefix = "[LUI:Headless] ";
        Message = "";
        if (!Line.StartsWith(Prefix, StringComparison.Ordinal)) return false;
        string Code = Line[Prefix.Length..];
        string? Advice = Code switch {
            "HttpPolicyRequired" => "Choose --http-origin <origin> or --http-public-only true for outbound networking.",
            "CapabilityDenied" => "Pass --grant for the capabilities declared by this app.",
            "UnsupportedCapability" => "Headless grants support network.client, network.server and network.raw.",
            "NativeProvidersUnsupported" => "Select the SDK providers requested by this project.",
            "FfiTrustRequired" => "The project requests in-process native authority. Its operator must pass --trusted-ffi true.",
            "FfiWorkerTrustRequired" => "Trusted worker execution requires the separate --trusted-ffi true grant.",
            "ProviderNotQualified" => "The selected providers must match this SDK's pinned images and the project's declarations.",
            "PresentationUnsupported" => "Use a headless entry without UI Instances, or launch the native desktop host.",
            "ProjectPreparationFailed" => "Check the manifest, declared files and module lock.",
            "InvalidArguments" => "Check the headless command options and their values.",
            "InvalidAssets" => "Check the app's declared assets.",
            "InvalidNetworkPolicy" or "InvalidHttpPolicy" => "Check the declared network policy and exact origins.",
            "RuntimeFailed" => "The app failed; use Check Types and an explicit development diagnostic run to investigate.",
            "TimedOut" => "The timeout was reached. Close ongoing work or choose a longer --timeout-ms.",
            "Interrupted" => "The app stopped on request.",
            "RuntimeUnavailable" or "DomainUnavailable" or "WakeUnavailable" or "ProjectUnavailable" or "Failed" => "Check the SDK with lui doctor and report a minimal reproduction.",
            _ => null,
        };
        if (Advice is null) return false;
        Message = Prefix + Code + ": " + Advice;
        return true;
    }

    internal static int RunNative(ProcessStartInfo Start, TextWriter Errors, bool Headless = false)
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
                            if (!Oversized && Records < 256) {
                                string Line = Pending.ToString().TrimEnd('\r');
                                bool Accepted = ApplicationDiagnostic.TryReadRecord(Line, out string Message, out bool Startup);
                                if (!Accepted && Headless) { Accepted = TryHeadlessDiagnostic(Line, out Message); Startup = Accepted; }
                                if (Accepted) { ++Records; StartupReported |= Startup; Errors.WriteLine(Message); }
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

    internal static ProcessStartInfo CreateRunStartInfo(string Host, string ManifestPath, bool AllowNative=false)
    {
        ProcessStartInfo Start = new(Host)
        {
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(Path.GetFullPath(ManifestPath))!,
        };
        Start.ArgumentList.Add("--manifest");
        Start.ArgumentList.Add(Path.GetFullPath(ManifestPath));
        if (AllowNative) Start.ArgumentList.Add("--allow-native");
        return Start;
    }
}
