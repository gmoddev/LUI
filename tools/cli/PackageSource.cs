using System.Buffers.Binary;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Microsoft.Win32.SafeHandles;

namespace Lui.Cli;

internal sealed class PackageSource
{
    internal const string Descriptor = "LuiPackage.json";
    internal const int MaxFileBytes = 8 * 1024 * 1024;
    internal const int MaxPackageBytes = 32 * 1024 * 1024;
    internal const int MaxProviders = 16;
    private const int MaxEntries = 194 + MaxProviders * 3;
    internal static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private static readonly Regex AliasPattern = new(@"\A[A-Za-z][A-Za-z0-9_]{0,63}\z", RegexOptions.CultureInvariant);
    private static readonly Regex PathPattern = new(@"\A[A-Za-z0-9_.-]+\z", RegexOptions.CultureInvariant);
    private static readonly Regex SdkPattern = new(@"\A[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?\z", RegexOptions.CultureInvariant);
    private static readonly Regex PendingArchivePattern = new(@"\A[0-9]+\.[0-9]+\.[0-9]+\.luipkg\.[0-9a-f]{32}\.pending\z", RegexOptions.CultureInvariant);
    internal readonly string Name, Sdk, License, Digest;
    internal readonly string? LicenseFile;
    internal readonly Version Version;
    internal readonly byte[] Metadata;
    internal readonly string[] Modules, Types, Capabilities;
    internal readonly string[] Platforms;
    internal readonly bool TrustedNative;
    internal readonly SortedDictionary<string, string> Requirements, OptionalRequirements;
    internal readonly SortedDictionary<string, string[]> FeatureModules = new(StringComparer.Ordinal);
    internal readonly SortedDictionary<string, byte[]> Content = new(StringComparer.Ordinal);
    internal readonly NativeProvider[] NativeProviders;

    internal sealed record NativeProvider(string Name, JsonObject Declaration);

