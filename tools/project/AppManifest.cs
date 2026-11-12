using System.Text.Json;

namespace Lui.WinUI;

// The native diagnostic transport carries fixed codes only. Application text,
// paths and exception details must never become transport fields.
internal static class ApplicationDiagnostic
{
    private static readonly Dictionary<string, string> Messages = new(StringComparer.Ordinal)
    {
        ["UnknownCapability"] = "lui.json Capabilities contains an unknown name. Use exact tokens such as network.client for HttpService or network.server for HttpServerService. Service names are not capability names.",
        ["DuplicateCapability"] = "lui.json Capabilities repeats a token. Keep each capability only once.",
        ["NetworkClientRequired"] = "HttpService or an outbound network operation needs network.client. Declare it in lui.json Capabilities and use a host that grants it.",
        ["NetworkServerRequired"] = "HttpServerService or an inbound network operation needs network.server. Declare it in lui.json Capabilities and use a host that grants it.",
        ["NetworkRawRequired"] = "NetworkService needs network.raw plus the operation's network.client or network.server grant. HTTP services do not need network.raw.",
        ["HostServicesRequired"] = "This provider needs the HostServices capability and a trusted host installation. A manifest declaration alone does not install it.",
        ["ClipboardUnavailable"] = "ClipboardService needs Clipboard in lui.json Capabilities and a host with clipboard support.",
        ["DialogsUnavailable"] = "DialogService needs Dialogs in lui.json Capabilities and a host with file-picker support.",
        ["ProviderUnavailable"] = "The requested native service is not installed. Check the service's platform support and host setup; adding a capability alone does not install it.",
        ["InspectorUnsupported"] = "Inspect Layout cannot run privileged capabilities or native extensions. Use Run App to test this project. The inspector runs a separate, unprivileged app generation.",
        ["CapabilityDenied"] = "The host did not grant a requested capability. Check its supported capabilities and explicit native-provider trust settings.",
        ["StorageUnavailable"] = "Application storage is unavailable. Check application-data permissions or close another running copy. No temporary storage fallback was granted.",
        ["InvalidManifest"] = "lui.json is invalid or incomplete. Check its schema, required fields and exact-case declared files, then run Check Types.",
        ["MissingFile"] = "A declared project file is missing. Check lui.json and the file's exact-case relative name.",
        ["CompileFailed"] = "A declared source could not be compiled. Run Check Types to locate source errors.",
        ["OperationFailed"] = "An application operation failed. Run Check Types and reproduce the last action. Private source and exception text are omitted from this diagnostic.",
        ["StartupFailed"] = "The app could not start. Run Check Types, confirm the selected SDK with lui doctor, and check the project's declared files and host requirements. No detailed startup reason was available."
    };

    public static string? GetCode(string? Message)
    {
        if (Message is null) return null;
        foreach (string Code in Messages.Keys)
            foreach (string System in new[] { "Project", "Capabilities", "Preview", "Diagnostics" })
            {
                string Fixed = "[LUI:" + System + "] " + Code;
                if (Message == Fixed || (System == "Capabilities" && Message.EndsWith(Fixed, StringComparison.Ordinal))) return Code;
            }
        return null;
    }

    public static string Describe(string Message) => GetCode(Message) is { } Code ? Messages[Code] : Message;
    public static bool TryReadRecord(string Line, out string Message, out bool Startup)
    {
        Message = string.Empty; Startup = false;
        const string Prefix = "[LUI:Diagnostic] ";
        if (Line.Length > 1024 || !Line.StartsWith(Prefix, StringComparison.Ordinal)) return false;
        try {
            using JsonDocument Document = JsonDocument.Parse(Line[Prefix.Length..], new JsonDocumentOptions { MaxDepth = 4 });
            JsonElement Value = Document.RootElement;
            if (Value.ValueKind != JsonValueKind.Object || Value.EnumerateObject().Count() != 3 ||
                !Value.TryGetProperty("Version", out var Version) || Version.ValueKind != JsonValueKind.Number || !Version.TryGetInt32(out int Number) || Number != 1 ||
                !Value.TryGetProperty("Code", out var Code) || Code.ValueKind != JsonValueKind.String || !Messages.TryGetValue(Code.GetString()!, out string? Advice) ||
                !Value.TryGetProperty("Phase", out var Phase) || Phase.ValueKind != JsonValueKind.String || Phase.GetString() is not ("Startup" or "Runtime")) return false;
            Startup = Phase.GetString() == "Startup";
            Message = "[LUI:App] " + (Startup ? "Startup failed. " : "An app callback or operation failed; the window may still be open. ") + Advice;
            return true;
        } catch (JsonException) { return false; }
    }
    public static string Record(string Code, string Phase) => JsonSerializer.Serialize(new {
        Version = 1, Code = Messages.ContainsKey(Code) ? Code : "OperationFailed",
        Phase = Phase == "Runtime" ? "Runtime" : "Startup"
    });
}

