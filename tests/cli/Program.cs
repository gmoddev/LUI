using Lui.Cli;
using Lui.WinUI;
using System.Text.Json;

if (args.Length != 2) throw new ArgumentException("Expected native runtime and type checker paths");
string Runtime = Path.GetFullPath(args[0]);
string Checker = Path.GetFullPath(args[1]);
string TempRoot = Path.Combine(Path.GetTempPath(), "LUI-CliTest-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(TempRoot);

static (int Status, string Output, string Error) Invoke(params string[] Arguments)
{
    using StringWriter Output = new();
    using StringWriter Error = new();
    return (CliProgram.Run(Arguments, Output, Error), Output.ToString(), Error.ToString());
}

static void Require(bool Condition, string Message)
{
    if (!Condition) throw new Exception("[LUI:CliTest] " + Message);
}

static (int Status, string Output, string Error) Syntax(string Manifest, string Runtime) =>
    Invoke("check", Manifest, "--mode", "syntax", "--runtime", Runtime);

try
{
    string Project = Path.Combine(TempRoot, "My_LUI_Project");
    var Created = Invoke("new", Project);
    Require(Created.Status == 0, "project scaffold failed: " + Created.Error);
    string ManifestPath = Path.Combine(Project, "lui.json");
    AppManifest Manifest = AppManifest.Load(ManifestPath);
    Require(Manifest.ScriptPath == Path.Combine(Project, "src", "main.luau"),
        "new project did not use the shared manifest");
    Require(Invoke("new", Project).Status == 1, "new overwrote an existing project");

    var Checked = Syntax(ManifestPath, Runtime);
    Require(Checked.Status == 0 && Checked.Output.Contains("syntax valid", StringComparison.Ordinal),
        "valid source did not pass check: " + Checked.Error);
    File.WriteAllText(Manifest.ScriptPath, "local = ");
    var Invalid = Syntax(ManifestPath, Runtime);
    Require(Invalid.Status == 1 && Invalid.Error.StartsWith("[LUI:CLI] ", StringComparison.Ordinal),
        "invalid Luau syntax was accepted");
    File.WriteAllText(Manifest.ScriptPath, "local Value = 1\0error('truncated source')");
    Require(Syntax(ManifestPath, Runtime).Status == 1,
        "checker accepted a truncated script");
    File.WriteAllText(Manifest.ScriptPath, "error('check must not execute')");
    Require(Syntax(ManifestPath, Runtime).Status == 0,
        "checker executed valid source");
    Require(Invoke("check", ManifestPath, "--checker", Checker).Status == 0,
        "type checker executed application source");
    File.WriteAllText(Manifest.ScriptPath,
        "local Window = Instance.new(\"Window\")\nWindow.Title = \"Typed\"\n");
    Require(Invoke("check", ManifestPath, "--checker", Checker).Status == 0,
        "reflected LUI globals were missing from application type check");
    File.WriteAllText(Manifest.ScriptPath,
        "local Prefix = \"😀\"; local Number: number = \"hello\"\r\n");
    var Typed = Invoke("check", ManifestPath, "--checker", Checker, "--format", "json");
    Require(Typed.Status == 1 && Typed.Error.Length == 0, "type mismatch did not produce JSON diagnostics");
    using (JsonDocument Document = JsonDocument.Parse(Typed.Output))
    {
        JsonElement Result = Document.RootElement;
        Require(Result.GetProperty("version").GetInt32() == 1 &&
            Result.GetProperty("source").GetString() == "src/main.luau", "unversioned or absolute diagnostic source");
        JsonElement Diagnostic = Result.GetProperty("diagnostics")[0];
        JsonElement Start = Diagnostic.GetProperty("range").GetProperty("start");
        Require(Diagnostic.GetProperty("kind").GetString() == "type" &&
            Start.GetProperty("line").GetInt32() == 0 && Start.GetProperty("column").GetInt32() ==
                File.ReadAllText(Manifest.ScriptPath).IndexOf("\"hello\"", StringComparison.Ordinal),
            "type error did not carry the UTF-16 source range");
    }
    File.WriteAllText(Manifest.ScriptPath, "local Root = Instance.new(\"Frame\")\nRoot.AbsoluteSize = Vector2.new(1, 1)\n");
    var ReadOnly = Invoke("check", ManifestPath, "--checker", Checker);
    Require(ReadOnly.Status == 1 && ReadOnly.Error.Contains("read-only"), "read-only assignment passed application check");
    File.WriteAllText(Manifest.ScriptPath, "local Props: WindowInit = {Title = 42}\n");
    Require(Invoke("check", ManifestPath, "--checker", Checker).Status == 1, "typed initializer accepted the wrong field type");
    File.WriteAllText(Manifest.ScriptPath, "local = ");
    var SyntaxJson = Invoke("check", ManifestPath, "--checker", Checker, "--format", "json");
    using (JsonDocument Document = JsonDocument.Parse(SyntaxJson.Output))
        Require(SyntaxJson.Status == 1 && Document.RootElement.GetProperty("diagnostics").EnumerateArray()
            .Any(Diagnostic => Diagnostic.GetProperty("kind").GetString() == "syntax"), "parser errors did not reach JSON diagnostics");
    File.WriteAllText(Manifest.ScriptPath, "error('check must not execute')");
    Require(Invoke("check", ManifestPath, "--checker", Checker, "--definitions", Path.Combine(Project, "missing.luau")).Status == 1,
        "missing definitions silently skipped type checking");
    File.WriteAllText(Manifest.ScriptPath, "local Value = 1\0error('truncated source')");
    Require(Invoke("check", ManifestPath, "--checker", Checker).Status == 1, "type checker accepted NUL source");
    File.WriteAllBytes(Manifest.ScriptPath, [0xff, 0xfe, 0x61, 0x00]);
    Require(Invoke("check", ManifestPath, "--checker", Checker).Status == 1, "type checker accepted non UTF-8 input");
    File.WriteAllText(Manifest.ScriptPath, "error('check must not execute')");
    File.WriteAllText(ManifestPath, "{");
    Require(Syntax(ManifestPath, Runtime).Status == 1,
        "malformed JSON escaped CLI error handling");
    File.WriteAllText(ManifestPath,
        "{\"SchemaVersion\":1,\"Script\":\"src/main.luau\",\"Capabilities\":[\"Unknown\"]}");
    Require(Syntax(ManifestPath, Runtime).Status == 1,
        "check accepted an invalid manifest");

    string Modular = Path.Combine(TempRoot, "Modular_\u03bc");
    Directory.CreateDirectory(Path.Combine(Modular, "Src"));
    string ModularManifest = Path.Combine(Modular, "lui.json");
    string ModuleEntry = Path.Combine(Modular, "Src", "Main.luau");
    string ModuleData = Path.Combine(Modular, "Src", "Data.luau");
    string ModuleUnused = Path.Combine(Modular, "Src", "Unused.luau");
    object ProjectManifest(string Diagnostics) => new
    {
        SchemaVersion = 2, Script = "Src/Main.luau", Capabilities = Array.Empty<string>(), Extensions = Array.Empty<string>(),
        Assets = Array.Empty<string>(), ExtensionDeclarations = Array.Empty<string>(), Diagnostics = new { Mode = Diagnostics },
        Modules = new { Root = "Src", Files = new[] { "Main.luau", "Data.luau", "Unused.luau" }, Requires = Array.Empty<string>(), Dependencies = Array.Empty<object>() },
    };
    File.WriteAllText(ModularManifest, JsonSerializer.Serialize(ProjectManifest("Development")));
    string ProjectTool = Path.Combine(Path.GetDirectoryName(Checker)!, OperatingSystem.IsWindows() ? "LuiProjectTool.exe" : "LuiProjectTool");
    File.WriteAllText(ModuleData, "return {Count=4}"); File.WriteAllText(ModuleUnused, "error('CHECK_MUST_NOT_EXECUTE'); return false");
    string NativeDeclarationPath = Path.Combine(Modular, "Native.json"), NativeFile = Path.Combine(Modular, "Synthetic.dll");
    byte[] NativeBytes = [0, 255, 1, 2, 3]; File.WriteAllBytes(NativeFile, NativeBytes);
    string NativeHash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(NativeBytes)).ToLowerInvariant();
    File.WriteAllText(NativeDeclarationPath, JsonSerializer.Serialize(new
    {
        SchemaVersion = 2, AbiVersion = 1, Provider = "Native",
        Extension = new { Name = "NativeCounter", Version = 1, RequiredCapabilities = new[] { "NativeExtensions" },
            Provenance = new { Kind = "LocalVendor", Id = "Synthetic-1" },
            Bindings = new[] { new { Platform = "WindowsX64", Path = "Synthetic.dll", Sha256 = NativeHash } } },
        Services = new[] { new { Name = "Counter", Methods = new[] { new { Name = "Count", Arguments = Array.Empty<string>(), Returns = "number" } }, Signals = Array.Empty<object>() } },
    }));
    var NativeManifest = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(ModularManifest))!;
    NativeManifest["Capabilities"] = new System.Text.Json.Nodes.JsonArray("NativeExtensions");
    NativeManifest["Extensions"] = new System.Text.Json.Nodes.JsonArray("NativeCounter");
    NativeManifest["ExtensionDeclarations"] = new System.Text.Json.Nodes.JsonArray("Native.json");
    File.WriteAllText(ModularManifest, NativeManifest.ToJsonString());
    File.WriteAllText(ModuleEntry, "local N:number=app:GetService('Counter'):Count()");
    Require(Invoke("check", ModularManifest, "--mode", "project", "--diagnostics", "development", "--checker", Checker).Status == 0,
        "native provider metadata did not typecheck without loading synthetic bytes");
    string NativeStage = Path.Combine(TempRoot, "NativeStage");
    var NativeStaged = Invoke("stage", ModularManifest, "--output", NativeStage, "--project-tool", ProjectTool);
    Require(NativeStaged.Status == 0 && Invoke("verify", NativeStage, "--project-tool", ProjectTool).Status == 0,
        "native bindings failed CLI staging/verification: " + NativeStaged.Error);
    using (JsonDocument Summary = JsonDocument.Parse(NativeStaged.Output))
        Require(Summary.RootElement.GetProperty("SchemaVersion").GetInt32() == 2 && Summary.RootElement.GetProperty("Profile").GetString() == "DeclaredContentV2",
            "native package summary downgraded its profile");
    File.WriteAllBytes(Path.Combine(NativeStage, "Synthetic.dll"), [4, 5]);
    var NativeTampered = Invoke("verify", NativeStage, "--project-tool", ProjectTool);
    Require(NativeTampered.Status == 1 && NativeTampered.Error.Contains("BinaryMismatch") && !NativeTampered.Error.Contains(NativeStage),
        "edited native package accepted or leaked physical path");
    File.WriteAllText(ModularManifest, JsonSerializer.Serialize(ProjectManifest("Development")));
    File.WriteAllText(ModuleEntry, "local Data=require('./Data'); local N:number=Data.Count");
    var ModuleValid = Invoke("check", ModularManifest, "--mode", "project", "--diagnostics", "development", "--checker", Checker, "--format", "json");
    Require(ModuleValid.Status == 0, "valid modular sources did not check: " + ModuleValid.Error);
    using (JsonDocument Document = JsonDocument.Parse(ModuleValid.Output))
    {
        JsonElement Result = Document.RootElement;
        Require(Result.GetProperty("version").GetInt32() == 2 && Result.GetProperty("sources").GetArrayLength() == 3 &&
            Result.GetProperty("digest").GetString()!.Length == 64 && Result.GetProperty("entry").GetString() == "app:Main.luau",
            "project checker did not produce v2 identity/digests");
        Require(!ModuleValid.Output.Contains(Modular, StringComparison.Ordinal), "project JSON exposed physical roots");
        ProjectTypeChecker.Validate(Result, 0);
    }
    static void MalformedProject(string Text, int Status)
    {
        using JsonDocument Document = JsonDocument.Parse(Text);
        bool Refused = false; try { ProjectTypeChecker.Validate(Document.RootElement, Status); } catch (InvalidDataException) { Refused = true; }
        Require(Refused, "malformed project protocol accepted");
    }
    MalformedProject(ModuleValid.Output.Replace("\"version\":2", "\"version\":1"), 0);
    MalformedProject(ModuleValid.Output.Replace("Src/Data.luau", "../escaped.luau"), 0);
    MalformedProject(ModuleValid.Output.Replace("\"redacted\":false", "\"redacted\":true"), 0);
    string StagedDirectory = Path.Combine(TempRoot, "Staged_\u03bc");
    if(OperatingSystem.IsWindows()) {
    string SyntheticHost=Path.Combine(TempRoot,"SyntheticHost"), Bundle=Path.Combine(TempRoot,"Bundle");
    Directory.CreateDirectory(SyntheticHost);
    File.WriteAllText(Path.Combine(SyntheticHost,"Lui.WinUI.exe"),"Synthetic host, not executable");
    File.WriteAllText(Path.Combine(SyntheticHost,"LuiRuntime.dll"),"Synthetic runtime, not executable");
    File.WriteAllText(Path.Combine(SyntheticHost,"Symbols.pdb"),"Excluded symbols");
    var Bundled=Invoke("build",ModularManifest,"--mode","project","--host",SyntheticHost,"--project-tool",ProjectTool,"--output",Bundle);
    Require(Bundled.Status==0 && File.Exists(Path.Combine(Bundle,"Start-LUI.cmd")) &&
        !File.Exists(Path.Combine(Bundle,"Symbols.pdb")) &&
        Invoke("verify",Path.Combine(Bundle,"Application"),"--project-tool",ProjectTool).Status==0,"modular host composition failed: "+Bundled.Error);
    Require(Invoke("build",ModularManifest,"--mode","project","--host",SyntheticHost,"--project-tool",ProjectTool,"--output",Bundle).Status==1,"bundle overwrote output");
    File.WriteAllText(Path.Combine(SyntheticHost,"ApplicationReserved"),"collision");
    Require(Invoke("build",ModularManifest,"--mode","project","--host",SyntheticHost,"--project-tool",ProjectTool,"--output",Path.Combine(TempRoot,"Collision")).Status==1,"reserved host namespace accepted");
    }
    var Staged = Invoke("stage", ModularManifest, "--output", StagedDirectory, "--project-tool", ProjectTool);
    Require(Staged.Status == 0 && !Staged.Output.Contains(Modular, StringComparison.Ordinal), "project staging failed: " + Staged.Error);
    var Verified = Invoke("verify", StagedDirectory, "--project-tool", ProjectTool);
    Require(Verified.Status == 0 && Staged.Output == Verified.Output, "staged verification fingerprint diverged: " + Verified.Error);
    Require(Invoke("stage", ModularManifest, "--output", StagedDirectory, "--project-tool", ProjectTool).Status == 1,
        "stage overwrote existing content");
    File.WriteAllText(Path.Combine(StagedDirectory, "Src", "Data.luau"), "return {Count=9}");
    var Tampered = Invoke("verify", StagedDirectory, "--project-tool", ProjectTool);
    Require(Tampered.Status == 1 && Tampered.Error.Contains("PackageMismatch") && !Tampered.Error.Contains(StagedDirectory),
        "edited package verified or error leaked physical root");
    static void MalformedPackage(string Text)
    {
        using JsonDocument Document = JsonDocument.Parse(Text);
        bool Refused = false; try { ProjectPackager.Validate(Document.RootElement); } catch (InvalidDataException) { Refused = true; }
        Require(Refused, "malformed package summary accepted");
    }
    MalformedPackage(Staged.Output.Replace("\"SchemaVersion\":1", "\"SchemaVersion\":2"));
    MalformedPackage(Staged.Output.Replace("\"SourceCount\":3", "\"SourceCount\":\"3\""));
    MalformedPackage(Staged.Output.Replace("\"Profile\":\"DeclaredContentV1\"", "\"Profile\":\"Unknown\""));
    // Structured registration data supplies types without loading host code.
    string HostDeclaration = Path.Combine(Modular, "Host.json");
    File.WriteAllText(HostDeclaration, JsonSerializer.Serialize(new
    {
        SchemaVersion = 1, AbiVersion = 1, Provider = "Host", Services = new[] { new
        {
            Name = "Counter", Methods = new[] { new { Name = "Count", Arguments = new[] { "string" }, Returns = "number" } },
            Signals = Array.Empty<object>(),
        } },
    }));
    var DeclaredManifest = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(ModularManifest))!;
    DeclaredManifest["Capabilities"] = new System.Text.Json.Nodes.JsonArray("HostServices");
    DeclaredManifest["ExtensionDeclarations"] = new System.Text.Json.Nodes.JsonArray("Host.json");
    File.WriteAllText(ModularManifest, DeclaredManifest.ToJsonString());
    File.WriteAllText(ModuleEntry, "local N:number=app:GetService('Counter'):Count('demo')");
    var DeclaredCheck = Invoke("check", ModularManifest, "--mode", "project", "--diagnostics", "development", "--checker", Checker);
    Require(DeclaredCheck.Status == 0, "declared host service failed CLI analysis: " + DeclaredCheck.Error);
    string DeclaredStage = Path.Combine(TempRoot, "DeclaredStage");
    Require(Invoke("stage", ModularManifest, "--output", DeclaredStage, "--project-tool", ProjectTool).Status == 0 &&
        File.Exists(Path.Combine(DeclaredStage, "Host.json")) && Invoke("verify", DeclaredStage, "--project-tool", ProjectTool).Status == 0,
        "declared service sidecar failed CLI staging/verification");
    File.WriteAllText(HostDeclaration, File.ReadAllText(HostDeclaration).Replace("\"number\"", "\"any\""));
    var InvalidDeclaration = Invoke("check", ModularManifest, "--mode", "project", "--diagnostics", "development", "--checker", Checker);
    Require(InvalidDeclaration.Status == 1 && InvalidDeclaration.Error.Contains("InvalidDeclaration") && !InvalidDeclaration.Error.Contains(Modular),
        "invalid sidecar lacked safe structured setup failure");
    File.WriteAllText(ModularManifest, JsonSerializer.Serialize(ProjectManifest("Development")));
    File.WriteAllText(ModuleEntry, "local Data=require('./Data'); local N:string=Data.Count");
    var ModuleInvalid = Invoke("check", ModularManifest, "--mode", "project", "--diagnostics", "development", "--checker", Checker, "--format", "json");
    Require(ModuleInvalid.Status == 1, "incorrect imported export type passed CLI check");
    using (JsonDocument Document = JsonDocument.Parse(ModuleInvalid.Output))
        Require(Document.RootElement.GetProperty("diagnostics").EnumerateArray().Any(Issue => Issue.GetProperty("source").GetString() == "app:Main.luau"),
            "modular error lost source identity");
    File.WriteAllText(ModuleEntry, "local Path:string='./Data'; local Value=require(Path); local N:number=Value.Count");
    Require(Invoke("check", ModularManifest, "--mode", "project", "--diagnostics", "development", "--checker", Checker).Status == 1,
        "dynamic module import did not remain unknown in CLI");
    File.WriteAllText(ModularManifest, JsonSerializer.Serialize(ProjectManifest("Restricted")));
    var Restricted = Invoke("check", ModularManifest, "--mode", "project", "--diagnostics", "development", "--checker", Checker);
    Require(Restricted.Status == 1 && Restricted.Error.Contains("PolicyNotSupported") && !Restricted.Error.Contains("Value.Count"),
        "restricted project source was disclosed");
    Require(Invoke("check", ModularManifest, "--mode", "project", "--checker", Checker).Status == 1,
        "project diagnostic owner option was optional");
    Require(Invoke("check", ModularManifest, "--mode", "project", "--diagnostics", "development", "--runtime", Runtime).Status == 1,
        "project checker accepted runtime execution option");

    if(OperatingSystem.IsLinux())
    {
        File.WriteAllText(ModularManifest,JsonSerializer.Serialize(ProjectManifest("Development")));
        File.WriteAllText(ModuleEntry,"local Data=require('./Data'); assert(Data.Count~=nil)");
        string Host=Path.Combine(TempRoot,"gtk-host");
        Directory.CreateDirectory(Path.Combine(Host,"bin")); Directory.CreateDirectory(Path.Combine(Host,"lib"));
        string Executable=Path.Combine(Host,"bin","lui-gtk"); File.WriteAllText(Executable,"#!/bin/sh\nexit 0\n");
        File.SetUnixFileMode(Executable,UnixFileMode.UserRead|UnixFileMode.UserWrite|UnixFileMode.UserExecute);
        File.Copy(Runtime,Path.Combine(Host,"lib","libLuiRuntime.so"));
        string Output=Path.Combine(TempRoot,"GTK package with spaces");
        var Built=Invoke("build",ModularManifest,"--mode","project","--host",Host,"--project-tool",ProjectTool,"--output",Output);
        Require(Built.Status==0 && File.Exists(Path.Combine(Output,"Application","lui.json")) && File.Exists(Path.Combine(Output,"bin","lui-gtk")),"GTK package failed: "+Built.Error);
        Require((File.GetUnixFileMode(Path.Combine(Output,"Start-LUI.sh"))&UnixFileMode.UserExecute)!=0 &&
            (File.GetUnixFileMode(Path.Combine(Output,"bin","lui-gtk"))&UnixFileMode.UserExecute)!=0,"GTK executable modes lost");
        Require(Invoke("verify",Path.Combine(Output,"Application"),"--project-tool",ProjectTool).Status==0,"GTK captured content verification failed");
        Require(Invoke("build",ModularManifest,"--mode","project","--host",Host,"--project-tool",ProjectTool,"--output",Output).Status==1,"GTK package overwritten");
        File.CreateSymbolicLink(Path.Combine(Host,"linked.so"),Path.Combine(Host,"lib","libLuiRuntime.so"));
        Require(Invoke("build",ModularManifest,"--mode","project","--host",Host,"--project-tool",ProjectTool,"--output",Path.Combine(TempRoot,"LinkedPackage")).Status==1,"GTK linked host input accepted");
        File.Delete(Path.Combine(Host,"linked.so"));
        Directory.CreateSymbolicLink(Path.Combine(Host,"linked-directory"),Host);
        Require(Invoke("build",ModularManifest,"--mode","project","--host",Host,"--project-tool",ProjectTool,"--output",Path.Combine(TempRoot,"LinkedDirectoryPackage")).Status==1,"GTK recursive linked directory accepted");
    }
    if (OperatingSystem.IsWindows())
    {
        File.WriteAllText(ManifestPath,
            "{\"SchemaVersion\":1,\"Script\":\"src/main.luau\",\"Capabilities\":[],\"Extensions\":[],\"Assets\":[]}");
        string Host = Path.Combine(TempRoot, "host");
        Directory.CreateDirectory(Host);
        File.WriteAllBytes(Path.Combine(Host, "Lui.WinUI.exe"), [1, 2, 3]);
        File.Copy(Runtime, Path.Combine(Host, "LuiRuntime.dll"));
        string Output = Path.Combine(TempRoot, "package");
        var Built = Invoke("build", ManifestPath, "--host", Host, "--output", Output);
        Require(Built.Status == 0 && File.Exists(Path.Combine(Output, "src", "main.luau")),
            "build did not stage the project: " + Built.Error);
        Require(Invoke("build", ManifestPath, "--host", Host, "--output", Output).Status == 1,
            "build overwrote a package");
        var Start = CliProgram.CreateRunStartInfo(Path.Combine(Host, "Lui.WinUI.exe"), ManifestPath);
        Require(Start.ArgumentList.Count == 2 && Start.ArgumentList[0] == "--manifest" &&
            Start.ArgumentList[1] == ManifestPath && Start.WorkingDirectory == Project,
            "run did not pass an absolute manifest to the native host");
    }
}
finally
{
    string SafeRoot = Path.GetFullPath(TempRoot);
    if (!string.Equals(Path.GetDirectoryName(SafeRoot),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(Path.GetTempPath())),
            StringComparison.OrdinalIgnoreCase) ||
        !Path.GetFileName(SafeRoot).StartsWith("LUI-CliTest-", StringComparison.Ordinal))
        throw new InvalidOperationException("Test cleanup target left the temporary directory");
    Directory.Delete(SafeRoot, recursive: true);
}

Console.WriteLine("[LUI:CliTest] Scaffold, validation, syntax/type diagnostics, and packaging passed");
