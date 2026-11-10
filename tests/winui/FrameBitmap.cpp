#include "LuiRuntime.h"
#include <windows.h>
#include <roapi.h>
#include <windows.graphics.imaging.interop.h>
#include <wincodec.h>
#include <wrl.h>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" __declspec(dllimport) HRESULT __cdecl LuiWinUI_CreateFrameBitmapV1(LuiFrame*, IInspectable**) noexcept;
using Microsoft::WRL::ComPtr;
namespace {
int Failures=0;
LuiFrame* Captured=nullptr;
void Check(bool Result,const char* Message) {if (!Result) {++Failures;std::fprintf(stderr,"[LUI:FrameBitmapTest] %s\n",Message);}}
int LUI_CALL Submit(void*,int,LuiFrame* Frame,uint32_t) {
    Check(Lui_FrameRetain(Frame)==1,"capture lease"); Captured=Frame; return Lui_FrameComplete(Frame,1);
}
int LUI_CALL Publish(void*,int,LuiFrame*,uint32_t) {return 1;}
void LUI_CALL Clear(void*,int,uint64_t) {}
LuiFrameStatsV1 Stats(LuiRuntime* Runtime) {LuiFrameStatsV1 Value{sizeof(Value),1}; Check(Lui_GetFrameStats(Runtime,&Value)==1,"stats");return Value;}
}
int main() {
    Check(SUCCEEDED(RoInitialize(RO_INIT_MULTITHREADED)),"WinRT apartment");
    auto* Runtime=Lui_Create(); LuiFrameCallbacksV1 Callbacks{sizeof(Callbacks),1,nullptr,Submit,Publish,Clear,0};
    Check(Lui_SetFrameCallbacks(Runtime,&Callbacks)==1,"callbacks");
    Check(Lui_RunScript(Runtime,R"(
        W=Instance.new('Window',{Visible=true}); S=Instance.new('FrameSurface',{Parent=W})
        B=buffer.create(16); for I=0,15 do buffer.writeu8(B,I,I) end
        assert(S:SubmitFrame(B,2,2,8,'BGRA8Premultiplied')); buffer.fill(B,0,99)
    )","FrameBitmapTest")==1,"public owned submission");
    Lui_Pump(Runtime);
    ComPtr<IInspectable> Bitmap;
    Check(SUCCEEDED(LuiWinUI_CreateFrameBitmapV1(Captured,&Bitmap)),"native read-only SoftwareBitmap factory");
    ComPtr<ISoftwareBitmapNative> Native; Check(SUCCEEDED(Bitmap.As(&Native)),"native bitmap interop");
    ComPtr<IWICBitmap> Wic; Check(SUCCEEDED(Native->GetData(IID_PPV_ARGS(&Wic))),"retained WIC data");
    UINT Width=0,Height=0; Check(SUCCEEDED(Wic->GetSize(&Width,&Height))&&Width==2&&Height==2,"dimensions");
    WICPixelFormatGUID Format{}; Check(SUCCEEDED(Wic->GetPixelFormat(&Format))&&Format==GUID_WICPixelFormat32bppPBGRA,"premultiplied BGRA");
    BYTE Pixels[16]{}; Check(SUCCEEDED(Wic->CopyPixels(nullptr,8,16,Pixels))&&Pixels[0]==0&&Pixels[15]==15,"immutable pixel copy");
    WICRect Region{1,1,1,1}; BYTE Part[4]{};
    Check(SUCCEEDED(Wic->CopyPixels(&Region,4,4,Part))&&Part[0]==12,"region orientation");
    Region={-1,0,1,1}; Check(FAILED(Wic->CopyPixels(&Region,4,4,Part)),"negative region");
    Region={0,0,INT_MAX,2}; Check(FAILED(Wic->CopyPixels(&Region,4,4,Part)),"overflow region");
    Check(FAILED(Wic->CopyPixels(nullptr,7,16,Pixels))&&FAILED(Wic->CopyPixels(nullptr,8,15,Pixels)),"stride/length validation");
    ComPtr<IWICBitmapLock> Lock;
    Check(FAILED(Wic->Lock(nullptr,WICBitmapLockWrite,&Lock)),"write lock refused");
    Check(SUCCEEDED(Wic->Lock(nullptr,WICBitmapLockRead,&Lock)),"read lock");
    UINT Length=0; BYTE* Pointer=nullptr;
    Check(SUCCEEDED(Lock->GetDataPointer(&Length,&Pointer))&&Length==16&&Pointer[15]==15,"borrowed lock pixels");
    Check(FAILED(Wic->SetResolution(72,72))&&FAILED(Wic->SetPalette(nullptr)),"immutable metadata");
    Lui_FrameRelease(Captured); Captured=nullptr;
    Lui_Destroy(Runtime); // Native bitmap/lock references keep the owned pixels charged.
    Runtime=Lui_Create(); Check(Lui_RunScript(Runtime,"S=Instance.new('FrameSurface')","Replacement")==1,"replacement runtime");
    Check(Stats(Runtime).PixelBytes==16,"native ownership survives runtime destruction");
    Lock.Reset(); Wic.Reset(); Native.Reset(); Bitmap.Reset();
    Check(Stats(Runtime).PixelBytes==0,"final native release frees frame charge");
    Lui_Destroy(Runtime); RoUninitialize();
    if (!Failures) std::puts("[LUI:FrameBitmapTest] native bitmap ownership, immutability, regions and retirement passed");
    return Failures?1:0;
}
