using Lui.Packaging;

string Root = Path.Combine(Path.GetTempPath(), "LUI-PackageTest-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(Root);
try
{
    string App = Path.Combine(Root, "app");
    string Host = Path.Combine(Root, "host");
    string Extensions = Path.Combine(Root, "extensions");
    string Output = Path.Combine(Root, "output");
    Directory.CreateDirectory(App);
    Directory.CreateDirectory(Host);
    Directory.CreateDirectory(Extensions);
    File.WriteAllText(Path.Combine(App, "entry.luau"), "print('package')");
    File.WriteAllBytes(Path.Combine(App, "logo.png"), new byte[] { 1, 2, 3 });
    File.WriteAllText(Path.Combine(App, "app.json"),
        "{\"SchemaVersion\":1,\"Script\":\"entry.luau\",\"Capabilities\":[\"NativeExtensions\"]," +
        "\"Extensions\":[\"Plugin.dll\"],\"Assets\":[\"logo.png\"]}");
    File.WriteAllText(Path.Combine(Host, "Lui.WinUI.exe"), "host");
    File.WriteAllText(Path.Combine(Host, "LuiRuntime.dll"), "runtime");
    File.WriteAllText(Path.Combine(Host, "Lui.WinUI.pdb"), "debug");
    File.WriteAllText(Path.Combine(Extensions, "Plugin.dll"), "plugin");
    File.WriteAllText(Path.Combine(Extensions, "Unlisted.dll"), "not packaged");
    PackageBuilder.Stage(Path.Combine(App, "app.json"), Host, Output, Extensions);
    string[] Names = Directory.EnumerateFiles(Output).Select(Path.GetFileName).OrderBy(Name => Name).ToArray()!;
    string[] Expected = new[] { "Lui.WinUI.exe", "LuiRuntime.dll", "Plugin.dll", "app.json", "entry.luau", "logo.png" }
        .OrderBy(Name => Name).ToArray();
    if (!Names.SequenceEqual(Expected))
        throw new Exception("[LUI:PackageTest] Package included an undeclared file or omitted a declared file");
    try { PackageBuilder.Stage(Path.Combine(App, "app.json"), Host, Output, Extensions); }
    catch (InvalidDataException) { Console.WriteLine("[LUI:PackageTest] Nonempty output rejected"); return; }
    throw new Exception("[LUI:PackageTest] Nonempty output was accepted");
}
finally
{
    if (Path.GetFileName(Root).StartsWith("LUI-PackageTest-", StringComparison.Ordinal) &&
        Path.GetDirectoryName(Root) == Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar))
        Directory.Delete(Root, recursive: true);
}
