using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace Lui.Project;

// One managed adapter to the native prepared descriptor. No managed import,
// dependency, path discovery or source concatenation implementation.
internal sealed class PreparedProject : IDisposable
{
    [StructLayout(LayoutKind.Sequential)] private struct PrepareOptions
    {
        public uint StructSize, AbiVersion; public ulong Reserved;
        public IntPtr Context, Canceled;
        public ulong ManifestBytes, LockBytes, Owners, Files, FileBytes, SourceBytes, ArtifactBytes, BytecodeBytes, RetainedBytes;
    }
    [StructLayout(LayoutKind.Sequential)] private struct Summary
    {
        public uint StructSize, AbiVersion, PreparationVersion, SourceCount;
        public IntPtr Entry, SourceDigest, CompiledDigest, Descriptor;
        public ulong DescriptorBytes, BytecodeBytes, RetainedBytes;
    }
    [StructLayout(LayoutKind.Sequential)] private struct SourceView
    {
        public uint StructSize, AbiVersion;
        public IntPtr Id, Owner, Path, StagedPath, SourceHash, ArtifactHash, Source;
        public ulong SourceBytes; public IntPtr Bytecode; public ulong BytecodeBytes;
    }
    [StructLayout(LayoutKind.Sequential)] private struct Policy {public uint StructSize, AbiVersion, Mode, Reserved;}
    [StructLayout(LayoutKind.Sequential)] private struct RunOptions {public uint StructSize, AbiVersion, Flags, Depth; public ulong Reserved;}
    [StructLayout(LayoutKind.Sequential)] private struct NativeOptions {public uint StructSize, AbiVersion, Flags, Reserved;}
    [StructLayout(LayoutKind.Sequential)] private struct Capabilities {public uint StructSize, AbiVersion; public ulong Granted;}
    [StructLayout(LayoutKind.Sequential)] private struct NetworkPolicy {public uint StructSize, AbiVersion, Flags, ClientMin, ClientMax, ServerMin, ServerMax;}
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int PrepareCall(ref PrepareOptions Options, byte[] Path, ulong Bytes, out IntPtr Project, byte[] Error, ulong Capacity);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SummaryCall(IntPtr Project, ref Summary Value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SourceCall(IntPtr Project, uint Index, ref SourceView Value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void ReleaseCall(IntPtr Project);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr CreateCall(ref Policy Value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RunCall(IntPtr Runtime, IntPtr Project, ref RunOptions Value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int NativeCall(IntPtr Runtime, IntPtr Project, ref NativeOptions Value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int CapCall(IntPtr Runtime, ref Capabilities Value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int NetworkCall(IntPtr Runtime, ref NetworkPolicy Value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int AssetCall(IntPtr Runtime,[MarshalAs(UnmanagedType.LPUTF8Str)] string Name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr ErrorCall(IntPtr Runtime);
    private IntPtr Library;
    private IntPtr Handle;
    public JsonElement Manifest {get;}
    public string ManifestPath {get;}
    public string Digest {get;}
    public string SourceDigest {get;}
    public string Entry {get;}
    public uint DiagnosticMode {get;}
    public IReadOnlyList<SourceIdentity> Sources {get;}
    internal sealed record SourceIdentity(string Id,string Path,string Hash);
    private T Export<T>(string Name) where T:Delegate => Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(Library,Name));
    public PreparedProject(string Path,string RuntimePath,uint OwnerMode=1)
    {
        ManifestPath=System.IO.Path.GetFullPath(Path);
        Library=NativeLibrary.Load(System.IO.Path.GetFullPath(RuntimePath));
        try
        {
            PrepareOptions Options=new(){StructSize=(uint)Marshal.SizeOf<PrepareOptions>(),AbiVersion=1};
            byte[] Bytes=Encoding.UTF8.GetBytes(ManifestPath), Error=new byte[256];
            if(Export<PrepareCall>("Lui_ProjectPrepare")(ref Options,Bytes,(ulong)Bytes.Length,out Handle,Error,(ulong)Error.Length)==0)
                throw new InvalidDataException(Encoding.UTF8.GetString(Error).TrimEnd('\0'));
            Summary Value=new(){StructSize=(uint)Marshal.SizeOf<Summary>(),AbiVersion=1};
            if(Export<SummaryCall>("Lui_ProjectGetSummary")(Handle,ref Value)==0 || Value.DescriptorBytes>2*1024*1024 || Value.SourceCount is 0 or >256)
                throw new InvalidDataException("[LUI:Project] InvalidInput");
            using JsonDocument Document=JsonDocument.Parse(Marshal.PtrToStringUTF8(Value.Descriptor,checked((int)Value.DescriptorBytes))!);
            Manifest=Document.RootElement.GetProperty("Manifest").Clone(); Digest=Marshal.PtrToStringUTF8(Value.CompiledDigest)!;
            SourceDigest=Marshal.PtrToStringUTF8(Value.SourceDigest)!; Entry=Marshal.PtrToStringUTF8(Value.Entry)!;
            DiagnosticMode=OwnerMode==2 || Manifest.GetProperty("Diagnostics").GetProperty("Mode").GetString()=="Restricted" ? 2u : 1u;
            List<SourceIdentity> Identities=new();
            for(uint Index=0;Index<Value.SourceCount;++Index)
            {
                SourceView Source=new(){StructSize=(uint)Marshal.SizeOf<SourceView>(),AbiVersion=1};
                if(Export<SourceCall>("Lui_ProjectGetSource")(Handle,Index,ref Source)==0) throw new InvalidDataException("[LUI:Project] InvalidInput");
                Identities.Add(new(Marshal.PtrToStringUTF8(Source.Id)!,Marshal.PtrToStringUTF8(Source.StagedPath)!,Marshal.PtrToStringUTF8(Source.SourceHash)!));
            }
            Sources=Identities;
        }
        catch {Dispose(); throw;}
    }
    public IntPtr CreateRuntime()
    {
        Policy Value=new(){StructSize=(uint)Marshal.SizeOf<Policy>(),AbiVersion=1,Mode=DiagnosticMode};
        IntPtr Runtime=Export<CreateCall>("Lui_CreateWithPolicy")(ref Value);
        if(Runtime==IntPtr.Zero) throw new InvalidDataException("[LUI:Project] PolicyNotSupported"); return Runtime;
    }
    public bool HasPrivileges => Manifest.GetProperty("Capabilities").GetArrayLength()!=0;
    public void Configure(IntPtr Runtime,bool GrantRequested,bool AllowNative=false)
    {
        Dictionary<string,ulong> Known=new(){{"NativeExtensions",1},{"HostServices",2},{"Clipboard",4},{"Dialogs",8},{"network.client",16},{"network.server",32},{"network.raw",64}};
        ulong Requested=0; foreach(JsonElement Name in Manifest.GetProperty("Capabilities").EnumerateArray()) Requested|=Known[Name.GetString()!];
        if((Requested!=0 && !GrantRequested) || ((Requested&1)!=0 && !AllowNative)) throw new InvalidDataException("[LUI:Project] CapabilityDenied");
        Capabilities Caps=new(){StructSize=(uint)Marshal.SizeOf<Capabilities>(),AbiVersion=1,Granted=Requested};
        if(Export<CapCall>("Lui_DeclareCapabilities")(Runtime,ref Caps)==0) throw new InvalidDataException(Error(Runtime));
        foreach(JsonElement Name in Manifest.GetProperty("Assets").EnumerateArray())
            if(Export<AssetCall>("Lui_RegisterAsset")(Runtime,Name.GetString()!)==0) throw new InvalidDataException(Error(Runtime));
        if(Manifest.TryGetProperty("NetworkPolicy",out JsonElement Data))
        {
            NetworkPolicy Policy=new(){StructSize=(uint)Marshal.SizeOf<NetworkPolicy>(),AbiVersion=1,
                Flags=(Data.GetProperty("ClientLoopbackOnly").GetBoolean()?1u:0)|(Data.GetProperty("ServerLoopbackOnly").GetBoolean()?2u:0),
                ClientMin=Data.GetProperty("ClientPortMin").GetUInt32(),ClientMax=Data.GetProperty("ClientPortMax").GetUInt32(),
                ServerMin=Data.GetProperty("ServerPortMin").GetUInt32(),ServerMax=Data.GetProperty("ServerPortMax").GetUInt32()};
            if(Export<NetworkCall>("Lui_SetNetworkPolicy")(Runtime,ref Policy)==0) throw new InvalidDataException(Error(Runtime));
        }
        if((Requested&1)!=0)
        {
            NativeOptions Value=new(){StructSize=(uint)Marshal.SizeOf<NativeOptions>(),AbiVersion=1,Flags=1};
            if(Export<NativeCall>("Lui_LoadProjectNative")(Runtime,Handle,ref Value)==0) throw new InvalidDataException(Error(Runtime));
        }
    }
    public bool Run(IntPtr Runtime)
    {
        RunOptions Value=new(){StructSize=(uint)Marshal.SizeOf<RunOptions>(),AbiVersion=1};
        return Export<RunCall>("Lui_RunProject")(Runtime,Handle,ref Value)!=0;
    }
    public string Error(IntPtr Runtime) => Marshal.PtrToStringUTF8(Export<ErrorCall>("Lui_GetLastError")(Runtime))??"[LUI:Project] LoadFailed";
    public void Dispose() {if(Handle!=IntPtr.Zero) {Export<ReleaseCall>("Lui_ProjectRelease")(Handle); Handle=IntPtr.Zero;} if(Library!=IntPtr.Zero) {NativeLibrary.Free(Library); Library=IntPtr.Zero;}}
}
