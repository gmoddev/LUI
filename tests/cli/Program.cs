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
