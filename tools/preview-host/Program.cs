using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Lui.WinUI;
using Lui.Project;

namespace Lui.PreviewHost;

internal static class Program
{
    public static int Main(string[] Arguments)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        try
        {
            if (Arguments.Length != 4 || Arguments[0] is not ("--manifest" or "--project") || Arguments[2] != "--runtime")
                throw new ArgumentException("Expected --manifest <path> --runtime <native library>");
            using PreviewSession Session = new(Arguments[1], Arguments[3],Arguments[0]=="--project");
            return Session.Run();
        }
        catch (Exception Error) when (Error is ArgumentException or InvalidDataException or IOException or
            UnauthorizedAccessException or JsonException or DllNotFoundException or EntryPointNotFoundException or
            BadImageFormatException)
        {
            string Code = ApplicationDiagnostic.GetCode(Error.Message) ?? "StartupFailed";
            Console.Error.WriteLine(Arguments.FirstOrDefault()=="--project" ?
                "[LUI:Preview] " + ApplicationDiagnostic.Describe("[LUI:Preview] " + Code) :
                "[LUI:Preview] " + ApplicationDiagnostic.Describe(Error.Message));
            return 1;
        }
    }
}

internal sealed class PreviewSession : IDisposable
{
    private readonly int ProtocolVersion;
    private const int MaxMessageChars = 64 * 1024;
    private const int MaxScriptBytes = 8 * 1024 * 1024;
    private readonly AppManifest? Manifest;
    private readonly string ManifestPath, RuntimePath;
    private PreparedProject? Project;
    private readonly NativeRuntime Native;
    private readonly object OutputLock = new();
    private readonly ConcurrentQueue<string> Commands = new();
    private readonly NativeRuntime.LogCallback LogHandler;
    private IntPtr Runtime;
    private int Generation;
    private string? LastTree;
    private volatile bool InputClosed;
    private int QueueOverflow;
    private readonly HashSet<(int Id, uint PointerId)> ActivePointers = new();
    private readonly HashSet<int> FocusTargets = new();
    private static readonly Regex SourcePattern = new(@"\[string ""(?<Source>[^""]+)""\]:(?<Line>[1-9]\d*):",
        RegexOptions.CultureInvariant);

    public PreviewSession(string ManifestPath, string RuntimePath,bool Modular=false)
    {
        this.ManifestPath=Path.GetFullPath(ManifestPath); this.RuntimePath=Path.GetFullPath(RuntimePath);
        ProtocolVersion=Modular?2:1;
        if(Modular) {Project=new PreparedProject(ManifestPath,RuntimePath); if(Project.HasPrivileges) {Project.Dispose(); Project=null; throw new InvalidDataException("[LUI:Preview] InspectorUnsupported");}}
        else Manifest = AppManifest.Load(ManifestPath);
        if (Manifest is not null && (Manifest.Capabilities.Count != 0 || Manifest.Extensions.Count != 0))
            throw new InvalidDataException("[LUI:Preview] InspectorUnsupported");
        Native = new NativeRuntime(RuntimePath);
        LogHandler = OnLog;
    }

    public int Run()
    {
        using (JsonDocument Schema = JsonDocument.Parse(Native.Schema()))
            Write(new { version = ProtocolVersion, type = "hello", generation = 0, schema = Schema.RootElement,
                diagnosticsMode=Project?.DiagnosticMode==2?"Restricted":"Development", redacted=ProtocolVersion==2,
                inputCommands = new[] { "focus", "pointerInput", "keyInput", "resetInput" } });
        Reload();
        _ = Task.Run(ReadInput);
        while (!InputClosed || !Commands.IsEmpty)
        {
            while (Commands.TryDequeue(out string? Line))
            {
                if (HandleCommand(Line)) return 0;
            }
            if (Interlocked.Exchange(ref QueueOverflow, 0) != 0)
                Write(new { version = ProtocolVersion, type = "diagnostic", generation = Generation,
                    message = "Preview command queue exceeded 256 entries" });
            if (Runtime != IntPtr.Zero)
            {
                Native.Pump(Runtime);
                PublishTree(false);
            }
            Thread.Sleep(16);
        }
        return 0;
    }

