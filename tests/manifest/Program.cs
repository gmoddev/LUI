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
    string NestedDirectory = Path.Combine(TempRoot, "ui");
    Directory.CreateDirectory(NestedDirectory);
    File.WriteAllText(Path.Combine(NestedDirectory, "main.luau"), "print('nested script')");
    File.WriteAllText(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"ui/main.luau\"}");
    if (!AppManifest.Load(ManifestPath).ScriptPath.EndsWith(
            Path.Combine("ui", "main.luau"), StringComparison.Ordinal))
        throw new Exception("[LUI:ManifestTest] Nested script did not resolve");
    bool LinkCreated = false;
    try
    {
        Directory.CreateSymbolicLink(Path.Combine(TempRoot, "linked-ui"), NestedDirectory);
        LinkCreated = true;
    }
    catch (Exception Error) when (Error is UnauthorizedAccessException or IOException)
    {
        Console.WriteLine("[LUI:ManifestTest] Directory symlink creation unavailable");
    }
    if (LinkCreated)
    {
        Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"linked-ui/main.luau\"}");
        Directory.Delete(Path.Combine(TempRoot, "linked-ui"));
    }
    Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Sandbox\":{\"MaxMemoryBytes\":1024,\"MaxInterrupts\":100}}");
    Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Capabilities\":[\"NativeExtensions\"],\"Sandbox\":{\"MaxMemoryBytes\":1048576,\"MaxInterrupts\":100}}");
    File.WriteAllText(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Sandbox\":{\"MaxMemoryBytes\":1048576,\"MaxInterrupts\":100}}");
    if (AppManifest.Load(ManifestPath).Sandbox?.MaxInterrupts != 100)
        throw new Exception("[LUI:ManifestTest] Sandbox manifest did not resolve");
    File.WriteAllText(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Capabilities\":[\"Clipboard\",\"Dialogs\"]}");
    AppManifest Platform = AppManifest.Load(ManifestPath);
    if (!Platform.AllowClipboard || !Platform.AllowDialogs)
        throw new Exception("[LUI:ManifestTest] Platform capability manifest did not resolve");
    File.WriteAllText(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Capabilities\":[\"network.client\",\"network.server\",\"network.raw\"]}");
    AppManifest Network = AppManifest.Load(ManifestPath);
    if (!Network.AllowNetworkClient || !Network.AllowNetworkServer || !Network.AllowNetworkRaw)
        throw new Exception("[LUI:ManifestTest] Network capability manifest did not resolve");
    Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Capabilities\":[\"NetworkClient\"]}");
    File.WriteAllBytes(Path.Combine(TempRoot, "logo.png"), new byte[] { 1, 2, 3 });
    File.WriteAllText(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Assets\":[\"logo.png\"]}");
    if (!AppManifest.Load(ManifestPath).AssetPaths.ContainsKey("logo.png"))
        throw new Exception("[LUI:ManifestTest] Packaged asset did not resolve");
    Reject(ManifestPath, "{\"SchemaVersion\":1,\"Script\":\"app.luau\",\"Assets\":[\"../logo.png\"]}");
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
