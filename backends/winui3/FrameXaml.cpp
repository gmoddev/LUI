#include "FrameGpu.h"
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <microsoft.ui.xaml.media.dxinterop.h>

namespace LuiWinUI {
HRESULT SurfaceCreate(Frame* Value) {
    if(!Value||Value->OwnerCall()!=S_OK||Value->Source||!Value->Submitted||Value->CopyState.load()!=2||
        Value->Canceled.load()||Value->Upload)return E_INVALIDARG;
    HRESULT DeviceStatus=Value->Owner->Native->GetDeviceRemovedReason();if(FAILED(DeviceStatus))return DeviceStatus;
    using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
    SurfaceImageSource Source(static_cast<int>(Value->View.Width),static_cast<int>(Value->View.Height),false);
    auto Native=Source.as<ISurfaceImageSourceNative>();
    ComPtr<IDXGIDevice> Dxgi;HRESULT Status=Value->Owner->Bridge.As(&Dxgi);if(FAILED(Status))return Status;
    Status=Native->SetDevice(Dxgi.Get());if(FAILED(Status))return Status;
    D3D11_RESOURCE_FLAGS Flags{};Flags.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if(!Value->Wrapped){Status=Value->Owner->Interop->CreateWrappedResource(Value->Texture.Get(),&Flags,
        D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON,IID_PPV_ARGS(&Value->Wrapped));if(FAILED(Status))return Status;}
    RECT Rectangle{0,0,static_cast<LONG>(Value->View.Width),static_cast<LONG>(Value->View.Height)};
    POINT Offset{};ComPtr<IDXGISurface> Surface;
    Status=Native->BeginDraw(Rectangle,&Surface,&Offset);if(FAILED(Status))return Status;
    // EndDraw must run even when a downstream interface check fails.
    ComPtr<ID3D11Texture2D> Target;
    Status=Surface.As(&Target);
    if(SUCCEEDED(Status)) {
        ID3D11Resource* Wrapped[]={Value->Wrapped.Get()};
        Value->Owner->Interop->AcquireWrappedResources(Wrapped,1);
        D3D11_BOX Box{0,0,0,Value->View.Width,Value->View.Height,1};
        Value->Owner->Context->CopySubresourceRegion(Target.Get(),0,Offset.x,Offset.y,0,Value->Wrapped.Get(),0,&Box);
        Value->Owner->Interop->ReleaseWrappedResources(Wrapped,1);
        Value->Owner->Context->Flush();
    }
    HRESULT EndStatus=Native->EndDraw();
    Value->Owner->Context->Flush();
    HRESULT FenceStatus=Value->Owner->Signal(Value->FenceValue);
    if(FAILED(Status))return Status;if(FAILED(EndStatus))return EndStatus;if(FAILED(FenceStatus))return FenceStatus;
    Value->Source=Source.as<IUnknown>().get();++Sources;++Draws;return S_OK;
}
HRESULT SurfaceAttach(IUnknown* Image,Frame* Value) {
    if(!Image||(Value&&(!Value->Source||Value->OwnerCall()!=S_OK)))return E_INVALIDARG;
    winrt::com_ptr<IUnknown> Pointer;Pointer.copy_from(Image);
    auto Picture=Pointer.as<winrt::Microsoft::UI::Xaml::Controls::Image>();
    if(!Value)Picture.Source(nullptr);
    else {
        winrt::com_ptr<IUnknown> Source;Source.copy_from(Value->Source.Get());
        Picture.Source(Source.as<winrt::Microsoft::UI::Xaml::Media::ImageSource>());
    }
    return S_OK;
}
HRESULT SurfaceClose(Frame* Value) {
    if(!Value||Value->OwnerCall()!=S_OK)return E_INVALIDARG;
    if(Value->Source) {
        ComPtr<ISurfaceImageSourceNative> Source;HRESULT Status=Value->Source.As(&Source);
        if(FAILED(Status))return Status;
        Status=Source->SetDevice(nullptr);
        if(FAILED(Status)&&SUCCEEDED(Value->Owner->Native->GetDeviceRemovedReason()))return Status;
        Value->Source.Reset();--Sources;
    }
    Value->Wrapped.Reset();
    if(FAILED(Value->Owner->Native->GetDeviceRemovedReason()))return S_OK;
    Value->Owner->Context->Flush();
    return Value->Owner->Signal(Value->FenceValue);
}
HRESULT SurfaceRefresh(Frame* Value) {
    if(!Value||Value->OwnerCall()!=S_OK)return E_INVALIDARG;
    if(Value->Source){
        ComPtr<ISurfaceImageSourceNative> Source;HRESULT Status=Value->Source.As(&Source);if(FAILED(Status))return Status;
        Status=Source->SetDevice(nullptr);if(FAILED(Status))return Status;
        Value->Source.Reset();--Sources;
    }
    // Keep the immutable GPU texture and its wrapper. No CPU allocation/copy is
    // needed for surface-only loss; all draws remain on the same ordered queue.
    return SurfaceCreate(Value);
}
}
#define FRAME_EXPORT extern "C" __declspec(dllexport)
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameSurfaceCreateV1(LuiWinUI::Frame* Value) noexcept {
    try{return LuiWinUI::SurfaceCreate(Value);}catch(const winrt::hresult_error& Error){return Error.code();}catch(...){return E_FAIL;}
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameSurfaceAttachV1(IUnknown* Image,LuiWinUI::Frame* Value) noexcept {
    try{return LuiWinUI::SurfaceAttach(Image,Value);}catch(const winrt::hresult_error& Error){return Error.code();}catch(...){return E_FAIL;}
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameSurfaceCloseV1(LuiWinUI::Frame* Value) noexcept {
    try{return LuiWinUI::SurfaceClose(Value);}catch(const winrt::hresult_error& Error){return Error.code();}catch(...){return E_FAIL;}
}
FRAME_EXPORT HRESULT __cdecl LuiWinUI_FrameSurfaceRefreshV1(LuiWinUI::Frame* Value) noexcept {
    try{return LuiWinUI::SurfaceRefresh(Value);}catch(const winrt::hresult_error& Error){return Error.code();}catch(...){return E_FAIL;}
}