    private PackageSource(byte[] Metadata, Func<string, int, byte[]> Read)
    {
        this.Metadata = Metadata;
        JsonObject Root = Object(Metadata);
        Shape(Root, ["SchemaVersion", "Name", "Version", "Sdk", "License", "Modules", "Dependencies"],
            ["LicenseFile", "Types", "Capabilities", "TrustedNative", "NativeProviders", "Platforms", "OptionalDependencies", "FeatureModules"]);
        if (Root["SchemaVersion"]?.GetValue<int>() != 1) throw Invalid("UnsupportedPackageSchema");
        Name = Alias(Text(Root, "Name"));
        Version = ParseVersion(Text(Root, "Version"));
        Sdk = SdkVersion(Text(Root, "Sdk"));
        License = Text(Root, "License");
        if (License.Length > 128) throw Invalid("InvalidPackageMetadata");
        Modules = Strings(Root["Modules"], 128);
        if (Modules.Length == 0 || Modules.Any(Path => !IsModule(Path))) throw Invalid("InvalidPackageModules");
        Types = Root.ContainsKey("Types") ? Strings(Root["Types"], 64) : [];
        if (Types.Any(Path => !Path.EndsWith(".d.luau", StringComparison.Ordinal))) throw Invalid("InvalidPackageTypes");
        Capabilities = Root.ContainsKey("Capabilities") ? Strings(Root["Capabilities"], 32) : [];
        TrustedNative = Root["TrustedNative"]?.GetValue<bool>() ?? false;
        Requirements = PackageRequests.ParseDependencies(Root["Dependencies"], 31);
        OptionalRequirements = Root.ContainsKey("OptionalDependencies")
            ? PackageRequests.ParseDependencies(Root["OptionalDependencies"], 31) : new(StringComparer.Ordinal);
        if (Requirements.Keys.Concat(OptionalRequirements.Keys).Distinct(StringComparer.OrdinalIgnoreCase).Count() !=
            Requirements.Count + OptionalRequirements.Count)
            throw Invalid("DuplicatePackageAlias");
        Platforms = Root.ContainsKey("Platforms") ? Strings(Root["Platforms"], 2) : [];
        if (Root.ContainsKey("Platforms") && (Platforms.Length == 0 || Platforms.Except(PackageRequests.Platforms).Any()))
            throw Invalid("UnsupportedPackagePlatform " + Name);
        if (Root.ContainsKey("FeatureModules")) {
            if (Root["FeatureModules"] is not JsonObject Features) throw Invalid("InvalidPackageFeatureModules");
            foreach (var Feature in Features) {
                if (!OptionalRequirements.ContainsKey(Feature.Key)) throw Invalid("UnknownOptionalDependency " + Feature.Key);
                string[] FeaturePaths = Strings(Feature.Value, 128);
                if (FeaturePaths.Length == 0 || FeaturePaths.Any(Path => !IsModule(Path))) throw Invalid("InvalidPackageModules");
                FeatureModules.Add(Feature.Key, FeaturePaths);
            }
            if (Modules.Length + FeatureModules.Values.Sum(Files => Files.Length) > 128) throw Invalid("PackageLimit");
        }

        IEnumerable<string> Files = Modules.Concat(Types).Concat(FeatureModules.Values.SelectMany(Paths => Paths));
        if (Root.ContainsKey("LicenseFile")) {
            LicenseFile = Text(Root, "LicenseFile");
            Files = Files.Append(LicenseFile);
        }
        var Paths = new Dictionary<string, (string Path, bool Directory)>(StringComparer.OrdinalIgnoreCase);
        AddPath(Paths, Descriptor);
        int Bytes = Metadata.Length;
        byte[] ReadMember(string Path, int Maximum)
        {
            AddPath(Paths, Path);
            byte[] Data = Read(Path, Math.Min(Maximum, MaxPackageBytes - Bytes));
            Content.Add(Path, Data);
            Bytes += Data.Length;
            return Data;
        }
        foreach (string Path in Files.Order(StringComparer.Ordinal)) {
            byte[] Data = ReadMember(Path, MaxFileBytes);
            string Source = Utf8.GetString(Data);
            if (Source.Contains('\0') || Source.StartsWith('\ufeff')) throw Invalid("InvalidPackageText");
        }
        string[] Sidecars = Root.ContainsKey("NativeProviders") ? Strings(Root["NativeProviders"], MaxProviders) : [];
        if (Sidecars.Length > 0 && (!TrustedNative || !Capabilities.Contains("NativeExtensions", StringComparer.Ordinal)))
            throw Invalid("TrustedNativeRequired " + Name);
        var ProviderNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        NativeProviders = Sidecars.Select(Path =>
        {
            if (!Path.EndsWith(".json", StringComparison.Ordinal)) throw Invalid("InvalidPackageProvider");
            JsonObject Sidecar = Object(ReadMember(Path, 256 * 1024));
            Shape(Sidecar, ["SchemaVersion", "AbiVersion", "Provider", "Extension", "Services"]);
            if (Text(Sidecar, "Provider") != "Native" || Sidecar["Extension"] is not JsonObject Extension)
                throw Invalid("InvalidPackageProvider");
            int? SchemaVersion = Sidecar["SchemaVersion"]?.GetValue<int>();
            int? AbiVersion = Sidecar["AbiVersion"]?.GetValue<int>();
            if ((SchemaVersion, AbiVersion) is not ((2, 1) or (3, 2))) throw Invalid("InvalidPackageProvider");
            Shape(Extension, ["Name", "Version", "RequiredCapabilities", "Bindings", "Provenance"]);
            string Provider = Alias(Text(Extension, "Name"));
            if (!ProviderNames.Add(Provider)) throw Invalid("DuplicatePackageProvider " + Provider);
            string[] Required = Strings(Extension["RequiredCapabilities"], 32);
            if (!Required.Contains("NativeExtensions", StringComparer.Ordinal) ||
                Required.Except(Capabilities, StringComparer.Ordinal).Any())
                throw Invalid("PackageProviderCapabilityMismatch " + Provider);
            if (Extension["Bindings"] is not JsonArray Bindings || Bindings.Count is 0 or > 2)
                throw Invalid("InvalidPackageProvider");
            var Platforms = new HashSet<string>(StringComparer.Ordinal);
            foreach (JsonNode? Value in Bindings) {
                var Binding = Value as JsonObject ?? throw Invalid("InvalidPackageProvider");
                Shape(Binding, ["Platform", "Path", "Sha256"]);
                string Platform = Text(Binding, "Platform");
                string ExtensionSuffix = Platform switch {
                    "WindowsX64" => ".dll",
                    "LinuxX64" => ".so",
                    _ => throw Invalid("UnsupportedPackagePlatform " + Platform)
                };
                string Binary = Text(Binding, "Path");
                if (!Platforms.Add(Platform) || !Binary.EndsWith(ExtensionSuffix, StringComparison.Ordinal))
                    throw Invalid("InvalidPackageProvider");
                byte[] Data = ReadMember(Binary, MaxFileBytes);
                if (!MatchesNativeImage(Data, Platform)) throw Invalid("InvalidPackageNativeImage " + Provider);
                if (Data.Length == 0 || Hash(Data) != Text(Binding, "Sha256"))
                    throw Invalid("PackageProviderIntegrityMismatch " + Provider);
            }
            return new NativeProvider(Provider, Sidecar);
        }).ToArray();
        Digest = Hash(Encode(new { Descriptor = Hash(Metadata), Files = Content.Select(Item =>
            new { Path = Item.Key, Sha256 = Hash(Item.Value) }) }));
    }

