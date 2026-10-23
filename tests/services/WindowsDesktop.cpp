#include "LuiApplicationServices.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void Require(bool Value,const char* Message) { if (!Value) throw std::runtime_error(Message); }
void Run(LuiRuntime* Runtime,const char* Source) { Require(Lui_RunScript(Runtime,Source,"WindowsDesktopProof"),Lui_GetLastError(Runtime)); }
void Messages() {
    MSG Message{};
    while (PeekMessageW(&Message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&Message); DispatchMessageW(&Message); }
}
HWND OwnedBridge() {
    HWND Current = nullptr, Result = nullptr;
    while ((Current = FindWindowExW(HWND_MESSAGE,Current,L"Lui.DesktopServices.V1",nullptr))) {
        DWORD Process = 0; GetWindowThreadProcessId(Current,&Process);
        if (Process == GetCurrentProcessId()) { Require(!Result,"ambiguous owned bridge"); Result = Current; }
    }
    return Result;
}
bool IconPresent(HWND Window,UINT Id) {
    NOTIFYICONIDENTIFIER Value{}; Value.cbSize = sizeof(Value); Value.hWnd = Window; Value.uID = Id;
    RECT Bounds{}; return SUCCEEDED(Shell_NotifyIconGetRect(&Value,&Bounds)) && Bounds.right > Bounds.left && Bounds.bottom > Bounds.top;
}
}
int main(int Count,char** Arguments) {
    SetThreadErrorMode(GetThreadErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX,nullptr);
    LuiRuntime* Runtime = nullptr;
    try {
        const bool Observe = Count == 2 && std::string(Arguments[1]) == "--observe";
        LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_RESTRICTED,0}; Runtime = Lui_CreateWithPolicy(&Policy);
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants),1,LUI_CAPABILITY_NOTIFICATIONS | LUI_CAPABILITY_TRAY};
        Require(Runtime && Lui_DeclareCapabilities(Runtime,&Grants),"owner grants");
        Require(Lui_InstallWindowsDesktopV1(Runtime) == 0,"Windows shell adapter install");
        Run(Runtime,
            "NotificationReady=false; IconReady=false; ActivationCount=0; WatchClosed=false\n"
            "app:GetService('NotificationService'):Show(buffer.fromstring('{\"Title\":\"LUI service proof\",\"Body\":\"Synthetic notification qualification\"}'),nil,\n"
            " function(_,_,Error) assert(not Error); NotificationReady=true end)\n"
            "app:GetService('TrayService'):Create(buffer.fromstring('LUI Service Proof'),nil,function(_,Handle,Error)\n"
            " assert(not Error and Handle); IconReady=true; Icon=Handle\n"
            " app:GetService('TrayService'):Watch(buffer.create(0),Handle,function(_,_,Failure) assert(Failure=='Closed'); WatchClosed=true end,\n"
            " function(Bytes) assert(buffer.len(Bytes)==0); ActivationCount+=1 end)\n"
            "end)\n");
        Lui_Pump(Runtime); Messages(); Run(Runtime,"assert(NotificationReady and IconReady)");
        const auto Window = OwnedBridge(); Require(Window && IconPresent(Window,1),"Shell did not report the created tray icon");
        // Exercise the adapter's real Win32 callback path using the exact
        // version-4 message shape, without injecting mouse/keyboard input.
        SendMessageW(Window,WM_APP+43,0,MAKELPARAM(NIN_SELECT,1));
        Run(Runtime,"assert(ActivationCount==0)"); Lui_Pump(Runtime); Run(Runtime,"assert(ActivationCount==1)");
        std::puts("[LUI:WindowsDesktopProof] Shell submission, icon rectangle and deferred activation passed"); std::fflush(stdout);
        HWND Status = nullptr;
        if (Observe) {
            Status = CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"LUI Service Qualification",WS_OVERLAPPEDWINDOW | SS_LEFT,
                CW_USEDEFAULT,CW_USEDEFAULT,520,190,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            Require(Status != nullptr,"status window");
            SetWindowTextW(Status,L"LUI native services\r\n\r\nWindows shell accepted the notification.\r\nTray icon is registered; activation reached Luau on its owner.\r\nAutomatic close and resource cleanup follow this observation.");
            ShowWindow(Status,SW_HIDE); ShowWindow(Status,SW_SHOWNOACTIVATE); UpdateWindow(Status);
            const auto Until = std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while (IsWindow(Status) && std::chrono::steady_clock::now() < Until) {
                Messages(); Lui_Pump(Runtime); std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        Run(Runtime,"assert(Icon:Close() and Icon:Close())"); Lui_Pump(Runtime); Messages();
        Run(Runtime,"assert(WatchClosed)"); Require(!IconPresent(Window,1),"closed tray icon remained registered");
        if (Status && IsWindow(Status)) DestroyWindow(Status);
        Lui_Destroy(Runtime); Runtime = nullptr; Require(!IsWindow(Window),"private event window survived runtime destruction");
        std::puts("[LUI:WindowsDesktopProof] Native icon removal, watch closure and runtime teardown passed"); return 0;
    } catch (const std::exception& Error) {
        if (Runtime) Lui_Destroy(Runtime);
        std::fprintf(stderr,"[LUI:WindowsDesktopProof] %s\n",Error.what()); return 1;
    }
}