    private void ReadInput()
    {
        try
        {
            TextReader Reader = Console.In;
            StringBuilder Line = new();
            while (true)
            {
                int Value = Reader.Read();
                if (Value < 0) break;
                if (Value == '\n')
                {
                    EnqueueLine(Line.ToString().TrimEnd('\r'));
                    Line.Clear();
                }
                else if (Line.Length <= MaxMessageChars) Line.Append((char)Value);
                if (Line.Length == MaxMessageChars + 1)
                {
                    EnqueueLine(string.Empty);
                    // Discard the rest of this oversized line.
                    while ((Value = Reader.Read()) >= 0 && Value != '\n') { }
                    Line.Clear();
                    if (Value < 0) break;
                }
            }
            if (Line.Length > 0) EnqueueLine(Line.ToString());
        }
        catch (IOException) { }
        finally { InputClosed = true; }
    }

    private void EnqueueLine(string Line)
    {
        if (Commands.Count < 256) Commands.Enqueue(Line);
        else Interlocked.Exchange(ref QueueOverflow, 1);
    }

    private void Reload()
    {
        ActivePointers.Clear();
        FocusTargets.Clear();
        if (Runtime != IntPtr.Zero) { Native.Destroy(Runtime); Runtime = IntPtr.Zero; }
        ++Generation;
        LastTree = null;
        if(ProtocolVersion==2) {Project?.Dispose(); Project=null;}
        try
        {
            if(ProtocolVersion==2) {
                Project=new PreparedProject(ManifestPath,RuntimePath);
                if(Project.HasPrivileges) throw new InvalidDataException("[LUI:Project] CapabilityDenied");
            }
            Runtime = Project?.CreateRuntime() ?? Native.Create();
            if (Runtime == IntPtr.Zero) throw new InvalidDataException("Unable to create Luau VM");
            Native.SetLogCallback(Runtime, IntPtr.Zero, LogHandler);
            Native.SetBackendName(Runtime, "preview");
            if(Project is not null) {
                Project.Configure(Runtime,false);
                if(Native.EnableSourceProvenance(Runtime)==0) throw new InvalidDataException(Project.Error(Runtime));
                Write(new {version=2,type="project",generation=Generation,digest=Project.Digest,sourceDigest=Project.SourceDigest,
                    diagnosticsMode=Project.DiagnosticMode==2?"Restricted":"Development",redacted=true,
                    sources=Project.DiagnosticMode==2?Array.Empty<object>():Project.Sources.Select(Source=>(object)new{id=Source.Id,path=Source.Path,hash=Source.Hash}).ToArray()});
                if(!Project.Run(Runtime)) throw new InvalidDataException(Project.Error(Runtime));
                PublishTree(true); return;
            }
            NativeRuntime.SandboxLimits Limits = new()
            {
                StructSize = (uint)Marshal.SizeOf<NativeRuntime.SandboxLimits>(),
                AbiVersion = 1,
                MaxMemoryBytes = Math.Min(Manifest!.Sandbox?.MaxMemoryBytes ?? 32UL * 1024 * 1024, 64UL * 1024 * 1024),
                MaxInterrupts = Math.Min(Manifest.Sandbox?.MaxInterrupts ?? 100_000UL, 1_000_000UL),
            };
            if (Native.ConfigureSandbox(Runtime, ref Limits) == 0) throw new InvalidDataException(Native.Error(Runtime));
            if (Native.EnableSourceProvenance(Runtime) == 0) throw new InvalidDataException(Native.Error(Runtime));
            foreach (string Name in Manifest.AssetPaths.Keys)
                if (Native.RegisterAsset(Runtime, Name) == 0) throw new InvalidDataException(Native.Error(Runtime));
            FileInfo Script = new(Manifest.ScriptPath);
            if (Script.Length > MaxScriptBytes) throw new InvalidDataException("Script exceeds 8 MiB preview limit");
            string Source = File.ReadAllText(Manifest.ScriptPath, new UTF8Encoding(false, true));
            if (Source.Contains('\0')) throw new InvalidDataException("Script contains a NUL character");
            if (Native.RunScript(Runtime, Source, Manifest.Script) == 0)
                throw new InvalidDataException(Native.Error(Runtime));
            PublishTree(true);
        }
        catch (Exception Error) when (Error is IOException or InvalidDataException or UnauthorizedAccessException or DecoderFallbackException)
        {
            if (Runtime != IntPtr.Zero) { Native.Destroy(Runtime); Runtime = IntPtr.Zero; }
            Write(new { version = ProtocolVersion, type = "runtimeError", generation = Generation,
                message = ProtocolVersion==2?"[LUI:Preview] StartupFailed":Error.Message, location = ProtocolVersion==2?null:GetLocation(Error.Message) });
            Write(new { version = ProtocolVersion, type = "fullTree", generation = Generation, redacted=ProtocolVersion==2, nodes = Array.Empty<object>() });
        }
    }

