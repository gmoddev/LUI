#include "LuiRuntime.h"
#include <windows.h>
#include <roapi.h>
#include <windows.graphics.imaging.interop.h>
#include <wincodec.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstring>
#include <new>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

namespace {
class FrameLock final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IWICBitmapLock> {
public:
    ComPtr<IWICBitmap> Owner;
    LuiFrameViewV1 View{};
    WICRect Rect{};
    HRESULT STDMETHODCALLTYPE GetSize(UINT* Width, UINT* Height) override {
        if (!Width || !Height) return E_POINTER; *Width = Rect.Width; *Height = Rect.Height; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStride(UINT* Stride) override {
        if (!Stride) return E_POINTER; *Stride = View.RowStride; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataPointer(UINT* Length, BYTE** Pixels) override {
        if (!Length || !Pixels) return E_POINTER;
        *Length = (Rect.Height-1)*View.RowStride + Rect.Width*4;
        *Pixels = const_cast<BYTE*>(static_cast<const BYTE*>(View.Pixels)) + Rect.Y*View.RowStride + Rect.X*4;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* Format) override {
        if (!Format) return E_POINTER; *Format = GUID_WICPixelFormat32bppPBGRA; return S_OK;
    }
};
class FrameBitmap final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IWICBitmap> {
    LuiFrame* Lease = nullptr;
    LuiFrameViewV1 View{sizeof(View),1};
    bool Region(const WICRect* Requested, WICRect& Result) const {
        Result = Requested ? *Requested : WICRect{0,0,static_cast<INT>(View.Width),static_cast<INT>(View.Height)};
        return Result.X >= 0 && Result.Y >= 0 && Result.Width > 0 && Result.Height > 0 &&
            static_cast<UINT>(Result.X) < View.Width && static_cast<UINT>(Result.Y) < View.Height &&
            static_cast<UINT>(Result.Width) <= View.Width-static_cast<UINT>(Result.X) &&
            static_cast<UINT>(Result.Height) <= View.Height-static_cast<UINT>(Result.Y);
    }
public:
    ~FrameBitmap() {Lui_FrameRelease(Lease);}
    HRESULT Initialize(LuiFrame* Frame) {
        if (!Frame || !Lui_FrameView(Frame,&View) || View.Format != 1 || !View.Pixels ||
            !View.Width || View.Width > 1920 || !View.Height || View.Height > 1080 ||
            View.RowStride != View.Width*4 || View.ByteCount != uint64_t(View.RowStride)*View.Height ||
            View.ByteCount > 8*1024*1024 || !Lui_FrameRetain(Frame)) return E_INVALIDARG;
        Lease = Frame; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* Width, UINT* Height) override {
        if (!Width || !Height) return E_POINTER; *Width=View.Width; *Height=View.Height; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* Format) override {
        if (!Format) return E_POINTER; *Format=GUID_WICPixelFormat32bppPBGRA; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResolution(double* X, double* Y) override {
        if (!X || !Y) return E_POINTER; *X=96; *Y=96; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette*) override {return WINCODEC_ERR_PALETTEUNAVAILABLE;}
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* Requested, UINT Stride, UINT Capacity, BYTE* Pixels) override {
        WICRect Rect;
        if (!Region(Requested,Rect) || !Pixels || Stride < UINT(Rect.Width)*4 ||
            uint64_t(Rect.Height-1)*Stride + uint64_t(Rect.Width)*4 > Capacity) return E_INVALIDARG;
        for (UINT Row=0;Row<UINT(Rect.Height);++Row)
            std::memcpy(Pixels + size_t(Row)*Stride,static_cast<const BYTE*>(View.Pixels) +
                size_t(Rect.Y+Row)*View.RowStride + size_t(Rect.X)*4, size_t(Rect.Width)*4);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Lock(const WICRect* Requested, DWORD Flags, IWICBitmapLock** Result) override {
        if (!Result) return E_POINTER; *Result=nullptr;
        WICRect Rect;
        if (Flags != WICBitmapLockRead) return E_ACCESSDENIED;
        if (!Region(Requested,Rect)) return E_INVALIDARG;
        auto Lock=Make<FrameLock>(); if (!Lock) return E_OUTOFMEMORY;
        Lock->Owner=this; Lock->View=View; Lock->Rect=Rect;
        return Lock.CopyTo(Result);
    }
    HRESULT STDMETHODCALLTYPE SetPalette(IWICPalette*) override {return E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE SetResolution(double,double) override {return E_ACCESSDENIED;}
};
}

// Private WinUI-only helper. The SoftwareBitmap's IWICBitmap retains the frame
// lease through actual native COM lifetime, including SDK worker retirement.
// No toolkit object or pointer is exposed to Luau or either extension ABI.
extern "C" __declspec(dllexport) HRESULT __cdecl LuiWinUI_CreateFrameBitmapV1(LuiFrame* Frame, IInspectable** Result) noexcept {
    if (!Result) return E_POINTER; *Result=nullptr;
    try {
        auto Bitmap=Make<FrameBitmap>(); if (!Bitmap) return E_OUTOFMEMORY;
        HRESULT Status=Bitmap->Initialize(Frame); if (FAILED(Status)) return Status;
        ComPtr<ISoftwareBitmapNativeFactory> Factory;
        Microsoft::WRL::Wrappers::HStringReference Name(L"Windows.Graphics.Imaging.SoftwareBitmap");
        Status=RoGetActivationFactory(Name.Get(),IID_PPV_ARGS(&Factory)); if (FAILED(Status)) return Status;
        return Factory->CreateFromWICBitmap(Bitmap.Get(),TRUE,__uuidof(IInspectable),reinterpret_cast<void**>(Result));
    } catch (const std::bad_alloc&) {return E_OUTOFMEMORY;}
    catch (...) {return E_FAIL;}
}
