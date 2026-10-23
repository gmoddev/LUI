#include "LuiApplicationServices.h"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void Require(bool Value,const char* Message) { if (!Value) throw std::runtime_error(Message); }
struct Bridge { int Notifications = 0, Created = 0, Removed = 0; LuiNativeIdV2 Icon{}; };
uint32_t LUI_CALL Notify(void* Pointer,const char* Title,const char* Body) {
    auto& Value = *static_cast<Bridge*>(Pointer); ++Value.Notifications;
    Require(std::string(Title) == "Synthetic title" && std::string(Body) == "Synthetic body","validated notification payload"); return 0;
}
uint32_t LUI_CALL Create(void* Pointer,LuiNativeIdV2 Icon,const char* Tooltip) {
    auto& Value = *static_cast<Bridge*>(Pointer); ++Value.Created; Value.Icon = Icon;
    Require(std::string(Tooltip) == "Synthetic tray","validated tooltip"); return 0;
}
void LUI_CALL Remove(void* Pointer,LuiNativeIdV2 Icon) {
    auto& Value = *static_cast<Bridge*>(Pointer); ++Value.Removed;
    Require(Icon.Session == Value.Icon.Session && Icon.Value == Value.Icon.Value,"typed icon removal");
}
void Run(LuiRuntime* Runtime,const char* Source) { Require(Lui_RunScript(Runtime,Source,"DesktopTest"),Lui_GetLastError(Runtime)); }
}
int main() {
    try {
        auto* Domain = Lui_CreateNativeDomainV2(); LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_RESTRICTED,0};
        auto* Runtime = Lui_CreateWithPolicy(&Policy); Require(Lui_SetNativeDomainV2(Runtime,Domain) == 0,"domain");
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants),1,LUI_CAPABILITY_NOTIFICATIONS | LUI_CAPABILITY_TRAY};
        Require(Lui_DeclareCapabilities(Runtime,&Grants),"separate desktop grants without HostServices");
        Bridge Value; LuiDesktopServicesV1 Callbacks{sizeof(Callbacks),1,&Value,Grants.GrantedCapabilities,Notify,Create,Remove};
        Require(Lui_InstallDesktopServicesV1(Runtime,&Callbacks) == 0,"bridge install");
        Run(Runtime,
            "local Platform=app:GetService('PlatformService'); assert(Platform:Supports('Notifications') and Platform:Supports('Tray'))\n"
            "local N=app:GetService('NotificationService'); local T=app:GetService('TrayService')\n"
            "NotifyDone=0; TrayDone=0; TrayClicks=0; TrayClosed=0\n"
            "N:Show(buffer.fromstring('{\"Title\":\"Synthetic title\",\"Body\":\"Synthetic body\"}'),nil,function(_,_,Error) assert(not Error); NotifyDone+=1 end)\n"
            "local Cancel=N:Show(buffer.fromstring('{\"Title\":\"Synthetic title\",\"Body\":\"Synthetic body\"}'),nil,function(_,_,Error) assert(Error=='Canceled'); NotifyDone+=1 end)\n"
            "assert(Cancel:Cancel())\n"
            "N:Show(buffer.fromstring('{\"Title\":\"SYNTHETIC_PRIVATE\",\"Body\":99}'),nil,function(_,_,Error) assert(Error=='InvalidArgument'); NotifyDone+=1 end)\n"
            "T:Create(buffer.fromstring('Synthetic tray'),nil,function(_,Handle,Error)\n"
            " assert(Handle and not Error); TrayIcon=Handle; TrayDone+=1\n"
            " TrayWatch=T:Watch(buffer.create(0),Handle,function(_,_,Failure) assert(Failure=='Closed'); TrayClosed+=1 end,\n"
            " function(Input) assert(buffer.len(Input)==0); TrayClicks+=1 end)\n"
            "end)\n"
            "assert(NotifyDone==0 and TrayDone==0)\n");
        Require(!Value.Notifications && !Value.Created,"native side effects were not deferred");
        Lui_Pump(Runtime);
        Run(Runtime,"assert(NotifyDone==3 and TrayDone==1 and TrayClicks==0)");
        Require(Value.Notifications == 1 && Value.Created == 1,"canceled/malformed payload reached native bridge");
        Require(Lui_TrayActivatedV1(Runtime,{Value.Icon.Session+1,Value.Icon.Value}) == LUI_NATIVE_INVALID_ARGUMENT,"foreign activation admitted");
        for (int Index = 0; Index < 64; ++Index) Require(Lui_TrayActivatedV1(Runtime,Value.Icon) == 0,"activation admission");
        Require(Lui_TrayActivatedV1(Runtime,Value.Icon) == LUI_NATIVE_WOULD_BLOCK,"activation flood did not backpressure");
        Run(Runtime,"assert(TrayClicks==0)"); Lui_Pump(Runtime); Run(Runtime,"assert(TrayClicks==64); assert(TrayIcon:Close() and TrayIcon:Close())");
        Require(Lui_TrayActivatedV1(Runtime,Value.Icon) == LUI_NATIVE_CLOSED,"closed icon admitted activation before cleanup pump");
        Lui_Pump(Runtime); Run(Runtime,"assert(TrayClosed==1)");
        Require(Value.Removed == 1 && Lui_TrayActivatedV1(Runtime,Value.Icon) != 0,"icon removal/lateness");
        Require(std::string(Lui_GetDiagnosticsJson(Runtime)).find("SYNTHETIC_PRIVATE") == std::string::npos,"desktop private data in diagnostics");
        Lui_Destroy(Runtime); Lui_PumpNativeDomainV2(Domain);
        LuiNativeStatsV2 Stats{sizeof(Stats),2}; Lui_GetNativeStatsV2(Domain,&Stats);
        Require(!Stats.Sessions && !Stats.Bytes && !Stats.Handles,"desktop ownership leaked");
        Runtime = Lui_CreateWithPolicy(&Policy);
        Run(Runtime,"assert(not app:GetService('PlatformService'):Supports('Notifications')); assert(not app:GetService('PlatformService'):Supports('Tray')); assert(not pcall(function() app:GetService('TrayService') end))");
#if !defined(_WIN32)
        Require(Lui_InstallWindowsDesktopV1(Runtime) == LUI_NATIVE_UNSUPPORTED,"non-Windows shell adapter fabricated support");
#endif
        Lui_Destroy(Runtime); Lui_ReleaseNativeDomainV2(Domain);
        std::puts("[LUI:DesktopTest] grants, deferred delivery, cancellation, activation, backpressure and removal passed"); return 0;
    } catch (const std::exception& Error) { std::fprintf(stderr,"[LUI:DesktopTest] %s\n",Error.what()); return 1; }
}
