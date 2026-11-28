using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using static Lui.Cli.PackageSource;

namespace Lui.Cli;

internal sealed class PackageRequests
{
    internal const string FileName = "LuiDependencies.json";
    internal static readonly string[] Platforms = ["LinuxX64", "WindowsX64"];
    internal static string CurrentPlatform => RuntimeInformation.ProcessArchitecture == Architecture.X64
        ? OperatingSystem.IsWindows() ? "WindowsX64" : OperatingSystem.IsLinux() ? "LinuxX64" : ""
        : "";

    private readonly JsonObject Document;
    private readonly SortedDictionary<string, string> Required;
    private readonly SortedDictionary<string, string> Optional;
    private readonly SortedDictionary<string, SortedDictionary<string, string>> ByPlatform = new(StringComparer.Ordinal);

    private PackageRequests(JsonObject Document)
    {
        this.Document = Document;
        Shape(Document, ["SchemaVersion", "Dependencies"], ["OptionalDependencies", "PlatformDependencies"]);
        if (Document["SchemaVersion"]?.GetValue<int>() != 1) throw Invalid("InvalidPackageRequests");
        Required = ParseDependencies(Document["Dependencies"], 32);
        Optional = Document.ContainsKey("OptionalDependencies") ? ParseDependencies(Document["OptionalDependencies"], 32)
            : new(StringComparer.Ordinal);
        if (Document.ContainsKey("PlatformDependencies")) {
            if (Document["PlatformDependencies"] is not JsonObject Groups) throw Invalid("InvalidPackageRequests");
            foreach (var Group in Groups) {
                if (!Platforms.Contains(Group.Key, StringComparer.Ordinal)) throw Invalid("UnsupportedPackagePlatform " + Group.Key);
                ByPlatform.Add(Group.Key, ParseDependencies(Group.Value, 32));
            }
        }
        foreach (var Group in ByPlatform.Values.Append(new SortedDictionary<string, string>(StringComparer.Ordinal))) {
            var Seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string Key in Required.Keys.Concat(Optional.Keys).Concat(Group.Keys))
                if (!Seen.Add(Key)) throw Invalid("DuplicatePackageAlias " + Key);
            if (Seen.Count > 32) throw Invalid("PackageLimit");
        }
    }

    internal static PackageRequests Read(string Path) => new(Object(ReadFile(Path, 65536)));

    internal static SortedDictionary<string, string> ParseDependencies(JsonNode? Node, int Maximum)
    {
        if (Node is not JsonObject Entries || Entries.Count > Maximum) throw Invalid("InvalidPackageDependencies");
        var Result = new SortedDictionary<string, string>(StringComparer.Ordinal);
        var Aliases = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach ((string Name, JsonNode? Value) in Entries) {
            if (!Aliases.Add(Alias(Name))) throw Invalid("DuplicatePackageAlias " + Name);
            string Range = Value?.GetValue<string>() ?? throw Invalid("InvalidVersionRange");
            ValidateRange(Range);
            Result.Add(Name, Range);
        }
        return Result;
    }

    internal static HashSet<string> Include(string? Names)
    {
        var Included = new HashSet<string>(StringComparer.Ordinal);
        if (Names is null) return Included;
        foreach (string Name in Names.Split(','))
            if (Included.Count >= 32 || !Included.Add(Alias(Name))) throw Invalid("InvalidOptionalDependencies");
        return Included;
    }

    internal SortedDictionary<string, string> Select(string Platform, IReadOnlySet<string> Included)
    {
        var Result = new SortedDictionary<string, string>(Required, StringComparer.Ordinal);
        foreach (var Item in Optional)
            if (Included.Contains(Item.Key)) Result.Add(Item.Key, Item.Value);
        if (ByPlatform.TryGetValue(Platform, out var Specific))
            foreach (var Item in Specific) Result.Add(Item.Key, Item.Value);
        return Result;
    }

    internal bool IsOptional(string Name) => Optional.ContainsKey(Name);

    internal static void Edit(string Manifest, string Path, string Name, string? Range, bool IsOptional, string? Platform)
    {
        Alias(Name);
        if (Range is not null) ValidateRange(Range);
        if (Platform is not null && (IsOptional || !Platforms.Contains(Platform, StringComparer.Ordinal)))
            throw Invalid("InvalidDependencyGroup");
        string Root = System.IO.Path.GetDirectoryName(Manifest)!;
        if (!string.Equals(System.IO.Path.GetDirectoryName(Path), Root,
                OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal) ||
            !string.Equals(System.IO.Path.GetExtension(Path), ".json", StringComparison.Ordinal) ||
            PackageRestore.ReservedOutput(Path, Manifest))
            throw Invalid("InvalidDependencyRequestsPath");
        VerifyLocalPath(Manifest);
        using var Guard = PackageRestore.OpenGuard(Root);
        bool Exists = File.Exists(Path);
        var Data = Exists ? Read(Path).Document : new JsonObject { ["SchemaVersion"] = 1, ["Dependencies"] = new JsonObject() };
        JsonObject Group(JsonObject Parent, string Key)
        {
            if (Parent[Key] is JsonObject Existing) return Existing;
            var Created = new JsonObject();
            Parent[Key] = Created;
            return Created;
        }
        string Key = IsOptional ? "OptionalDependencies" : "Dependencies";
        JsonObject? Parent = Data;
        if (Platform is not null) {
            Key = Platform;
            Parent = Range is not null ? Group(Data, "PlatformDependencies") : Data["PlatformDependencies"] as JsonObject;
        }
        JsonObject? Target = Range is not null ? Group(Parent!, Key) : Parent?[Key] as JsonObject;
        if (Range is null) {
            if (Target is null || !Target.Remove(Name)) throw Invalid("DependencyNotDeclared " + Name);
        } else Target![Name] = Range;
        _ = new PackageRequests(Data);
        PackageRestore.Publish(Path, Encode(Data), Exists);
    }
}
