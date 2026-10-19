#include "LuiProject.h"
#include "Package.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
static void Require(bool Value,const char* Label) {if(!Value) throw std::runtime_error(Label);}
static void LUI_CALL Log(void* Context,const char*,const char* Text) {static_cast<std::string*>(Context)->append(Text);}
static int Id(const nlohmann::json& Tree,const char* Name) {for(const auto& Node:Tree) if(Node.at("name")==Name) return Node.at("id").get<int>(); return 0;}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    try {
        const std::string Path=LUI_MODULAR_MANIFEST; LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=1;
        LuiProject* Project=nullptr; char Error[256]{};
        Require(Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Project,Error,sizeof(Error)),Error);
        for(int Generation=0;Generation<2;++Generation) {
            LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_DEVELOPMENT,0}; auto* Runtime=Lui_CreateWithPolicy(&Policy);
            std::string Logs; Lui_SetLogCallback(Runtime,&Logs,Log);
            LuiProjectRunOptionsV1 Run{sizeof(Run),1,0,0,0}; Require(Lui_RunProject(Runtime,Project,&Run),Lui_GetLastError(Runtime));
            auto Tree=nlohmann::json::parse(Lui_GetPreviewTreeJson(Runtime));
            Require(Id(Tree,"Home") && Id(Tree,"CounterStatus"),"home did not run");
            Require(Lui_Activate(Runtime,Id(Tree,"Increment")),"action did not activate");
            auto Updated=nlohmann::json::parse(Lui_GetPreviewTreeJson(Runtime));
            bool Count=false; for(const auto& Node:Updated) Count|=Node.at("text")=="Actions: 1"; Require(Count,"state action failed");
            Require(Lui_Activate(Runtime,Id(Tree,"ShowLibrary")),"screen navigation failed");
            Tree=nlohmann::json::parse(Lui_GetPreviewTreeJson(Runtime)); Require(Id(Tree,"Library") && !Id(Tree,"Home"),"screen teardown failed");
            Require(Lui_Activate(Runtime,Id(Tree,"Project_Workshop")),"project selection failed");
            Require(std::string(Lui_GetPreviewTreeJson(Runtime)).find("Selected: Workshop")!=std::string::npos,"selection not reflected");
            Require(Lui_Activate(Runtime,Id(Tree,"ShowHome")),"home navigation failed");
            Require(std::string(Lui_GetPreviewTreeJson(Runtime)).find("SYNTHETIC_")==std::string::npos && Logs.find("SYNTHETIC_")==std::string::npos,"application canary leaked");
            Lui_Destroy(Runtime);
        }
        LuiDiagnosticPolicyV1 Restricted{sizeof(Restricted),1,LUI_DIAGNOSTICS_RESTRICTED,0};
        auto* Protected=Lui_CreateWithPolicy(&Restricted); LuiProjectRunOptionsV1 Run{sizeof(Run),1,0,0,0};
        Require(Lui_RunProject(Protected,Project,&Run),Lui_GetLastError(Protected));
        const std::string ProtectedTree=Lui_GetPreviewTreeJson(Protected);
        for(const char* Content:{"SYNTHETIC_","LUI modular workspace","Actions: 0","ModularWorkspace"})
            Require(ProtectedTree.find(Content)==std::string::npos,"owner Restricted policy loosened by manifest");
        Lui_Destroy(Protected); Lui_ProjectRelease(Project);
        std::puts("[LUI:ApplicationTest] Modular screens, factories, state, redaction and fresh generations passed"); return 0;
    } catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:ApplicationTest] %s\n",Error.what()); return 1;}
}
