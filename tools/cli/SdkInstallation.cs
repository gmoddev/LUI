using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Lui.Cli;

// This resolver is also consumed by the editor through `doctor --format json`.
internal sealed class SdkInstallation
{
    internal string Root { get; }
    internal string Version { get; }
    internal string Target { get; }
    internal string Digest { get; }
    internal Dictionary<string, string> Components { get; } = new(StringComparer.Ordinal);
    private static readonly string[] Names = ["Cli", "Checker", "Definitions", "Runtime", "PreviewHost", "NativeHost", "ProjectTool"];

    private SdkInstallation(string DirectoryPath)
    {
        Root = Path.GetFullPath(DirectoryPath);
        byte[] Bytes = Read(Path.Combine(Root, "SdkManifest.json"));
        Digest = Hash(Bytes);
        using JsonDocument Document = JsonDocument.Parse(Bytes, new JsonDocumentOptions { MaxDepth = 8 });
        JsonElement Data = Document.RootElement;
        Fields(Data, "SchemaVersion", "SdkVersion", "Target", "Components", "Files");
        if (Data.GetProperty("SchemaVersion").GetInt32() != 1) throw Invalid("UnsupportedSchema");
        Version = Data.GetProperty("SdkVersion").GetString() ?? "";
        Target = Data.GetProperty("Target").GetString() ?? "";
        if (!Regex.IsMatch(Version, @"^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$")) throw Invalid("InvalidVersion");
        string Platform = OperatingSystem.IsWindows() ? "WindowsX64" : OperatingSystem.IsLinux() ? "LinuxX64" : "Unsupported";
        if (Target != Platform || RuntimeInformation.ProcessArchitecture != Architecture.X64) throw Invalid("UnsupportedTarget");
        JsonElement Files = Data.GetProperty("Files");
        if (Files.ValueKind != JsonValueKind.Object) throw Invalid("InvalidInventory");
        Dictionary<string, string> Inventory = new(StringComparer.OrdinalIgnoreCase);
        foreach (JsonProperty File in Files.EnumerateObject())
        {
            string Expected = File.Value.GetString() ?? "";
            if (Inventory.Count >= 20000 || !Inventory.TryAdd(File.Name, Expected) || !Regex.IsMatch(Expected, "^[0-9a-f]{64}$"))
                throw Invalid("InvalidInventory");
            string FilePath = GetPath(File.Name);
            using FileStream Stream = System.IO.File.OpenRead(FilePath);
            if (Stream.Length > 1024L * 1024 * 1024 || Hash(Stream) != Expected) throw Invalid("ComponentMismatch");
        }
        JsonElement Parts = Data.GetProperty("Components");
        Fields(Parts, Names);
        foreach (string Name in Names)
        {
            string Relative = Parts.GetProperty(Name).GetString() ?? "";
            if (!Inventory.ContainsKey(Relative)) throw Invalid("UnlistedComponent");
            Components.Add(Name, GetPath(Relative));
        }
    }

    internal static SdkInstallation? Resolve(string? Selected, string? Manifest, bool Required = false)
    {
        string? DirectoryPath = Selected ?? Environment.GetEnvironmentVariable("LUI_SDK");
        if (string.IsNullOrEmpty(DirectoryPath))
        {
            foreach (string Candidate in new[] { AppContext.BaseDirectory, Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..")) })
                if (File.Exists(Path.Combine(Candidate, "SdkManifest.json"))) { DirectoryPath = Candidate; break; }
        }
        string? Pin = Manifest is null ? null : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(Manifest))!, "LuiToolchain.json");
        if (string.IsNullOrEmpty(DirectoryPath))
        {
            if (Required || (Pin is not null && File.Exists(Pin))) throw Invalid("SdkRequired: choose the project's SDK with --sdk or LUI_SDK");
            return null;
        }
        SdkInstallation Installation = new(DirectoryPath);
        if (Pin is not null && File.Exists(Pin))
        {
            using JsonDocument Document = JsonDocument.Parse(Read(Pin));
            JsonElement Data = Document.RootElement;
            Fields(Data, "SchemaVersion", "SdkVersion", "SdkManifestSha256");
            if (Data.GetProperty("SchemaVersion").GetInt32() != 1 || Data.GetProperty("SdkVersion").GetString() != Installation.Version ||
                Data.GetProperty("SdkManifestSha256").GetString() != Installation.Digest) throw Invalid("PinnedSdkMismatch: select the exact SDK recorded in LuiToolchain.json");
        }
        return Installation;
    }