    internal IEnumerable<KeyValuePair<string, string>> Dependencies(IReadOnlySet<string> Included) =>
        Requirements.Concat(OptionalRequirements.Where(Item => Included.Contains(Item.Key)));

    internal IEnumerable<string> ActiveModules(IReadOnlySet<string> Included) =>
        Modules.Concat(FeatureModules.Where(Item => Included.Contains(Item.Key)).SelectMany(Item => Item.Value));

    private static bool IsModule(string Path) => Path.EndsWith(".luau", StringComparison.Ordinal) &&
        !Path.EndsWith(".d.luau", StringComparison.Ordinal);

    internal bool Supports(string Platform) =>
        (Platforms.Length == 0 || Platforms.Contains(Platform, StringComparer.Ordinal)) &&
        NativeProviders.All(Provider => ((JsonArray)Provider.Declaration["Extension"]!["Bindings"]!)
            .OfType<JsonObject>().Any(Item => Text(Item, "Platform") == Platform));

    private static bool MatchesNativeImage(ReadOnlySpan<byte> Bytes, string Platform)
    {
        if (Bytes.Length < 64) return false;
        if (Platform == "LinuxX64")
            return Bytes[..4].SequenceEqual("\u007fELF"u8) && Bytes[4] == 2 && Bytes[5] == 1 && Bytes[6] == 1 &&
                BinaryPrimitives.ReadUInt16LittleEndian(Bytes[16..]) == 3 &&
                BinaryPrimitives.ReadUInt16LittleEndian(Bytes[18..]) == 62 &&
                BinaryPrimitives.ReadUInt32LittleEndian(Bytes[20..]) == 1;
        int Offset = BinaryPrimitives.ReadInt32LittleEndian(Bytes[60..]);
        if (!Bytes[..2].SequenceEqual("MZ"u8) || Offset < 64 || Offset > Bytes.Length - 26) return false;
        var Header = Bytes[Offset..];
        return Header[..4].SequenceEqual("PE\0\0"u8) &&
            BinaryPrimitives.ReadUInt16LittleEndian(Header[4..]) == 0x8664 &&
            BinaryPrimitives.ReadUInt16LittleEndian(Header[20..]) >= 2 &&
            (BinaryPrimitives.ReadUInt16LittleEndian(Header[22..]) & 0x2000) != 0 &&
            BinaryPrimitives.ReadUInt16LittleEndian(Header[24..]) == 0x20b;
    }

    internal static string SdkVersion(string Value, bool AllowWildcard = true)
    {
        if (AllowWildcard && Value == "*") return Value;
        if (Value.Length > 64 || !SdkPattern.IsMatch(Value)) throw Invalid("InvalidPackageSdkVersion");
        return Value;
    }

