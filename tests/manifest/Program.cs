using Lui.WinUI;

if (args.Length != 1) throw new ArgumentException("Expected repository root");
string Root = Path.GetFullPath(args[0]);
AppManifest Example = AppManifest.Load(Path.Combine(Root, "examples", "native-service.manifest.json"));
if (!Example.AllowNativeExtensions || Example.ExtensionPaths.Count != 1 ||
    !Example.ScriptPath.EndsWith("native-service.luau", StringComparison.Ordinal))
    throw new Exception("[LUI:ManifestTest] Valid example manifest did not resolve");

string TempRoot = Path.Combine(Path.GetTempPath(), "LUI-ManifestTest-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(TempRoot);
try
{
    string Script = Path.Combine(TempRoot, "app.luau");
    File.WriteAllText(Script, "print('manifest test')");
    string ManifestPath = Path.Combine(TempRoot, "app.json");
    static void Reject(string ManifestPathValue, string Json)
    {
        File.WriteAllText(ManifestPathValue, Json);
        try { AppManifest.Load(ManifestPathValue); }
        catch (InvalidDataException) { return; }
        throw new Exception("[LUI:ManifestTest] Invalid manifest was accepted: " + Json);
    }
    Reject(ManifestPath, "{\"SchemaVersion\":2,\"Script\":\"app.luau\"}");
    Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Capabilities\":[\"Unknown\"]}");
    Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Extensions\":[\"a.dll\"]}");
    Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Capabilities\":[\"NativeExtensions\"],\"Extensions\":[\"../a.dll\"]}");
    Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"../outside.luau\"}");
}
finally
{
    string SafeRoot = Path.GetFullPath(TempRoot);
    if (!string.Equals(Path.GetDirectoryName(SafeRoot),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(Path.GetTempPath())),
            StringComparison.OrdinalIgnoreCase) ||
        !Path.GetFileName(SafeRoot).StartsWith("LUI-ManifestTest-", StringComparison.Ordinal))
        throw new InvalidOperationException("Test cleanup target left the temporary directory");
    Directory.Delete(SafeRoot, recursive: true);
}
Console.WriteLine("[LUI:ManifestTest] Capability and path validation passed");