    internal void Configure(Dictionary<string, string> Options, string Command)
    {
        void Component(string Option, string Name)
        {
            string Expected = Components[Name];
            if (Options.TryGetValue(Option, out string? Override) && !Path.GetFullPath(Override).Equals(Expected,
                OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal)) throw Invalid("ComponentOverrideMismatch");
            Options[Option] = Expected;
        }
        if (Command == "check")
        {
            if (Options.GetValueOrDefault("--mode") == "syntax") Component("--runtime", "Runtime");
            else { Component("--checker", "Checker"); Component("--definitions", "Definitions"); }
        }
        if (Command is "run" or "build") Component("--host", "NativeHost");
        if (Command == "preview") { Component("--host", "PreviewHost"); Component("--runtime", "Runtime"); }
        if (Command is "stage" or "verify" || (Command == "build" && Options.GetValueOrDefault("--mode") == "project")) Component("--project-tool", "ProjectTool");
    }

    internal object Describe() => new { SchemaVersion = 1, Status = "Ready", SdkVersion = Version, Target, SdkManifestSha256 = Digest, SdkRoot = Root, Components,
        Integrity = "LocalConsistencyOnly" };

    internal void WritePin(string DirectoryPath) => File.WriteAllText(Path.Combine(DirectoryPath, "LuiToolchain.json"),
        JsonSerializer.Serialize(new { SchemaVersion = 1, SdkVersion = Version, SdkManifestSha256 = Digest }, new JsonSerializerOptions { WriteIndented = true }) + "\n");

    private string GetPath(string Relative)
    {
        if (string.IsNullOrEmpty(Relative) || Relative.Contains('\\') || Relative.Contains(':') || Relative.StartsWith('/') ||
            Relative.Split('/').Any(Part => Part is "" or "." or ".." || Part.EndsWith('.') || Part.EndsWith(' '))) throw Invalid("InvalidPath");
        string FilePath = Path.Combine(Root, Relative.Replace('/', Path.DirectorySeparatorChar));
        for (string? Current = FilePath; Current is not null; Current = Path.GetDirectoryName(Current))
        {
            if ((File.GetAttributes(Current) & FileAttributes.ReparsePoint) != 0) throw Invalid("LinkNotAllowed");
        }
        return FilePath;
    }

    private static byte[] Read(string FilePath)
    {
        FileInfo Info = new(FilePath);
        if (Info.Length > 4 * 1024 * 1024 || (Info.Attributes & FileAttributes.ReparsePoint) != 0) throw Invalid("InvalidMetadata");
        return File.ReadAllBytes(FilePath);
    }

    private static void Fields(JsonElement Data, params string[] Expected)
    {
        if (Data.ValueKind != JsonValueKind.Object) throw Invalid("InvalidMetadata");
        HashSet<string> Seen = new(StringComparer.Ordinal);
        foreach (JsonProperty Item in Data.EnumerateObject()) if (!Expected.Contains(Item.Name, StringComparer.Ordinal) || !Seen.Add(Item.Name)) throw Invalid("InvalidMetadata");
        if (Seen.Count != Expected.Length) throw Invalid("InvalidMetadata");
    }

    private static string Hash(byte[] Bytes) => Convert.ToHexString(SHA256.HashData(Bytes)).ToLowerInvariant();
    private static string Hash(Stream Stream) => Convert.ToHexString(SHA256.HashData(Stream)).ToLowerInvariant();
    private static InvalidDataException Invalid(string Code) => new("[LUI:SDK] " + Code);
}
