using System.Runtime.InteropServices;
namespace Lui.WinUI;
internal sealed partial class WinUIBackend {
    private static partial class Native {
        [StructLayout(LayoutKind.Sequential)] internal struct FrameView {internal uint StructSize,AbiVersion;internal IntPtr Pixels;internal ulong ByteCount,Epoch,Sequence;internal uint Width,Height,RowStride,Format;}
        [StructLayout(LayoutKind.Sequential)] internal struct FrameStats {internal uint StructSize,AbiVersion;internal ulong Surfaces,Frames,PixelBytes,StagingBytes,PeakBytes,Pending,InFlight,Accepted,Replaced,Rejected,Consumed,Stale;}
        [StructLayout(LayoutKind.Sequential)] internal struct FrameGpuStats {internal uint StructSize,Version;internal ulong Uploads,UploadBytes,PeakUploadBytes,Devices,Sources,Frames,Copies,Draws,Removed;}
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int FrameSubmitCallback(IntPtr Context,int Id,IntPtr Frame,uint Mode);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void FrameClearCallback(IntPtr Context,int Id,ulong Epoch);
        [StructLayout(LayoutKind.Sequential)] internal struct FrameCallbacks {internal uint StructSize,AbiVersion;internal IntPtr Context;internal FrameSubmitCallback Submit,Publish;internal FrameClearCallback Clear;internal ulong Reserved;}
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int Lui_SetFrameCallbacks(IntPtr Runtime,ref FrameCallbacks Callbacks);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int Lui_FrameRetain(IntPtr Frame);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern void Lui_FrameRelease(IntPtr Frame);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int Lui_FrameView(IntPtr Frame,ref FrameView View);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int Lui_FrameComplete(IntPtr Frame,int Success);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int Lui_FrameReserveStaging(IntPtr Frame,ulong Bytes);
        [DllImport("LuiRuntime.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int Lui_GetFrameStats(IntPtr Runtime,ref FrameStats Stats);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FramePresenterVersionV1();
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameDeviceCreateV1(uint Mode,out IntPtr Device);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern void LuiWinUI_FrameDeviceReleaseV1(IntPtr Device);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameDeviceBindOwnerV1(IntPtr Device);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameDeviceRemovedV1(IntPtr Device);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameDeviceRemoveForTestV1(IntPtr Device);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameBeginV1(IntPtr Device,IntPtr Frame,out IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameCopyGateForTestV1(IntPtr Upload,IntPtr Entered,IntPtr Resume);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameCopyV1(IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern void LuiWinUI_FrameCancelV1(IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameGpuGateForTestV1(IntPtr Upload,int Enable);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameSubmitCopyV1(IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FramePollV1(IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameReleaseV1(IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameGpuStatsV1(ref FrameGpuStats Stats);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameSurfaceCreateV1(IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameSurfaceAttachV1(IntPtr Image,IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameSurfaceCloseV1(IntPtr Upload);
        [DllImport("LuiWinUIFrames.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int LuiWinUI_FrameSurfaceRefreshV1(IntPtr Upload);
    }
}
