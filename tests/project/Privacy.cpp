#include "LuiRuntime.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>
#include <stdexcept>
#include "../../runtime/diagnostics/NativeHost.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
static void Require(bool Value,const char* Text) {if(!Value) throw std::runtime_error(Text);}
static void LUI_CALL Log(void* Context,const char*,const char* Message) {static_cast<std::string*>(Context)->append(Message);}
static int LUI_CALL NativeFailure(void* Context,const LuiValueV1*,uint32_t,LuiValueV1*) {
    Lui_SetHostError(static_cast<LuiRuntime*>(Context),"PRIVATE_EXTENSION_CANARY"); return 0;
}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    try {
        for(uint32_t Mode:{LUI_DIAGNOSTICS_DEVELOPMENT,LUI_DIAGNOSTICS_RESTRICTED}) {
            LuiDiagnosticPolicyV1 Policy{sizeof(Policy),LUI_DIAGNOSTICS_ABI_VERSION,Mode,0}; auto* App=Lui_CreateWithPolicy(&Policy);
            Require(App!=nullptr,"policy creation failed"); std::string Logs; Lui_SetLogCallback(App,&Logs,Log);
            LuiCapabilityDeclarationV1 Grants{sizeof(Grants),1,LUI_CAPABILITY_HOST_SERVICES};
            Require(Lui_DeclareCapabilities(App,&Grants) && Lui_RegisterHostMethod(App,"FaultProbe","Fail","() -> boolean",NativeFailure,App),"policy host fixture failed");
            for (const auto& Case : {std::make_pair("HttpService", "NetworkClientRequired"),
                    std::make_pair("HttpServerService", "NetworkServerRequired"), std::make_pair("NetworkService", "NetworkRawRequired")}) {
                const std::string Source = std::string("app:GetService('") + Case.first + "')";
                Require(!Lui_RunScript(App, Source.c_str(), "PRIVATE_CAPABILITY_SOURCE_CANARY"), "ungranted service accepted");
                const std::string Expected = std::string("[LUI:Capabilities] ") + Case.second;
                Require(Lui_GetLastError(App) == Expected, "missing grant lost its fixed reason");
                Require(Logs.find("PRIVATE_") == std::string::npos, "capability error leaked source");
            }
            Require(Lui::Diagnostics::NativeHost::GetCode("PRIVATE_CANARY: [LUI:Capabilities] NetworkServerRequired") != nullptr,
                "host cannot recognize projected capability");
            Require(Lui::Diagnostics::NativeHost::GetCode("PRIVATE_CANARY") == nullptr, "host accepted free-form diagnostic");
            Require(Lui_RunScript(App,R"(
                local Window=Instance.new('Window',{Title='PUBLIC_TITLE'})
                Parent=Instance.new('Frame',{Parent=Window,Sensitive=true,Name='PRIVATE_NAME_CANARY'})
                Child=Instance.new('TextBox',{Text='PRIVATE_TEXT_CANARY',Parent=Parent,AccessibilityLabel='PRIVATE_ACCESS_CANARY'})
                assert(Child.EffectiveSensitive and Child.Text=='PRIVATE_TEXT_CANARY')
                Parent.Sensitive=false; Child.Parent=Window; assert(Child.EffectiveSensitive)
                Copy=Child:Clone(); assert(Copy.EffectiveSensitive)
                assert(not pcall(function() Child.EffectiveSensitive=false end))
                Public=Instance.new('TextLabel',{Parent=Window,Text='PUBLIC_TEXT'})
                DiagnosticButton=Instance.new('TextButton',{Parent=Window})
                DiagnosticButton.Activated:Connect(function() error(Child.Text) end)
                local Ok,Message=pcall(function() return app:GetService('FaultProbe'):Fail() end)
                assert(not Ok and not string.find(Message,'PRIVATE_EXTENSION_CANARY'))
                Window.Visible=true
            )","PRIVATE_SOURCE_CANARY"),Lui_GetLastError(App));
            std::string Tree=Lui_GetPreviewTreeJson(App);
            for(const auto& Node:nlohmann::json::parse(Tree)) if(Node.at("className")=="TextButton")
                Require(Lui_Activate(App,Node.at("id").get<int>()),"policy callback fixture failed");
            Require(Logs.find("PRIVATE_")==std::string::npos,"callback/native error leaked");
            Require(Lui_RunScript(App, R"(
                DiagnosticButton.Activated:Connect(function()
                    app:GetService('HttpServerService')
                    DiagnosticButton.Text='UNREACHABLE'
                end)
            )", "PRIVATE_CAPABILITY_CALLBACK_CANARY"), "capability callback setup failed");
            for (const auto& Node : nlohmann::json::parse(Tree)) if (Node.at("className") == "TextButton")
                Require(Lui_Activate(App, Node.at("id").get<int>()), "capability callback activation failed");
            Require(std::string(Lui_GetLastError(App)) == "[LUI:Capabilities] NetworkServerRequired", "callback lost capability reason");
            Require(Lui_RunScript(App, "assert(DiagnosticButton.Text~='UNREACHABLE'); DiagnosticButton.Text='Still usable'", "Recovery"),
                "capability callback disabled unrelated UI work");
            Require(Logs.find("NetworkServerRequired") != std::string::npos && Logs.find("PRIVATE_") == std::string::npos,
                "capability callback was lost or leaked private data");
            for(const auto* Canary:{"PRIVATE_NAME_CANARY","PRIVATE_TEXT_CANARY","PRIVATE_ACCESS_CANARY","PRIVATE_SOURCE_CANARY"})
                Require(Tree.find(Canary)==std::string::npos,"canary in tree");
            Require(Tree.find("[Redacted]")!=std::string::npos,"redaction markers absent");
            Require(Lui_GetInspectionTreeJson(App)==nullptr,"inspection implicitly granted");
            if(Mode==LUI_DIAGNOSTICS_DEVELOPMENT) {
                Require(Lui_BeginInspection(App),"owner inspection grant rejected");
                Require(std::string(Lui_GetInspectionTreeJson(App)).find("PRIVATE_TEXT_CANARY")!=std::string::npos,"memory inspection unavailable");
                Require(std::string(Lui_GetPreviewTreeJson(App)).find("PRIVATE_TEXT_CANARY")==std::string::npos,"inspection leaked into ordinary projection");
            } else Require(!Lui_BeginInspection(App),"restricted inspection granted");
            if(Mode==LUI_DIAGNOSTICS_DEVELOPMENT) Require(Tree.find("PUBLIC_TEXT")!=std::string::npos,"public content lost");
            else Require(Tree.find("PUBLIC_TEXT")==std::string::npos,"restricted public content leaked");
            Require(!Lui_RunScript(App,"error('PRIVATE_ERROR_CANARY')","PRIVATE_PATH_CANARY"),"error unexpectedly passed");
            Require(std::string(Lui_GetLastError(App)).find("PRIVATE_")==std::string::npos && Logs.find("PRIVATE_")==std::string::npos,"raw failure retained or delivered");
            Require(Lui_RunScript(App,"for I=1,600 do print(string.rep('a',8000)) end","Flood"),"flood changed execution");
            const auto Records=nlohmann::json::parse(Lui_GetDiagnosticsJson(App));
            Require(Records.at("records").size()<=256 && Records.at("dropped").get<size_t>()>0,"diagnostic bounds failed");
            for(const auto& Record:Records.at("records")) Require(Record.get<std::string>().size()<=4096,"record unbounded");
            Require(Lui_RunScript(App,"Public.Sensitive=true; Public.Text='LATE_CLASS_CANARY'","Late"),"classification failed");
            Require(std::string(Lui_GetPreviewTreeJson(App)).find("LATE_CLASS_CANARY")==std::string::npos,"late classification leaked");
            Lui_Destroy(App); Policy.Reserved=1; Require(Lui_CreateWithPolicy(&Policy)==nullptr,"invalid policy used legacy fallback");
        }
        std::puts("[LUI:PrivacyTest] Sticky projections, error sinks, policy and bounded retention passed"); return 0;
    } catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:PrivacyTest] %s\n",Error.what()); return 1;}
}