    internal static PackageSource Read(string Path)
    {
        if (Directory.Exists(Path)) {
            VerifyLocalPath(Path);
            return new PackageSource(ReadFile(PathFor(Path, Descriptor), 65536),
                (Name, Bound) => ReadFile(PathFor(Path, Name), Bound));
        }
        using var File = OpenRegularFile(Path);
        if (File.Length > MaxPackageBytes + 65536) throw Invalid("PackageLimit");
        CheckZipDirectory(File);
        using var Zip = new ZipArchive(File, ZipArchiveMode.Read);
        var Entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.Ordinal);
        var Paths = new Dictionary<string, (string Path, bool Directory)>(StringComparer.OrdinalIgnoreCase);
        if (Zip.Entries.Count > MaxEntries) throw Invalid("PackageLimit");
        long Expanded = 0;
        foreach (var Entry in Zip.Entries) {
            AddPath(Paths, Entry.FullName);
            int Mode = (Entry.ExternalAttributes >> 16) & 0xf000;
            if (Mode is not (0 or 0x8000) || (Entry.ExternalAttributes & 0x410) != 0)
                throw Invalid("PackageLinkOrSpecialEntry");
            if (Entry.Length > MaxFileBytes || Entry.Length > MaxPackageBytes - Expanded) throw Invalid("PackageLimit");
            Expanded += Entry.Length;
            Entries.Add(Entry.FullName, Entry);
        }
        byte[] ReadEntry(string Name, int Bound) {
            if (!Entries.Remove(Name, out var Entry)) throw Invalid("MissingPackageFile");
            using Stream Input = Entry.Open();
            return ReadExact(Input, Entry.Length, Bound);
        }
        var Result = new PackageSource(ReadEntry(Descriptor, 65536), ReadEntry);
        if (Entries.Count != 0) throw Invalid("UnexpectedPackageEntry");
        return Result;
    }

    internal static PackageSource ReadSnapshot(Func<string, int, byte[]> ReadMember) =>
        new(ReadMember(Descriptor, 65536), ReadMember);

    internal static bool IsPendingPackageArchive(string Leaf) => PendingArchivePattern.IsMatch(Leaf);

    private static void CheckZipDirectory(FileStream File)
    {
        const int EndBytes = 22;
        int TailBytes = (int)Math.Min(File.Length, EndBytes + ushort.MaxValue);
        byte[] Tail = new byte[TailBytes];
        File.Position = File.Length - TailBytes;
        File.ReadExactly(Tail);
        for (int Index = TailBytes - EndBytes; Index >= 0; --Index) {
            var End = Tail.AsSpan(Index);
            if (BinaryPrimitives.ReadUInt32LittleEndian(End) != 0x06054b50 ||
                BinaryPrimitives.ReadUInt16LittleEndian(End[20..]) != End.Length - EndBytes) continue;
            uint Disks = BinaryPrimitives.ReadUInt32LittleEndian(End[4..]);
            ushort LocalCount = BinaryPrimitives.ReadUInt16LittleEndian(End[8..]);
            ushort Count = BinaryPrimitives.ReadUInt16LittleEndian(End[10..]);
            uint Length = BinaryPrimitives.ReadUInt32LittleEndian(End[12..]);
            uint Offset = BinaryPrimitives.ReadUInt32LittleEndian(End[16..]);
            if (Disks != 0 || LocalCount != Count || Count > MaxEntries || Length > 1024 * 1024 ||
                (ulong)Offset + Length != (ulong)(File.Length - TailBytes + Index)) throw Invalid("PackageZipDirectoryLimit");
            File.Position = 0;
            return;
        }
        throw Invalid("InvalidPackageArchive");
    }

    internal void WriteArchive(string Path)
    {
        VerifyLocalPath(System.IO.Path.GetDirectoryName(Path)!);
        if (System.IO.Path.Exists(Path)) throw Invalid("PackageOutputExists");
        string Temporary = Path + "." + Guid.NewGuid().ToString("N") + ".pending";
        var File = new FileStream(Temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        try {
            using (File)
            using (var Archive = new ZipArchive(File, ZipArchiveMode.Create)) {
                void Add(string Name, byte[] Bytes) {
                    var Entry = Archive.CreateEntry(Name, CompressionLevel.Optimal);
                    Entry.LastWriteTime = DateTimeOffset.UnixEpoch.AddYears(10);
                    Entry.ExternalAttributes = unchecked((int)0x81a40000);
                    using var Output = Entry.Open();
                    Output.Write(Bytes);
                }
                Add(Descriptor, Metadata);
                foreach (var Item in Content) Add(Item.Key, Item.Value);
            }
            System.IO.File.Move(Temporary, Path);
        } finally {
            System.IO.File.Delete(Temporary);
        }
    }

    internal void WriteDirectory(string Path)
    {
        WriteNew(PathFor(Path, Descriptor), Metadata);
        foreach (var Item in Content) WriteNew(PathFor(Path, Item.Key), Item.Value);
    }

    internal static string Alias(string Value) => AliasPattern.IsMatch(Value) && !Reserved(Value)
        ? Value : throw Invalid("InvalidPackageAlias");

    internal static Version ParseVersion(string Value) =>
        Version.TryParse(Value, out var Result) && Result.Revision < 0 && Result.Build >= 0 &&
        Result.ToString(3) == Value ? Result : throw Invalid("InvalidPackageVersion");

    internal static void ValidateRange(string Range) {
        if (Range != "*") ParseVersion(Range.StartsWith('^') || Range.StartsWith('~') ? Range[1..] : Range);
    }

    internal static bool Matches(string Range, Version Value)
    {
        if (Range == "*") return true;
        bool Compatible = Range.StartsWith('^') || Range.StartsWith('~');
        Version Base = ParseVersion(Compatible ? Range[1..] : Range);
        if (!Compatible) return Base == Value;
        if (Value < Base || Value.Major != Base.Major) return false;
        if (Range[0] == '~' || Base.Major == 0) {
            if (Value.Minor != Base.Minor) return false;
            if (Range[0] == '^' && Base.Minor == 0) return Value.Build == Base.Build;
        }
        return true;
    }

    internal static void PortablePath(string Path)
    {
        if (Path.Length is 0 or > 400) throw Invalid("InvalidPackagePath");
        foreach (string Part in Path.Split('/'))
            if (Part.Length > 255 || !PathPattern.IsMatch(Part) || Part.EndsWith('.') || Reserved(Part.Split('.')[0]))
                throw Invalid("InvalidPackagePath");
    }

    private static bool Reserved(string Name) => Name.ToLowerInvariant() is "con" or "prn" or "aux" or "nul" ||
        Name.Length == 4 && (Name.StartsWith("com", StringComparison.OrdinalIgnoreCase) ||
        Name.StartsWith("lpt", StringComparison.OrdinalIgnoreCase)) && Name[3] is >= '1' and <= '9';

    private static void AddPath(Dictionary<string, (string Path, bool Directory)> Paths, string Path)
    {
        PortablePath(Path);
        int End = 0;
        do {
            End = Path.IndexOf('/', End + 1);
            string Prefix = End < 0 ? Path : Path[..End];
            if (!Paths.TryAdd(Prefix, (Prefix, End >= 0)) &&
                (Paths[Prefix].Path != Prefix || !Paths[Prefix].Directory || End < 0))
                throw Invalid("DuplicatePackagePath");
        } while (End >= 0);
    }

    internal static string PathFor(string Root, string Relative) {
        PortablePath(Relative);
        return System.IO.Path.Combine(Root, Relative.Replace('/', System.IO.Path.DirectorySeparatorChar));
    }

    internal static void VerifyLocalPath(string Path)
    {
        for (string? Current = System.IO.Path.GetFullPath(Path); Current is not null;
                Current = System.IO.Path.GetDirectoryName(Current)) {
            if ((File.GetAttributes(Current) & FileAttributes.ReparsePoint) != 0)
                throw Invalid("PackageLinkOrSpecialEntry");
        }
    }

    internal static byte[] ReadFile(string Path, int Bound)
    {
        using var Input = OpenRegularFile(Path);
        return ReadExact(Input, Input.Length, Bound);
    }

    private static FileStream OpenRegularFile(string Path)
    {
        VerifyLocalPath(Path);
        if (!OperatingSystem.IsLinux()) return new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.Read);
        // Anchor each component and open the leaf without blocking on FIFOs.
        const int AtCurrent = -100, NoFollow = 0x20000, CloseOnExec = 0x80000, DirectoryOnly = 0x10000, NonBlocking = 0x800;
        string[] Parts = System.IO.Path.GetFullPath(Path).Split('/', StringSplitOptions.RemoveEmptyEntries);
        if (Parts.Length == 0) throw Invalid("PackageLinkOrSpecialEntry");
        SafeFileHandle GetHandle(int Parent, string Name, int Flags) {
            int File = OpenAt(Parent, Name, Flags | NoFollow | CloseOnExec);
            return File < 0 ? throw Invalid("PackageFileOpenFailed") : new SafeFileHandle((IntPtr)File, true);
        }
        SafeFileHandle Root = GetHandle(AtCurrent, "/", DirectoryOnly);
        try {
            foreach (string Part in Parts[..^1]) {
                SafeFileHandle Next = GetHandle(Root.DangerousGetHandle().ToInt32(), Part, DirectoryOnly);
                Root.Dispose();
                Root = Next;
            }
            SafeFileHandle Leaf = GetHandle(Root.DangerousGetHandle().ToInt32(), Parts[^1], NonBlocking);
            try {
                const int EmptyPath = 0x1000;
                if (Statx(Leaf.DangerousGetHandle().ToInt32(), "", EmptyPath, 1, out var Info) != 0 ||
                    (Info.Mode & 0xf000) != 0x8000) throw Invalid("PackageLinkOrSpecialEntry");
                return new FileStream(Leaf, FileAccess.Read);
            } catch { Leaf.Dispose(); throw; }
        } finally { Root.Dispose(); }
    }

    [StructLayout(LayoutKind.Explicit, Size = 256)]
    private struct StatxInfo { [FieldOffset(28)] internal ushort Mode; }
    [DllImport("libc", EntryPoint = "openat", SetLastError = true)]
    private static extern int OpenAt(int Parent, [MarshalAs(UnmanagedType.LPUTF8Str)] string Path, int Flags);
    [DllImport("libc", EntryPoint = "statx", SetLastError = true)]
    private static extern int Statx(int File, [MarshalAs(UnmanagedType.LPUTF8Str)] string Path, int Flags, uint Mask, out StatxInfo Info);

    private static byte[] ReadExact(Stream Input, long Length, int Bound)
    {
        if (Length < 0 || Length > Bound) throw Invalid("PackageLimit");
        byte[] Bytes = new byte[(int)Length];
        Input.ReadExactly(Bytes);
        if (Input.ReadByte() != -1) throw Invalid("PackageFileChanged");
        return Bytes;
    }

    internal static void WriteNew(string Path, byte[] Bytes)
    {
        string Parent = System.IO.Path.GetDirectoryName(Path)!;
        EnsureDirectory(Parent);
        using var Output = new FileStream(Path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        Output.Write(Bytes);
    }

    internal static void EnsureDirectory(string Path)
    {
        if (!Directory.Exists(Path)) {
            EnsureDirectory(System.IO.Path.GetDirectoryName(Path) ?? throw Invalid("InvalidPackagePath"));
            Directory.CreateDirectory(Path);
        }
        VerifyLocalPath(Path);
    }

    internal static JsonObject Object(byte[] Bytes) => JsonNode.Parse(Bytes, documentOptions:
        new JsonDocumentOptions { MaxDepth = 16 }) as JsonObject ?? throw Invalid("InvalidPackageMetadata");
    internal static byte[] Encode<T>(T Value) => JsonSerializer.SerializeToUtf8Bytes(Value, JsonOptions);
    internal static string Hash(byte[] Bytes) => Convert.ToHexStringLower(SHA256.HashData(Bytes));
    internal static InvalidDataException Invalid(string Code) => new("[LUI:Packages] " + Code);
    internal static string Text(JsonObject Object, string Key) => Object[Key]?.GetValue<string>() is { Length: > 0 } Value
        ? Value : throw Invalid("InvalidPackageMetadata");

    internal static string[] Strings(JsonNode? Node, int Maximum)
    {
        if (Node is not JsonArray Items || Items.Count > Maximum) throw Invalid("InvalidPackageMetadata");
        var Result = Items.Select(Value => Value?.GetValue<string>() ?? throw Invalid("InvalidPackageMetadata")).ToArray();
        if (Result.Distinct(StringComparer.OrdinalIgnoreCase).Count() != Result.Length) throw Invalid("DuplicatePackageValue");
        Array.Sort(Result, StringComparer.Ordinal);
        return Result;
    }

    internal static void Shape(JsonObject Root, string[] Required, string[]? Optional = null)
    {
        if (Required.Any(Key => !Root.ContainsKey(Key)) ||
            Root.Any(Item => !Required.Contains(Item.Key) && !(Optional?.Contains(Item.Key) ?? false)))
            throw Invalid("InvalidPackageMetadata");
    }
}