    private bool HandleCommand(string Line)
    {
        try
        {
            if (Line.Length == 0 || Line.Length > MaxMessageChars) throw new InvalidDataException("Invalid command length");
            using JsonDocument Document = JsonDocument.Parse(Line);
            JsonElement Root = Document.RootElement;
            if (Root.ValueKind != JsonValueKind.Object || !Root.TryGetProperty("version", out JsonElement Version) ||
                Version.ValueKind != JsonValueKind.Number || !Version.TryGetInt32(out int Number) || Number != ProtocolVersion)
                throw new InvalidDataException("Unsupported protocol version");
            string Type = RequiredString(Root, "type");
            if (Type == "shutdown") return true;
            int ExpectedGeneration = RequiredInt(Root, "generation");
            if (ExpectedGeneration != Generation) throw new InvalidDataException("Stale preview generation");
            switch (Type)
            {
                case "reload": Reload(); break;
                case "snapshot": PublishTree(true); break;
                case "activate":
                    RequireRuntime();
                    if (Native.Activate(Runtime, RequiredPositiveInt(Root, "id")) == 0)
                        throw new InvalidDataException(Native.Error(Runtime));
                    PublishTree(false);
                    break;
                case "resizeViewport":
                    RequireRuntime();
                    int Id = RequiredPositiveInt(Root, "id");
                    double Width = RequiredSize(Root, "width");
                    double Height = RequiredSize(Root, "height");
                    if (Native.WindowResized(Runtime, Id, Width, Height) == 0)
                        throw new InvalidDataException(Native.Error(Runtime));
                    PublishTree(false);
                    break;
                case "focus":
                    RequireRuntime();
                    int FocusId = RequiredPositiveInt(Root, "id");
                    bool Focused = RequiredBool(Root, "focused");
                    if (Native.FocusChanged(Runtime, FocusId, Focused ? 1 : 0) == 0)
                        throw new InvalidDataException("[LUI:Preview] Focus rejected");
                    if (Focused) { FocusTargets.Clear(); FocusTargets.Add(FocusId); }
                    else FocusTargets.Remove(FocusId);
                    PublishTree(false);
                    break;
                case "pointerInput":
                    RequireRuntime();
                    int PointerTarget = RequiredPositiveInt(Root, "id");
                    int PointerPhase = RequiredPhase(Root, true);
                    int Device = RequiredInt(Root, "device");
                    int PointerId = RequiredInt(Root, "pointerId");
                    double X = RequiredCoordinate(Root, "x"), Y = RequiredCoordinate(Root, "y");
                    if (Device is < 0 or > 3 || PointerId is < 0 or > 31)
                        throw new InvalidDataException("[LUI:Preview] Invalid pointer identity");
                    var Identity = (PointerTarget, (uint)PointerId);
                    if (PointerPhase == 0 && !ActivePointers.Contains(Identity) && ActivePointers.Count >= 32)
                        throw new InvalidDataException("[LUI:Preview] Pointer limit exceeded; reset input");
                    int PointerResult = Native.PointerInput(Runtime, PointerTarget, PointerPhase, Device, (uint)PointerId, X, Y);
                    if (PointerPhase >= 2) ActivePointers.Remove(Identity);
                    if (PointerResult == 0) throw new InvalidDataException("[LUI:Preview] Pointer input rejected");
                    if (PointerPhase == 0) ActivePointers.Add(Identity);
                    PublishTree(false);
                    break;
                case "keyInput":
                    RequireRuntime();
                    string Key = RequiredString(Root, "key");
                    if (Key.Length > 32 || Key.Contains('\0') ||
                        Native.KeyInput(Runtime, RequiredPositiveInt(Root, "id"), RequiredPhase(Root, false), Key) == 0)
                        throw new InvalidDataException("[LUI:Preview] Keyboard input rejected");
                    PublishTree(false);
                    break;
                case "resetInput":
                    RequireRuntime();
                    ResetInput();
                    PublishTree(false);
                    break;
                default: throw new InvalidDataException("Unknown preview command");
            }
        }
        catch (Exception Error) when (Error is JsonException or InvalidDataException or ArgumentException)
        {
            Write(new { version = ProtocolVersion, type = "diagnostic", generation = Generation,
                message = ProtocolVersion==2?"[LUI:Preview] InvalidCommand":Error.Message });
        }
        return false;
    }

    private void RequireRuntime()
    {
        if (Runtime == IntPtr.Zero) throw new InvalidDataException("Preview generation has no active runtime");
    }

