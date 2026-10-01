using System.Text.Json;

namespace Lui.WinUI;

internal sealed class AppManifest
{
    public int SchemaVersion { get; set; }
    public string Script { get; set; } = string.Empty;
    public List<string> Capabilities { get; set; } = new();
    public List<string> Extensions { get; set; } = new();

    public string ScriptPath { get; private set; } = string.Empty;
    public List<string> ExtensionPaths { get; private set; } = new();
    public bool AllowNativeExtensions { get; private set; }

    public static AppManifest Load(string PathValue)
    {
        string ManifestPath = Path.GetFullPath(PathValue);
        FileInfo FileInfo = new(ManifestPath);
        if (!FileInfo.Exists || FileInfo.Length > 64 * 1024)
            throw new InvalidDataException("Application manifest is missing or too large");
        AppManifest Manifest = JsonSerializer.Deserialize<AppManifest>(File.ReadAllText(ManifestPath))
            ?? throw new InvalidDataException("Application manifest is empty");
        if (Manifest.SchemaVersion != 1 || string.IsNullOrWhiteSpace(Manifest.Script) ||
            Manifest.Capabilities is null || Manifest.Extensions is null)
            throw new InvalidDataException("Unsupported or incomplete application manifest");

        HashSet<string> SeenCapabilities = new(StringComparer.Ordinal);
        foreach (string Capability in Manifest.Capabilities)
        {
            if (!SeenCapabilities.Add(Capability) || Capability != "NativeExtensions")
                throw new InvalidDataException("Unknown or duplicate application capability");
        }
        Manifest.AllowNativeExtensions = SeenCapabilities.Contains("NativeExtensions");

        string DirectoryPath = Path.GetDirectoryName(ManifestPath)
            ?? throw new InvalidDataException("Application manifest has no directory");
        if (Path.IsPathRooted(Manifest.Script))
            throw new InvalidDataException("Application script must use a relative path");
        Manifest.ScriptPath = Path.GetFullPath(Path.Combine(DirectoryPath, Manifest.Script));
        string RelativeScript = Path.GetRelativePath(DirectoryPath, Manifest.ScriptPath);
        if (Path.IsPathRooted(RelativeScript) || RelativeScript == ".." ||
            RelativeScript.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) ||
            !File.Exists(Manifest.ScriptPath))
            throw new InvalidDataException("Application script must exist inside the manifest directory");

        HashSet<string> SeenExtensions = new(StringComparer.OrdinalIgnoreCase);
        foreach (string Extension in Manifest.Extensions)
        {
            if (string.IsNullOrWhiteSpace(Extension) || Path.GetFileName(Extension) != Extension ||
                Extension.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                !Extension.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) ||
                !SeenExtensions.Add(Extension))
                throw new InvalidDataException("Extensions must be unique DLL names beside the manifest");
            Manifest.ExtensionPaths.Add(Path.Combine(DirectoryPath, Extension));
        }
        if (Manifest.ExtensionPaths.Count > 0 && !Manifest.AllowNativeExtensions)
            throw new InvalidDataException("NativeExtensions capability must be declared for extensions");
        return Manifest;
    }
}
