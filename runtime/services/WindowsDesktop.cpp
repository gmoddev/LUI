#include "LuiApplicationServices.h"
#include "../internal/State.h"
#include "../internal/Dispatch.h"
#include <map>
#include <memory>
#include <algorithm>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

namespace {
constexpr UINT CallbackMessage = WM_APP + 43;
constexpr UINT NotificationId = 65535;
struct Icon { LuiNativeIdV2 Token; std::wstring Tooltip; };
struct Desktop {
    LuiRuntime* Runtime = nullptr;
    HWND Window = nullptr;
    UINT NextIcon = 1;
    bool Notification = false;
    std::map<UINT,Icon> Icons;
    static LRESULT CALLBACK Procedure(HWND Window,UINT Message,WPARAM First,LPARAM Second) {
        auto* Self = reinterpret_cast<Desktop*>(GetWindowLongPtrW(Window,GWLP_USERDATA));
        if (Message == WM_NCCREATE) {
            Self = static_cast<Desktop*>(reinterpret_cast<CREATESTRUCTW*>(Second)->lpCreateParams);
            SetWindowLongPtrW(Window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(Self));
        }
        if (Self && Message == CallbackMessage) {
            const UINT Id = HIWORD(Second), Event = LOWORD(Second);
            if (Id == NotificationId) {
                if (Event == NIN_BALLOONHIDE || Event == NIN_BALLOONTIMEOUT || Event == NIN_BALLOONUSERCLICK) {
                    auto Value = Self->Data(NotificationId); Shell_NotifyIconW(NIM_DELETE,&Value); Self->Notification = false;
                }
            } else if (Self->Runtime && (Event == NIN_SELECT || Event == NIN_KEYSELECT || Event == WM_CONTEXTMENU)) {
                const auto Found = Self->Icons.find(Id);
                if (Found != Self->Icons.end()) Lui_TrayActivatedV1(Self->Runtime,Found->second.Token);
            }
            return 0;
        }
        if (Self && Message == WM_TIMER && First == NotificationId) {
            KillTimer(Window,NotificationId);
            if (Self->Notification) { auto Value = Self->Data(NotificationId); Shell_NotifyIconW(NIM_DELETE,&Value); Self->Notification = false; }
            return 0;
        }
        return DefWindowProcW(Window,Message,First,Second);
    }
    bool Open() {
        if (Window) return true;
        static const ATOM Class = [] {
            WNDCLASSEXW Value{}; Value.cbSize = sizeof(Value); Value.lpfnWndProc = Procedure;
            Value.hInstance = GetModuleHandleW(nullptr); Value.lpszClassName = L"Lui.DesktopServices.V1";
            return RegisterClassExW(&Value);
        }();
        if (!Class) return false;
        Window = CreateWindowExW(0,L"Lui.DesktopServices.V1",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),this);
        return Window != nullptr;
    }
    NOTIFYICONDATAW Data(UINT Id) const {
        NOTIFYICONDATAW Value{}; Value.cbSize = sizeof(Value); Value.hWnd = Window; Value.uID = Id;
        Value.uFlags = NIF_MESSAGE | NIF_ICON; Value.uCallbackMessage = CallbackMessage;
        Value.hIcon = LoadIconW(nullptr,MAKEINTRESOURCEW(32512)); return Value;
    }
    bool Version(UINT Id) {
        auto Value = Data(Id); Value.uVersion = NOTIFYICON_VERSION_4;
        return Shell_NotifyIconW(NIM_SETVERSION,&Value) != FALSE;
    }
    ~Desktop() {
        Runtime = nullptr;
        for (const auto& Pair : Icons) { auto Value = Data(Pair.first); Shell_NotifyIconW(NIM_DELETE,&Value); }
        if (Notification) { auto Value = Data(NotificationId); Shell_NotifyIconW(NIM_DELETE,&Value); }
        if (Window) { SetWindowLongPtrW(Window,GWLP_USERDATA,0); DestroyWindow(Window); }
    }
};
bool Wide(const char* Input,std::wstring& Output,size_t Capacity) {
    const int Length = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,Input,-1,nullptr,0);
    if (Length <= 0 || static_cast<size_t>(Length) > Capacity) return false;
    Output.resize(static_cast<size_t>(Length));
    if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,Input,-1,Output.data(),Length) != Length) return false;
    Output.resize(static_cast<size_t>(Length-1)); return true;
}
uint32_t LUI_CALL Notify(void* Context,const char* Title,const char* Body) {
    auto& Self = *static_cast<Desktop*>(Context); std::wstring Caption, Content;
    if (!Wide(Title,Caption,64) || !Wide(Body,Content,256)) return LUI_NATIVE_INVALID_ARGUMENT;
    if (!Self.Open()) return LUI_NATIVE_UNSUPPORTED;
    auto Value = Self.Data(NotificationId);
    if (!Self.Notification) {
        if (!Shell_NotifyIconW(NIM_ADD,&Value)) return LUI_NATIVE_UNSUPPORTED;
        Self.Notification = true;
        if (!Self.Version(NotificationId)) { Shell_NotifyIconW(NIM_DELETE,&Value); Self.Notification = false; return LUI_NATIVE_PROVIDER_FAILURE; }
    }
    Value.uFlags = NIF_INFO; Value.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    std::copy(Caption.begin(),Caption.end(),Value.szInfoTitle); std::copy(Content.begin(),Content.end(),Value.szInfo);
    if (!Shell_NotifyIconW(NIM_MODIFY,&Value) || !SetTimer(Self.Window,NotificationId,6000,nullptr)) {
        Shell_NotifyIconW(NIM_DELETE,&Value); Self.Notification = false; return LUI_NATIVE_PROVIDER_FAILURE;
    }
    return 0;
}
uint32_t LUI_CALL Create(void* Context,LuiNativeIdV2 Token,const char* Tooltip) {
    auto& Self = *static_cast<Desktop*>(Context); std::wstring Text;
    if (!Wide(Tooltip,Text,128)) return LUI_NATIVE_INVALID_ARGUMENT;
    if (!Self.Open()) return LUI_NATIVE_UNSUPPORTED;
    if (Self.NextIcon >= NotificationId || Self.Icons.size() >= 64) return LUI_NATIVE_LIMIT_EXCEEDED;
    const UINT Id = Self.NextIcon++; auto Value = Self.Data(Id); Value.uFlags |= NIF_TIP | NIF_SHOWTIP;
    std::copy(Text.begin(),Text.end(),Value.szTip);
    // Allocate bookkeeping before creating an OS resource.
    Self.Icons.emplace(Id,Icon{Token,Text});
    if (!Shell_NotifyIconW(NIM_ADD,&Value)) { Self.Icons.erase(Id); return LUI_NATIVE_UNSUPPORTED; }
    if (!Self.Version(Id)) { Shell_NotifyIconW(NIM_DELETE,&Value); Self.Icons.erase(Id); return LUI_NATIVE_PROVIDER_FAILURE; }
    return 0;
}
void LUI_CALL Remove(void* Context,LuiNativeIdV2 Token) {
    auto& Self = *static_cast<Desktop*>(Context);
    for (auto Item = Self.Icons.begin(); Item != Self.Icons.end(); ++Item) if (Item->second.Token.Session == Token.Session && Item->second.Token.Value == Token.Value) {
        auto Value = Self.Data(Item->first); Shell_NotifyIconW(NIM_DELETE,&Value); Self.Icons.erase(Item); return;
    }
}
}
#endif
extern "C" LUI_API uint32_t LUI_CALL Lui_InstallWindowsDesktopV1(LuiRuntime* Runtime) {
#if defined(_WIN32)
    if (!CheckOwner(Runtime) || Runtime->DesktopBridgeOwner) return LUI_NATIVE_INVALID_ARGUMENT;
    try {
        auto Owner = std::make_shared<Desktop>(); Owner->Runtime = Runtime;
        LuiDesktopServicesV1 Bridge{sizeof(Bridge),1,Owner.get(),LUI_CAPABILITY_NOTIFICATIONS | LUI_CAPABILITY_TRAY,Notify,Create,Remove};
        const auto Status = Lui_InstallDesktopServicesV1(Runtime,&Bridge);
        if (Status) return Status;
        Runtime->DesktopBridgeOwner = Owner; return 0;
    } catch (...) { return LUI_NATIVE_PROVIDER_FAILURE; }
#else
    (void)Runtime; return LUI_NATIVE_UNSUPPORTED;
#endif
}