    private void ResetInput()
    {
        foreach (var Pointer in ActivePointers.ToArray())
            Native.PointerInput(Runtime, Pointer.Id, 3, 0, Pointer.PointerId, 0, 0);
        foreach (int Id in FocusTargets.ToArray()) Native.FocusChanged(Runtime, Id, 0);
        ActivePointers.Clear();
        FocusTargets.Clear();
    }

    private static bool RequiredBool(JsonElement Root, string Name) =>
        Root.TryGetProperty(Name, out JsonElement Value) && Value.ValueKind is JsonValueKind.True or JsonValueKind.False
            ? Value.GetBoolean() : throw new InvalidDataException("Invalid " + Name);

    private static int RequiredPhase(JsonElement Root, bool Pointer) => RequiredString(Root, "phase") switch
    {
        "Began" => 0, "Changed" => 1, "Ended" => 2,
        "Canceled" when Pointer => 3,
        _ => throw new InvalidDataException("Invalid input phase")
    };

    private static double RequiredCoordinate(JsonElement Root, string Name)
    {
        if (!Root.TryGetProperty(Name, out JsonElement Value) || Value.ValueKind != JsonValueKind.Number ||
            !Value.TryGetDouble(out double Number) ||
            !double.IsFinite(Number) || Math.Abs(Number) > 100_000)
            throw new InvalidDataException("Invalid " + Name);
        return Number;
    }

    private static string RequiredString(JsonElement Root, string Name) =>
        Root.TryGetProperty(Name, out JsonElement Value) && Value.ValueKind == JsonValueKind.String &&
        !string.IsNullOrWhiteSpace(Value.GetString()) ? Value.GetString()! : throw new InvalidDataException("Invalid " + Name);

    private static int RequiredInt(JsonElement Root, string Name) =>
        Root.TryGetProperty(Name, out JsonElement Value) && Value.ValueKind == JsonValueKind.Number &&
        Value.TryGetInt32(out int Number) ? Number : throw new InvalidDataException("Invalid " + Name);

    private static int RequiredPositiveInt(JsonElement Root, string Name)
    {
        int Value = RequiredInt(Root, Name);
        if (Value <= 0) throw new InvalidDataException("Invalid " + Name);
        return Value;
    }

    private static double RequiredSize(JsonElement Root, string Name)
    {
        if (!Root.TryGetProperty(Name, out JsonElement Value) || Value.ValueKind != JsonValueKind.Number ||
            !Value.TryGetDouble(out double Number) || !double.IsFinite(Number) || Number is < 0 or > 100_000)
            throw new InvalidDataException("Invalid " + Name);
        return Number;
    }

    private void PublishTree(bool Force)
    {
        if (Runtime == IntPtr.Zero) return;
        IntPtr Pointer = Native.GetPreviewTreeJson(Runtime);
        if (Pointer == IntPtr.Zero)
        {
            Write(new { version = ProtocolVersion, type = "diagnostic", generation = Generation, message = Native.Error(Runtime) });
            return;
        }
        string Tree = Marshal.PtrToStringUTF8(Pointer)!;
        if (!Force && Tree == LastTree) return;
        LastTree = Tree;
        lock (OutputLock)
        {
            Console.Out.Write("{\"version\":"+ProtocolVersion+",\"type\":\"fullTree\",\"redacted\":"+(ProtocolVersion==2?"true":"false")+",\"generation\":");
            Console.Out.Write(Generation);
            Console.Out.Write(",\"nodes\":");
            Console.Out.Write(Tree);
            Console.Out.WriteLine('}');
            Console.Out.Flush();
        }
    }

    private void OnLog(IntPtr Context, string Level, string Message)
    {
        if (Message.Length > 8192) Message = Message[..8192] + "…";
        Write(new { version = ProtocolVersion, type = Level == "Print" ? "consoleMessage" : "diagnostic",
            generation = Generation, level = Level, message = Message,
            location = Level == "Print" ? null : GetLocation(Message) });
    }

    private object? GetLocation(string Message)
    {
        if(Manifest is null) return null;
        Match Match = SourcePattern.Match(Message);
        if (!Match.Success || Match.Groups["Source"].Value != Manifest.Script ||
            !int.TryParse(Match.Groups["Line"].Value, out int Line) || Line <= 0) return null;
        return new { source = Manifest.Script, line = Line };
    }

    private void Write(object Message)
    {
        lock (OutputLock) Console.Out.WriteLine(JsonSerializer.Serialize(Message));
    }