internal sealed class AppManifest
{
    internal sealed class SandboxConfiguration
    {
        public ulong MaxMemoryBytes { get; set; }
        public ulong MaxInterrupts { get; set; }
    }

    internal sealed class NetworkPolicyConfiguration
    {
        public bool ClientLoopbackOnly { get; set; }
        public bool ServerLoopbackOnly { get; set; }
        public uint ClientPortMin { get; set; }
        public uint ClientPortMax { get; set; }
        public uint ServerPortMin { get; set; }
        public uint ServerPortMax { get; set; }
    }

    public int SchemaVersion { get; set; }
    public string Script { get; set; } = string.Empty;
    public List<string> Capabilities { get; set; } = new();
    public List<string> Extensions { get; set; } = new();
    public List<string> Assets { get; set; } = new();
    public SandboxConfiguration? Sandbox { get; set; }
    public NetworkPolicyConfiguration? NetworkPolicy { get; set; }

    public string ScriptPath { get; private set; } = string.Empty;
    public List<string> ExtensionPaths { get; private set; } = new();
    public Dictionary<string, string> AssetPaths { get; private set; } = new(StringComparer.OrdinalIgnoreCase);
    public bool AllowNativeExtensions { get; private set; }
    public bool AllowHostServices { get; private set; }
    public bool AllowClipboard { get; private set; }
    public bool AllowDialogs { get; private set; }
    public bool AllowNetworkClient { get; private set; }
    public bool AllowNetworkServer { get; private set; }
    public bool AllowNetworkRaw { get; private set; }
    public bool AllowStorage { get; private set; }
    public bool AllowNotifications { get; private set; }
    public bool AllowTray { get; private set; }

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
            if (Capability is not
                ("NativeExtensions" or "HostServices" or "Clipboard" or "Dialogs" or
                 "network.client" or "network.server" or "network.raw" or "Storage" or "Notifications" or "Tray"))
                throw new InvalidDataException("[LUI:Project] UnknownCapability");
            if (!SeenCapabilities.Add(Capability))
                throw new InvalidDataException("[LUI:Project] DuplicateCapability");
        }
        Manifest.AllowNativeExtensions = SeenCapabilities.Contains("NativeExtensions");
        Manifest.AllowHostServices = SeenCapabilities.Contains("HostServices");
        Manifest.AllowClipboard = SeenCapabilities.Contains("Clipboard");
        Manifest.AllowDialogs = SeenCapabilities.Contains("Dialogs");
        Manifest.AllowNetworkClient = SeenCapabilities.Contains("network.client");
        Manifest.AllowNetworkServer = SeenCapabilities.Contains("network.server");
        Manifest.AllowNetworkRaw = SeenCapabilities.Contains("network.raw");
        Manifest.AllowStorage = SeenCapabilities.Contains("Storage");
        Manifest.AllowNotifications = SeenCapabilities.Contains("Notifications");
        Manifest.AllowTray = SeenCapabilities.Contains("Tray");
        if (Manifest.Sandbox is not null &&
            (Manifest.Sandbox.MaxMemoryBytes < 1024 * 1024 || Manifest.Sandbox.MaxInterrupts == 0 ||
             Manifest.AllowNativeExtensions))
            throw new InvalidDataException("Sandbox limits are invalid or grant native extensions");
        static bool ValidPortRange(uint Minimum, uint Maximum) =>
            (Minimum == 0 && Maximum == 0) ||
            (Minimum >= 1 && Minimum <= Maximum && Maximum <= 65535);
        if (Manifest.NetworkPolicy is { } Policy &&
            (!ValidPortRange(Policy.ClientPortMin, Policy.ClientPortMax) ||
             !ValidPortRange(Policy.ServerPortMin, Policy.ServerPortMax)))
            throw new InvalidDataException("Network policy port range is invalid");

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
