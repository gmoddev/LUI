using Lui.Cli;
using Lui.WinUI;

if (args.Length != 1) throw new ArgumentException("Expected native runtime library path");
string Runtime = Path.GetFullPath(args[0]);
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

    var Checked = Invoke("check", ManifestPath, "--runtime", Runtime);
    Require(Checked.Status == 0 && Checked.Output.Contains("syntax valid", StringComparison.Ordinal),
        "valid source did not pass check: " + Checked.Error);
    File.WriteAllText(Manifest.ScriptPath, "local = ");
    var Invalid = Invoke("check", ManifestPath, "--runtime", Runtime);
    Require(Invalid.Status == 1 && Invalid.Error.StartsWith("[LUI:CLI] ", StringComparison.Ordinal),
        "invalid Luau syntax was accepted");
    File.WriteAllText(Manifest.ScriptPath, "local Value = 1\0error('truncated source')");
    Require(Invoke("check", ManifestPath, "--runtime", Runtime).Status == 1,
        "checker accepted a truncated script");
    File.WriteAllText(Manifest.ScriptPath, "error('check must not execute')");
    Require(Invoke("check", ManifestPath, "--runtime", Runtime).Status == 0,
        "checker executed valid source");
    File.WriteAllText(ManifestPath, "{");
    Require(Invoke("check", ManifestPath, "--runtime", Runtime).Status == 1,
        "malformed JSON escaped CLI error handling");
    File.WriteAllText(ManifestPath,
        "{\"SchemaVersion\":1,\"Script\":\"src/main.luau\",\"Capabilities\":[\"Unknown\"]}");
    Require(Invoke("check", ManifestPath, "--runtime", Runtime).Status == 1,
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

Console.WriteLine("[LUI:CliTest] Scaffold, validation, syntax check, and packaging passed");