    public void Dispose()
    {
        Project?.Dispose(); Project=null;
        if (Runtime != IntPtr.Zero) { Native.Destroy(Runtime); Runtime = IntPtr.Zero; }
        Native.Dispose();
    }
}

internal sealed class NativeRuntime : IDisposable
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct SandboxLimits
    {
        public uint StructSize;
        public uint AbiVersion;
        public ulong MaxMemoryBytes;
        public ulong MaxInterrupts;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate IntPtr CreateDelegate();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void DestroyDelegate(IntPtr Runtime);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void SetLogDelegate(IntPtr Runtime, IntPtr Context, LogCallback Callback);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void LogCallback(IntPtr Context,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string Level, [MarshalAs(UnmanagedType.LPUTF8Str)] string Message);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void SetNameDelegate(IntPtr Runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string Name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int SandboxDelegate(IntPtr Runtime, ref SandboxLimits Limits);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int StringDelegate(IntPtr Runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string Name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int RunDelegate(IntPtr Runtime,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string Source, [MarshalAs(UnmanagedType.LPUTF8Str)] string Chunk);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int IntDelegate(IntPtr Runtime, int Id);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int PumpDelegate(IntPtr Runtime);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int ResizeDelegate(IntPtr Runtime, int Id, double Width, double Height);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int FocusDelegate(IntPtr Runtime, int Id, int Focused);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int InputPointerDelegate(IntPtr Runtime, int Id,
        int Phase, int Device, uint PointerId, double X, double Y);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int KeyDelegate(IntPtr Runtime, int Id,
        int Phase, [MarshalAs(UnmanagedType.LPUTF8Str)] string Key);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate IntPtr PointerDelegate(IntPtr Runtime);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate IntPtr SchemaDelegate();

    private readonly IntPtr Library;
    internal readonly CreateDelegate Create;
    internal readonly DestroyDelegate Destroy;
    internal readonly SetLogDelegate SetLogCallback;
    internal readonly SetNameDelegate SetBackendName;
    internal readonly SandboxDelegate ConfigureSandbox;
    internal readonly PumpDelegate EnableSourceProvenance;
    internal readonly StringDelegate RegisterAsset;
    internal readonly RunDelegate RunScript;
    internal readonly IntDelegate Activate;
    internal readonly ResizeDelegate WindowResized;
    internal readonly FocusDelegate FocusChanged;
    internal readonly InputPointerDelegate PointerInput;
    internal readonly KeyDelegate KeyInput;
    internal readonly PumpDelegate Pump;
    internal readonly PointerDelegate GetPreviewTreeJson;
    private readonly PointerDelegate GetLastError;
    private readonly SchemaDelegate GetSchemaJson;

    internal NativeRuntime(string PathValue)
    {
        Library = NativeLibrary.Load(Path.GetFullPath(PathValue));
        Create = Load<CreateDelegate>("Lui_Create");
        Destroy = Load<DestroyDelegate>("Lui_Destroy");
        SetLogCallback = Load<SetLogDelegate>("Lui_SetLogCallback");
        SetBackendName = Load<SetNameDelegate>("Lui_SetBackendName");
        ConfigureSandbox = Load<SandboxDelegate>("Lui_ConfigureSandbox");
        EnableSourceProvenance = Load<PumpDelegate>("Lui_EnableSourceProvenance");
        RegisterAsset = Load<StringDelegate>("Lui_RegisterAsset");
        RunScript = Load<RunDelegate>("Lui_RunScript");
        Activate = Load<IntDelegate>("Lui_Activate");
        WindowResized = Load<ResizeDelegate>("Lui_WindowResized");
        FocusChanged = Load<FocusDelegate>("Lui_FocusChanged");
        PointerInput = Load<InputPointerDelegate>("Lui_PointerInput");
        KeyInput = Load<KeyDelegate>("Lui_KeyInput");
        Pump = Load<PumpDelegate>("Lui_Pump");
        GetPreviewTreeJson = Load<PointerDelegate>("Lui_GetPreviewTreeJson");
        GetLastError = Load<PointerDelegate>("Lui_GetLastError");
        GetSchemaJson = Load<SchemaDelegate>("Lui_GetSchemaJson");
    }

    private T Load<T>(string Name) where T : Delegate =>
        Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(Library, Name));

    internal string Error(IntPtr Runtime) => Marshal.PtrToStringUTF8(GetLastError(Runtime)) ?? "Native runtime error";
    internal string Schema() => Marshal.PtrToStringUTF8(GetSchemaJson()) ?? throw new InvalidDataException("Runtime schema unavailable");
    public void Dispose() => NativeLibrary.Free(Library);
}
