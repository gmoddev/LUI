using System.Text;
using System.Text.Json.Nodes;
using static Lui.Cli.PackageSource;

namespace Lui.Cli;

internal static class PackageRestore
{
    internal const string DefaultSdk = "0.1.0-preview.3";
    private const string CachePath = ".lui/Packages";
    internal const string PinsName = "LuiPackages.lock.json";
    private sealed record Pin(string Name, string Version, string Sha256);
    private sealed record Candidate(string Path, Version Version);

    internal static string Restore(string ManifestPath, string RequestsPath, string? Catalog, string OutputName,
        string Sdk, bool Update, bool TrustedNative, bool ReplaceOutput, bool Locked, string PinsFile,
        IReadOnlySet<string> Included)
    {
        string Root = Path.GetDirectoryName(ManifestPath)!;
        PortablePath(OutputName);
        if (OutputName.Contains('/') || !OutputName.EndsWith(".json", StringComparison.Ordinal) ||
            OutputName.Equals(Path.GetFileName(ManifestPath), StringComparison.OrdinalIgnoreCase) ||
            OutputName.Equals(Path.GetFileName(RequestsPath), StringComparison.OrdinalIgnoreCase) ||
            OutputName.Equals(PinsFile, StringComparison.OrdinalIgnoreCase) || ReservedOutput(OutputName, ManifestPath, AllowResolved: true))
            throw Invalid("InvalidRestoreOutput");
        string Output = PathFor(Root, OutputName);
        string PinsPath = PinPath(Root, PinsFile);
        if (PinsPath.Equals(ManifestPath, StringComparison.OrdinalIgnoreCase) ||
            PinsPath.Equals(RequestsPath, StringComparison.OrdinalIgnoreCase)) throw Invalid("ReservedPackageNamespace");
        if (Directory.Exists(Output)) throw Invalid("PackageOutputExists");
        VerifyLocalPath(Root);
        JsonObject Manifest = Object(ReadFile(ManifestPath, 256 * 1024));
        if (Manifest["SchemaVersion"]?.GetValue<int>() != 2 || Manifest["Modules"] is not JsonObject Modules ||
            Modules["Dependencies"] is not JsonArray Existing || Manifest["Assets"] is not JsonArray Assets)
            throw Invalid("RestoreRequiresModularProject");
        if (Existing.Count > 32) throw Invalid("PackageLimit");
        bool ReservedName(JsonNode? Item) => string.Equals(Item?.GetValue<string>(), OutputName, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(Item?.GetValue<string>(), PinsFile, StringComparison.OrdinalIgnoreCase);
        if (Assets.Any(ReservedName))
            throw Invalid("InvalidRestoreOutput");
        if (Manifest["ExtensionDeclarations"] is JsonArray Declarations &&
            Declarations.Any(ReservedName))
            throw Invalid("InvalidRestoreOutput");
        string AppRoot = Text(Modules, "Root");
        if (AppRoot == "." || Overlaps(AppRoot, CachePath)) throw Invalid("RestoreRequiresSeparateSourceRoot");
        PortablePath(AppRoot);
        var ReservedAliases = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (JsonNode? Value in Existing) {
            var Item = Value as JsonObject ?? throw Invalid("InvalidExistingModules");
            string DependencyRoot = Text(Item, "Root");
            PortablePath(DependencyRoot);
            if (Overlaps(DependencyRoot, CachePath) || !ReservedAliases.Add(Alias(Text(Item, "Alias"))))
                throw Invalid("ReservedPackageNamespace");
        }
        var NativePins = new JsonArray();
        string? ExistingLock = Modules["Lock"]?.GetValue<string>();
        if (ExistingLock is not null) {
            if (OutputName.Equals(ExistingLock, StringComparison.OrdinalIgnoreCase) ||
                PinsFile.Equals(ExistingLock, StringComparison.OrdinalIgnoreCase)) throw Invalid("InvalidRestoreOutput");
            var InputLock = Object(ReadFile(PathFor(Root, ExistingLock), 1024 * 1024));
            if (InputLock["SchemaVersion"]?.GetValue<int>() != 1 || InputLock["Packages"] is not JsonArray Array ||
                Array.Count != Existing.Count) throw Invalid("InvalidExistingModuleLock");
            NativePins = (JsonArray)Array.DeepClone();
        } else if (Existing.Count != 0) throw Invalid("MissingExistingModuleLock");

        using var Guard = OpenGuard(Root);
        var Requests = PackageRequests.Read(RequestsPath);
        string Platform = PackageRequests.CurrentPlatform;
        var DirectRanges = Requests.Select(Platform, Included);
        foreach (string Name in DirectRanges.Keys)
            if (!ReservedAliases.Add(Name)) throw Invalid("DuplicatePackageAlias " + Name);
        string Inputs = InputsFor(Sdk, DirectRanges, Included);
        string Storage = PathFor(Root, CachePath);
        var ExistingPins = ReadPins(PinsPath);
        if (Locked && ExistingPins is null) throw Invalid("PackageLockMissing (run package restore first)");
        var Resolver = new Resolver(Catalog, Sdk, Platform, Included, 32 - Existing.Count);
        SortedDictionary<string, PackageSource> Selected;
        string Key;
        if (ExistingPins is not null && !Update) {
            if (ExistingPins.Inputs != Inputs)
                throw Invalid("PackageLockChanged (restore with --update true to select versions)");
            Key = ExistingPins.Cache;
            Selected = ReadSelection(Storage, ExistingPins, Resolver);
        } else {
            Selected = Resolver.Resolve(DirectRanges);
            Key = CacheKey(Selected);
        }

        ValidateGraph(Selected, DirectRanges, Included);
        foreach (string Feature in Included)
            if (!Requests.IsOptional(Feature) && Selected.Values.All(Package => !Package.OptionalRequirements.ContainsKey(Feature)))
                throw Invalid("UnknownOptionalDependency " + Feature);
        string[] Granted = Strings(Manifest["Capabilities"], 32);
        if (Selected.Count + Existing.Count > 32 ||
            Selected.Values.Sum(Item => Item.ActiveModules(Included).Count()) + Strings(Modules["Files"], 256).Length +
            Existing.Sum(Item => Strings(Item?["Files"], 256).Length) > 256) throw Invalid("PackageLimit");
        var UsedAliases = new HashSet<string>(Existing.Select(Item => Text((JsonObject)Item!, "Alias")),
            StringComparer.OrdinalIgnoreCase);
        string Installed = PathFor(Storage, Key);
        var ProviderDeclarations = new SortedDictionary<string, byte[]>(StringComparer.Ordinal);
        var ProviderNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (PackageSource Package in Selected.Values) {
            if (!UsedAliases.Add(Package.Name)) throw Invalid("DuplicatePackageAlias " + Package.Name);
            if (Package.Sdk != "*" && Package.Sdk != Sdk) throw Invalid("SdkIncompatible " + Package.Name);
            if (!Package.Supports(Platform)) throw Invalid("UnsupportedPackagePlatform " + Package.Name);
            string[] ActiveModules = Package.ActiveModules(Included).ToArray();
            foreach (string Capability in Package.Capabilities)
                if (!Granted.Contains(Capability, StringComparer.Ordinal))
                    throw Invalid("CapabilityRequired " + Package.Name + " (" + Capability + ")");
            if (Package.TrustedNative && (!TrustedNative || !Granted.Contains("NativeExtensions", StringComparer.Ordinal) ||
                Manifest["Extensions"] is not JsonArray Extensions || Extensions.Count == 0))
                throw Invalid("TrustedNativeRequired " + Package.Name);
            string PackageRoot = CachePath + "/" + Key + "/" + Package.Name;
            if (Package.NativeProviders.Length != 0) {
                string[] Enabled = Strings(Manifest["Extensions"], MaxProviders);
                foreach (var Provider in Package.NativeProviders) {
                    if (!Enabled.Contains(Provider.Name, StringComparer.Ordinal))
                        throw Invalid("PackageProviderRequired " + Package.Name + " (" + Provider.Name + ")");
                    if (!ProviderNames.Add(Provider.Name)) throw Invalid("DuplicatePackageProvider " + Provider.Name);
                    byte[] Declaration = SelectProvider(Provider, PackageRoot, Platform);
                    ProviderDeclarations.Add(CachePath + "/NativeDeclaration-" + Hash(Declaration) + ".json", Declaration);
                }
            }
            Existing.Add(new JsonObject {
                ["Alias"] = Package.Name, ["Root"] = PackageRoot,
                ["Files"] = JsonSerializerNode(ActiveModules), ["Requires"] = JsonSerializerNode(Package.Dependencies(Included).Select(Item => Item.Key))
            });
            NativePins.Add(new JsonObject {
                ["Alias"] = Package.Name,
                ["Provenance"] = new JsonObject { ["Kind"] = "LocalVendor", ["Id"] = Package.Version + ":" + Package.Digest },
                ["Files"] = JsonSerializerNode(ActiveModules.Select(File => new { Path = File, Sha256 = Hash(Package.Content[File]) }))
            });
        }
        if (ProviderDeclarations.Count != 0) {
            if (Manifest["ExtensionDeclarations"] is not JsonArray Paths ||
                Paths.Count + ProviderDeclarations.Count > MaxProviders) throw Invalid("PackageProviderLimit");
            foreach (string Path in Strings(Paths, MaxProviders)) {
                JsonObject Authored = Object(ReadFile(PathFor(Root, Path), 256 * 1024));
                if (Authored["Provider"]?.GetValue<string>() == "Native" &&
                    Authored["Extension"] is JsonObject Extension &&
                    !ProviderNames.Add(Text(Extension, "Name"))) throw Invalid("DuplicatePackageProvider " + Text(Extension, "Name"));
            }
            foreach (string Path in ProviderDeclarations.Keys) Paths.Add(Path);
        }
        string[] Requires = Strings(Modules["Requires"], 32).Concat(DirectRanges.Keys)
            .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
        if (Requires.Length > 32) throw Invalid("PackageLimit");
        Modules["Requires"] = JsonSerializerNode(Requires);
        byte[] NativeLock = Encode(new { SchemaVersion = 1, Packages = NativePins });
        string LockPath = CachePath + "/ModuleLock-" + Hash(NativeLock) + ".json";
        if (NativePins.Count != 0) Modules["Lock"] = LockPath;
        var Notices = new StringBuilder();
        foreach (PackageSource Package in Selected.Values) {
            Notices.Append(Package.Name).Append(' ').Append(Package.Version).Append(" — ").Append(Package.License).Append('\n');
            if (Package.LicenseFile is { } Notice) Notices.Append(Encoding.UTF8.GetString(Package.Content[Notice])).Append('\n');
            if (Notices.Length > 1024 * 1024) throw Invalid("PackageNoticesLimit");
        }
        byte[] NoticeBytes = Encoding.UTF8.GetBytes(Notices.ToString());
        string NoticeName = "LuiPackageNotices-" + Hash(NoticeBytes) + ".txt";
        if (Selected.Count != 0 && (Assets.Count >= 1024 ||
            Assets.Any(Asset => string.Equals(Asset?.GetValue<string>(), NoticeName, StringComparison.OrdinalIgnoreCase))))
            throw Invalid("PackageNoticesCollision");
        if (Selected.Count != 0) Assets.Add(NoticeName);
        byte[] NewPins = Encode(new { SchemaVersion = 1, Inputs, Cache = Key, Packages = PinsFor(Selected) });
        byte[] Resolved = Encode(Manifest);
        if (File.Exists(Output) && !ReadFile(Output, 256 * 1024).AsSpan().SequenceEqual(Resolved) && !ReplaceOutput)
            throw Invalid("PackageOutputExists (choose another --output or pass --replace-output true)");

        if (Selected.Count != 0 && !Directory.Exists(Installed)) {
            string Stage = PathFor(Storage, Guid.NewGuid().ToString("N") + ".pending");
            EnsureDirectory(Stage);
            try {
                foreach (PackageSource Package in Selected.Values)
                    Package.WriteDirectory(PathFor(Stage, Package.Name));
                Directory.Move(Stage, Installed);
            } finally {
                if (Directory.Exists(Stage)) {
                    VerifyLocalPath(Stage);
                    Directory.Delete(Stage, recursive: true);
                }
            }
        } else {
            foreach (PackageSource Package in Selected.Values) {
                var Cached = Read(PathFor(Installed, Package.Name));
                if (Cached.Digest != Package.Digest) throw Invalid("PackageIntegrityMismatch " + Package.Name);
            }
        }
        if (NativePins.Count != 0) Publish(PathFor(Root, LockPath), NativeLock, false);
        if (Selected.Count != 0) Publish(PathFor(Root, NoticeName), NoticeBytes, false);
        foreach (var Declaration in ProviderDeclarations)
            Publish(PathFor(Root, Declaration.Key), Declaration.Value, false);
        Publish(PinsPath, NewPins, ExistingPins is not null && Update);
        Publish(Output, Resolved, ReplaceOutput);
        return Output;
    }

    private static byte[] SelectProvider(NativeProvider Provider, string PackageRoot, string Platform)
    {
        var Declaration = (JsonObject)Provider.Declaration.DeepClone();
        var Extension = (JsonObject)Declaration["Extension"]!;
        var Bindings = (JsonArray)Extension["Bindings"]!;
        var Binding = Bindings.OfType<JsonObject>().SingleOrDefault(Item => Text(Item, "Platform") == Platform);
        if (Binding is null) throw Invalid("UnsupportedPackagePlatform " + Provider.Name);
        string NativePath = PackageRoot + "/" + Text(Binding, "Path");
        if (NativePath.Length > 512) throw Invalid("InvalidPackagePath");
        Binding["Path"] = NativePath;
        Extension["Bindings"] = new JsonArray(Binding.DeepClone());
        byte[] Bytes = Encode(Declaration);
        if (Bytes.Length > 256 * 1024) throw Invalid("PackageProviderLimit");
        return Bytes;
    }

    private static JsonNode? JsonSerializerNode<T>(T Value) => System.Text.Json.JsonSerializer.SerializeToNode(Value);
    private static string CacheKey(SortedDictionary<string, PackageSource> Selected) => Hash(Encode(PinsFor(Selected)));
    private static Pin[] PinsFor(SortedDictionary<string, PackageSource> Selected) =>
        Selected.Values.Select(Package => new Pin(Package.Name, Package.Version.ToString(3), Package.Digest)).ToArray();
    private static string RequireDigest(string Value) => Value.Length == 64 && Value.All(Byte => Byte is >= '0' and <= '9' or >= 'a' and <= 'f')
        ? Value : throw Invalid("InvalidPackageDigest");
    private static bool Overlaps(string Left, string Right) =>
        Left.Equals(Right, StringComparison.OrdinalIgnoreCase) || Left.StartsWith(Right + "/", StringComparison.OrdinalIgnoreCase) ||
        Right.StartsWith(Left + "/", StringComparison.OrdinalIgnoreCase);

    internal static bool ReservedOutput(string Path, string Manifest, bool AllowResolved = false)
    {
        string Leaf = System.IO.Path.GetFileName(Path);
        return Leaf.Equals(System.IO.Path.GetFileName(Manifest), StringComparison.OrdinalIgnoreCase) ||
            Leaf.Equals("LuiToolchain.json", StringComparison.OrdinalIgnoreCase) ||
            Leaf.Equals("LuiPackage.json", StringComparison.OrdinalIgnoreCase) ||
            Leaf.EndsWith(".lock.json", StringComparison.OrdinalIgnoreCase) ||
            (!AllowResolved && Leaf.Equals("lui.resolved.json", StringComparison.OrdinalIgnoreCase));
    }

    private static string PinPath(string Root, string Name)
    {
        PortablePath(Name);
        if (Name.Contains('/') || !Name.EndsWith(".lock.json", StringComparison.Ordinal))
            throw Invalid("InvalidPackageLockPath");
        return PathFor(Root, Name);
    }

    internal static FileStream OpenGuard(string Root)
    {
        VerifyLocalPath(Root);
        string Storage = PathFor(Root, CachePath);
        EnsureDirectory(Storage);
        string Path = PathFor(Storage, "Restore.guard");
        if (System.IO.Path.Exists(Path)) VerifyLocalPath(Path);
        try { return new FileStream(Path, FileMode.OpenOrCreate, FileAccess.Write, FileShare.None); }
        catch (IOException) { throw Invalid("PackageRestoreBusy (retry after the other package operation finishes)"); }
    }

    internal static void Publish(string Path, byte[] Bytes, bool Replace)
    {
        if (File.Exists(Path)) {
            if (ReadFile(Path, 1024 * 1024).AsSpan().SequenceEqual(Bytes)) return;
            if (!Replace) throw Invalid("PackageIntegrityMismatch");
        }
        string Temp = Path + "." + Guid.NewGuid().ToString("N") + ".pending";
        WriteNew(Temp, Bytes);
        try { File.Move(Temp, Path, Replace); }
        finally { File.Delete(Temp); }
    }

    private sealed record PackageLock(string Inputs, string Cache, Pin[] Packages);

    private static PackageLock? ReadPins(string Path)
    {
        if (!File.Exists(Path)) return null;
        var Document = Object(ReadFile(Path, 65536));
        Shape(Document, ["SchemaVersion", "Inputs", "Cache", "Packages"]);
        if (Document["SchemaVersion"]?.GetValue<int>() != 1 ||
            Document["Packages"] is not JsonArray Entries || Entries.Count > 32) throw Invalid("InvalidPackageLock");
        var Names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        Pin[] Pins = Entries.Select(Value =>
        {
            var Entry = Value as JsonObject ?? throw Invalid("InvalidPackageLock");
            Shape(Entry, ["Name", "Version", "Sha256"]);
            string Name = Alias(Text(Entry, "Name"));
            if (!Names.Add(Name)) throw Invalid("InvalidPackageLock");
            return new Pin(Name, ParseVersion(Text(Entry, "Version")).ToString(3), RequireDigest(Text(Entry, "Sha256")));
        }).ToArray();
        return new PackageLock(RequireDigest(Text(Document, "Inputs")), RequireDigest(Text(Document, "Cache")), Pins);
    }

    private static string InputsFor(string Sdk, SortedDictionary<string, string> Dependencies, IReadOnlySet<string> Included) =>
        Included.Count == 0 ? Hash(Encode(new { Sdk, Dependencies })) :
            Hash(Encode(new { Sdk, Dependencies, Included = Included.Order(StringComparer.Ordinal) }));

    private static SortedDictionary<string, PackageSource> ReadSelection(string Storage, PackageLock Lock, Resolver Resolver)
    {
        var Selected = new SortedDictionary<string, PackageSource>(StringComparer.Ordinal);
        foreach (var Pin in Lock.Packages) {
            string Cached = PathFor(Storage, Lock.Cache + "/" + Pin.Name);
            var Version = ParseVersion(Pin.Version);
            var Package = Directory.Exists(Cached) ? Resolver.Load(Cached, Pin.Name, Version) : Resolver.LoadPinned(Pin.Name, Version);
            if (Package.Digest != Pin.Sha256) throw Invalid("PackageIntegrityMismatch " + Pin.Name);
            Selected.Add(Pin.Name, Package);
        }
        if (CacheKey(Selected) != Lock.Cache) throw Invalid("InvalidPackageLock");
        return Selected;
    }

    internal static object Inspect(string Manifest, string RequestsPath, string PinsFile, string Sdk, IReadOnlySet<string> Included)
    {
        string Root = Path.GetDirectoryName(Manifest)!;
        using var Guard = OpenGuard(Root);
        var Lock = ReadPins(PinPath(Root, PinsFile)) ?? throw Invalid("PackageLockMissing (run package restore first)");
        string Platform = PackageRequests.CurrentPlatform;
        var Requests = PackageRequests.Read(RequestsPath).Select(Platform, Included);
        if (Lock.Inputs != InputsFor(Sdk, Requests, Included))
            throw Invalid("PackageLockChanged (restore with --update true to select versions)");
        var Selected = ReadSelection(PathFor(Root, CachePath), Lock, new Resolver(null, Sdk, Platform, Included, 32));
        ValidateGraph(Selected, Requests, Included);
        return new { SchemaVersion = 1, Sdk, Platform, Direct = Requests,
            Packages = Selected.Values.Select(Package => new { Package.Name, Version = Package.Version.ToString(3),
                Sha256 = Package.Digest, Dependencies = Package.Dependencies(Included).ToDictionary(),
                Package.Capabilities, Package.TrustedNative, Supported = Package.Supports(Platform),
                Providers = Package.NativeProviders.Select(Provider => Provider.Name) }) };
    }

    private static void ValidateGraph(SortedDictionary<string, PackageSource> Selected,
        SortedDictionary<string, string> Direct, IReadOnlySet<string> Included)
    {
        var Active = new HashSet<string>(StringComparer.Ordinal);
        var Visited = new HashSet<string>(StringComparer.Ordinal);
        void Visit(string Name, string Range) {
            if (!Selected.TryGetValue(Name, out var Package) || !Matches(Range, Package.Version))
                throw Invalid("VersionConflict " + Name);
            if (!Active.Add(Name)) throw Invalid("DependencyCycle " + Name);
            if (Visited.Add(Name)) foreach (var Item in Package.Dependencies(Included)) Visit(Item.Key, Item.Value);
            Active.Remove(Name);
        }
        foreach (var Item in Direct) Visit(Item.Key, Item.Value);
        if (Visited.Count != Selected.Count) throw Invalid("UnreachablePackage");
    }

    private sealed class Resolver(string? Catalog, string Sdk, string Platform, IReadOnlySet<string> Included, int Limit)
    {
        private readonly Dictionary<string, Candidate[]> Indices = new(StringComparer.Ordinal);
        private readonly Dictionary<string, PackageSource> Contents = new(StringComparer.Ordinal);
        private int Attempts, LoadedBytes;
        private string Conflict = "UnresolvedDependency";

        internal SortedDictionary<string, PackageSource> Resolve(SortedDictionary<string, string> Direct)
        {
            bool Search(SortedDictionary<string, PackageSource> Selection, Dictionary<string, List<string>> Inputs,
                out SortedDictionary<string, PackageSource> Result) {
                Result = Selection;
                if (++Attempts > 1024) throw Invalid("PackageResolutionLimit");
                if (Inputs.Count > Limit) { Conflict = "PackageResolutionLimit"; return false; }
                foreach (var Item in Selection)
                    if (Inputs[Item.Key].Any(Range => !Matches(Range, Item.Value.Version))) {
                        Conflict = "VersionConflict " + Item.Key;
                        return false;
                    }
                string? Name = Inputs.Keys.Order(StringComparer.Ordinal).FirstOrDefault(Key => !Selection.ContainsKey(Key));
                if (Name is null) {
                    try { ValidateGraph(Selection, Direct, Included); return true; }
                    catch (InvalidDataException Error) { Conflict = Error.Message.Replace("[LUI:Packages] ", "", StringComparison.Ordinal); return false; }
                }
                string Skipped = "VersionConflict " + Name + " (requires " + string.Join(", ", Inputs[Name].Distinct(StringComparer.Ordinal)) + ")";
                foreach (var Candidate in Candidates(Name)) {
                    if (Inputs[Name].Any(Range => !Matches(Range, Candidate.Version))) continue;
                    var Package = Load(Candidate.Path, Name, Candidate.Version);
                    if (Package.Sdk != "*" && Package.Sdk != Sdk) { Skipped = "SdkIncompatible " + Name; continue; }
                    if (!Package.Supports(Platform)) { Skipped = "UnsupportedPackagePlatform " + Name; continue; }
                    var Next = new SortedDictionary<string, PackageSource>(Selection, StringComparer.Ordinal) { [Name] = Package };
                    var Needs = Inputs.ToDictionary(Item => Item.Key, Item => Item.Value.ToList(), StringComparer.Ordinal);
                    foreach ((string Dependency, string Range) in Package.Dependencies(Included)) {
                        if (!Needs.TryGetValue(Dependency, out var Ranges)) Needs.Add(Dependency, Ranges = []);
                        Ranges.Add(Range);
                    }
                    if (Search(Next, Needs, out Result)) return true;
                }
                if (Conflict == "UnresolvedDependency") Conflict = Skipped;
                return false;
            }
            var Roots = Direct.ToDictionary(Item => Item.Key, Item => new List<string> { Item.Value }, StringComparer.Ordinal);
            return Search(new(StringComparer.Ordinal), Roots, out var Selected) ? Selected : throw Invalid(Conflict);
        }

        internal PackageSource LoadPinned(string Name, Version Version)
        {
            var Candidate = Candidates(Name).SingleOrDefault(Value => Value.Version == Version);
            if (Candidate is null) throw Invalid("PackageCacheMissing " + Name);
            return Load(Candidate.Path, Name, Version);
        }

        internal PackageSource Load(string Path, string Name, Version Version)
        {
            if (!Contents.TryGetValue(Path, out var Package)) {
                Package = Read(Path);
                LoadedBytes += Package.Metadata.Length + Package.Content.Values.Sum(Bytes => Bytes.Length);
                if (LoadedBytes > 64 * 1024 * 1024) throw Invalid("PackageResolutionLimit");
                Contents.Add(Path, Package);
            }
            if (!Package.Name.Equals(Name, StringComparison.Ordinal) || Package.Version != Version)
                throw Invalid("PackageIdentityMismatch " + Name);
            return Package;
        }

        private Candidate[] Candidates(string Name)
        {
            if (Indices.TryGetValue(Name, out var Entries)) return Entries;
            if (Catalog is null) return Indices[Name] = [];
            string Root = PathFor(Catalog, Alias(Name));
            if (!Directory.Exists(Root)) return Indices[Name] = [];
            VerifyLocalPath(Root);
            var Found = new Dictionary<Version, Candidate>();
            int Pending = 0;
            foreach (string Entry in Directory.EnumerateFileSystemEntries(Root)) {
                string Leaf = Path.GetFileName(Entry);
                if (IsPendingPackageArchive(Leaf)) {
                    if (++Pending > 128) throw Invalid("PackageResolutionLimit");
                    continue;
                }
                bool Archive = Leaf.EndsWith(".luipkg", StringComparison.Ordinal);
                if (Found.Count >= 128) throw Invalid("PackageResolutionLimit");
                var Version = ParseVersion(Archive ? Leaf[..^7] : Leaf);
                if (Archive ? !File.Exists(Entry) : !Directory.Exists(Entry)) throw Invalid("InvalidCatalogEntry");
                if (!Found.TryAdd(Version, new Candidate(Entry, Version))) throw Invalid("AmbiguousPackageVersion " + Name);
            }
            return Indices[Name] = Found.Values.OrderByDescending(Item => Item.Version).ToArray();
        }
    }
}
