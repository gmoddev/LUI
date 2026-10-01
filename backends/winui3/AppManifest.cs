using System.Text.Json;

namespace Lui.WinUI;

internal sealed class AppManifest
{
    internal sealed class SandboxConfiguration
    {
        public ulong MaxMemoryBytes { get; set; }
        public ulong MaxInterrupts { get; set; }
    }

    public int SchemaVersion { get; set; }
    public string Script { get; set; } = string.Empty;
    public List<string> Capabilities { get; set; } = new();
    public List<string> Extensions { get; set; } = new();
    public List<string> Assets { get; set; } = new();
    public SandboxConfiguration? Sandbox { get; set; }

    public string ScriptPath { get; private set; } = string.Empty;
    public List<string> ExtensionPaths { get; private set; } = new();
    public Dictionary<string, string> AssetPaths { get; private set; } = new(StringComparer.OrdinalIgnoreCase);
    public bool AllowNativeExtensions { get; private set; }
    public bool AllowHostServices { get; private set; }
    public bool AllowClipboard { get; private set; }
    public bool AllowDialogs { get; private set; }

    public static AppManifest Load(string PathValue)
    {
        string ManifestPath = Path.GetFullPath(PathValue);
        FileInfo FileInfo = new(ManifestPath);
        if (!FileInfo.Exists || FileInfo.Length > 64 * 1024)
            throw new InvalidDataException("Application manifest is missing or too large");
        AppManifest Manifest = JsonSerializer.Deserialize<AppManifest>(File.ReadAllText(ManifestPath))
            ?? throw new InvalidDataException("Application manifest is empty");
        if (Manifest.SchemaVersion != 1 || string.IsNullOrWhiteSpace(Manifest.Script) ||
            Manifest.Capabilities is null || Manifest.Extensions is null || Manifest.Assets is null)
            throw new InvalidDataException("Unsupported or incomplete application manifest");

        HashSet<string> SeenCapabilities = new(StringComparer.Ordinal);
        foreach (string Capability in Manifest.Capabilities)
        {
            if (!SeenCapabilities.Add(Capability) || Capability is not
                ("NativeExtensions" or "HostServices" or "Clipboard" or "Dialogs"))
                throw new InvalidDataException("Unknown or duplicate application capability");
        }
        Manifest.AllowNativeExtensions = SeenCapabilities.Contains("NativeExtensions");
        Manifest.AllowHostServices = SeenCapabilities.Contains("HostServices");
        Manifest.AllowClipboard = SeenCapabilities.Contains("Clipboard");
        Manifest.AllowDialogs = SeenCapabilities.Contains("Dialogs");
        if (Manifest.Sandbox is not null &&
            (Manifest.Sandbox.MaxMemoryBytes < 1024 * 1024 || Manifest.Sandbox.MaxInterrupts == 0 ||
             Manifest.AllowNativeExtensions))
            throw new InvalidDataException("Sandbox limits are invalid or grant native extensions");

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
        string ScriptComponent = DirectoryPath;
        foreach (string Part in RelativeScript.Split(
            [Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar],
            StringSplitOptions.RemoveEmptyEntries))
        {
            ScriptComponent = Path.Combine(ScriptComponent, Part);
            if ((File.GetAttributes(ScriptComponent) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Application script cannot traverse a linked path");
        }

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
        foreach (string Asset in Manifest.Assets)
        {
            if (string.IsNullOrWhiteSpace(Asset) || Path.GetFileName(Asset) != Asset ||
                Asset.Length > 128 || Asset is "." or ".." ||
                Asset.Any(Character => !char.IsAsciiLetterOrDigit(Character) && Character is not ('.' or '_' or '-')) ||
                !Manifest.AssetPaths.TryAdd(Asset, Path.Combine(DirectoryPath, Asset)))
                throw new InvalidDataException("Assets must have unique filenames beside the manifest");
            string AssetPath = Manifest.AssetPaths[Asset];
            if (!File.Exists(AssetPath) || (File.GetAttributes(AssetPath) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Declared asset is missing or linked outside the package");
        }
        return Manifest;
    }
}
