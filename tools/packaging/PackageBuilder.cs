using Lui.WinUI;

namespace Lui.Packaging;

internal static class PackageBuilder
{
    public static void Stage(string ManifestPathValue, string HostDirectoryValue,
        string OutputDirectoryValue, string? ExtensionDirectoryValue)
    {
        AppManifest Manifest = AppManifest.Load(ManifestPathValue);
        string ManifestPath = Path.GetFullPath(ManifestPathValue);
        string HostDirectory = Path.GetFullPath(HostDirectoryValue);
        string OutputDirectory = Path.GetFullPath(OutputDirectoryValue);
        string ExtensionDirectory = ExtensionDirectoryValue is null
            ? Path.GetDirectoryName(ManifestPath)!
            : Path.GetFullPath(ExtensionDirectoryValue);
        if (!File.Exists(Path.Combine(HostDirectory, "Lui.WinUI.exe")) ||
            !File.Exists(Path.Combine(HostDirectory, "LuiRuntime.dll")))
            throw new InvalidDataException("Published Windows host and native runtime are required");
        if (Directory.Exists(OutputDirectory) && Directory.EnumerateFileSystemEntries(OutputDirectory).Any())
            throw new InvalidDataException("Package output directory must be empty");

        List<(string Source, string Relative)> Files = new();
        foreach (string HostFile in Directory.EnumerateFiles(HostDirectory, "*", SearchOption.AllDirectories))
        {
            if (Path.GetExtension(HostFile).Equals(".pdb", StringComparison.OrdinalIgnoreCase)) continue;
            Files.Add((HostFile, Path.GetRelativePath(HostDirectory, HostFile)));
        }
        Files.Add((ManifestPath, Path.GetFileName(ManifestPath)));
        Files.Add((Manifest.ScriptPath, Path.GetRelativePath(Path.GetDirectoryName(ManifestPath)!, Manifest.ScriptPath)));
        foreach (KeyValuePair<string, string> Asset in Manifest.AssetPaths)
            Files.Add((Asset.Value, Asset.Key));
        foreach (string ExtensionPath in Manifest.ExtensionPaths)
        {
            string ExtensionFile = Path.Combine(ExtensionDirectory, Path.GetFileName(ExtensionPath));
            if (!File.Exists(ExtensionFile) ||
                (File.GetAttributes(ExtensionFile) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Declared extension is missing or linked: " + Path.GetFileName(ExtensionPath));
            Files.Add((ExtensionFile, Path.GetFileName(ExtensionPath)));
        }
        HashSet<string> Names = new(StringComparer.OrdinalIgnoreCase);
        foreach ((string Source, string Relative) in Files)
        {
            if (!Names.Add(Relative) || Path.IsPathRooted(Relative) || Relative == ".." ||
                Relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal))
                throw new InvalidDataException("Package file collision or path escape: " + Relative);
        }
        Directory.CreateDirectory(OutputDirectory);
        foreach ((string Source, string Relative) in Files)
        {
            string Target = Path.Combine(OutputDirectory, Relative);
            Directory.CreateDirectory(Path.GetDirectoryName(Target)!);
            File.Copy(Source, Target, overwrite: false);
        }
        Console.WriteLine($"[LUI:Package] Staged {Files.Count} files at {OutputDirectory}");
    }
}
