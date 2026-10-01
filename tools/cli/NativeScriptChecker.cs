using System.Runtime.InteropServices;

namespace Lui.Cli;

internal static class NativeScriptChecker
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate nint CreateRuntime();

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void DestroyRuntime(nint Runtime);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int CheckScript(nint Runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string Source);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate nint GetLastError(nint Runtime);

    public static string? Check(string RuntimePath, string Source)
    {
        nint Library = NativeLibrary.Load(RuntimePath);
        nint Runtime = 0;
        try
        {
            CreateRuntime Create = Marshal.GetDelegateForFunctionPointer<CreateRuntime>(
                NativeLibrary.GetExport(Library, "Lui_Create"));
            DestroyRuntime Destroy = Marshal.GetDelegateForFunctionPointer<DestroyRuntime>(
                NativeLibrary.GetExport(Library, "Lui_Destroy"));
            CheckScript Compile = Marshal.GetDelegateForFunctionPointer<CheckScript>(
                NativeLibrary.GetExport(Library, "Lui_CheckScript"));
            GetLastError Error = Marshal.GetDelegateForFunctionPointer<GetLastError>(
                NativeLibrary.GetExport(Library, "Lui_GetLastError"));
            Runtime = Create();
            if (Runtime == 0) return "Unable to create the Luau checker";
            try
            {
                if (Compile(Runtime, Source) == 1) return null;
                return Marshal.PtrToStringUTF8(Error(Runtime)) ?? "Luau compilation failed";
            }
            finally
            {
                Destroy(Runtime);
                Runtime = 0;
            }
        }
        finally
        {
            NativeLibrary.Free(Library);
        }
    }
}
