#include "Backend.h"
#include "LuiProject.h"
#include "Files.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

int main(int Count,char** Arguments) {
    const char* Phase="Arguments";
    LuiProject* Project=nullptr;
    struct ProjectGuard {LuiProject*& Project; ~ProjectGuard(){if(Project) Lui_ProjectRelease(Project);}} Guard{Project};
    try {
        if(Count!=3 && Count!=4) throw std::runtime_error("arguments");
        const std::string Mode=Arguments[1]; const auto Path=std::filesystem::absolute(std::filesystem::u8path(Arguments[2]));
        if(Mode!="--project" && Mode!="--script") throw std::runtime_error("arguments");
        const bool NativeAllowed=Count==4 && std::string(Arguments[3])=="--allow-native";
        if(Count==4 && !NativeAllowed) throw std::runtime_error("arguments");
        nlohmann::json Manifest; uint32_t PolicyMode=LUI_DIAGNOSTICS_DEVELOPMENT;
        if(Mode=="--project") {
            Phase="Preparation";
            const auto Utf8=Path.u8string(); LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=1;
            char Error[256]{}; if(!Lui_ProjectPrepare(&Options,Utf8.data(),Utf8.size(),&Project,Error,sizeof(Error))) throw std::runtime_error("preparation");
            LuiProjectSummaryV1 Summary{}; Summary.StructSize=sizeof(Summary); Summary.AbiVersion=1;
            Phase="Summary";
            if(!Lui_ProjectGetSummary(Project,&Summary) || Summary.DescriptorBytes>2*1024*1024) throw std::runtime_error("summary");
            Manifest=nlohmann::json::parse(Summary.Descriptor,Summary.Descriptor+Summary.DescriptorBytes).at("Manifest");
            if(Manifest.at("Diagnostics").at("Mode")=="Restricted") PolicyMode=LUI_DIAGNOSTICS_RESTRICTED;
        }
        Phase="Display";
        if(!Lui::Gtk::Backend::Initialize()) {std::fputs("[LUI:GTK] DisplayUnavailable\n",stderr); return 1;}
        Phase="Runtime";
        Lui::Gtk::Backend Host(PolicyMode); auto* Runtime=Host.GetRuntime();
        if(Project) {
            Phase="Capabilities";
            const std::map<std::string,uint64_t> Known{{"NativeExtensions",1},{"HostServices",2},{"Clipboard",4},{"Dialogs",8},{"network.client",16},{"network.server",32},{"network.raw",64},{"Storage",128},{"Notifications",256},{"Tray",512}};
            uint64_t Requested=0; for(const auto& Name:Manifest.at("Capabilities")) Requested|=Known.at(Name.get<std::string>());
            if((Requested&1) && !NativeAllowed) throw std::runtime_error("native trust");
            LuiCapabilityDeclarationV1 Caps{sizeof(Caps),1,Requested}; if(!Lui_DeclareCapabilities(Runtime,&Caps)) throw std::runtime_error("grants");
            if(Manifest.contains("NetworkPolicy")) {
                Phase="NetworkPolicy";
                const auto& Data=Manifest.at("NetworkPolicy");
                LuiNetworkPolicyV1 Policy{sizeof(Policy),1,(Data.at("ClientLoopbackOnly").get<bool>()?1u:0u)|(Data.at("ServerLoopbackOnly").get<bool>()?2u:0u),
                    Data.at("ClientPortMin"),Data.at("ClientPortMax"),Data.at("ServerPortMin"),Data.at("ServerPortMax")};
                if(!Lui_SetNetworkPolicy(Runtime,&Policy)) throw std::runtime_error("network policy");
            }
            Phase="Assets";
            Lui::Project::Internal::Anchor Root(Path.parent_path());
            for(const auto& Name:Manifest.at("Assets")) {const auto Relative=Name.get<std::string>(); Host.SetAsset(Relative,Root.Read(Relative,16*1024*1024,{}).Bytes);}
            if(Requested&1) {LuiProjectNativeOptionsV1 Options{sizeof(Options),1,LUI_PROJECT_NATIVE_TRUSTED,0}; if(!Lui_LoadProjectNative(Runtime,Project,&Options)) throw std::runtime_error("native binding");}
            Phase="Execution";
            LuiProjectRunOptionsV1 Options{sizeof(Options),1,0,0,0}; if(!Lui_RunProject(Runtime,Project,&Options)) throw std::runtime_error("execution");
            Lui_ProjectRelease(Project); Project=nullptr;
        } else {
            Lui::Project::Internal::Anchor Root(Path.parent_path()); auto Source=Root.Read(Path.filename().u8string(),8*1024*1024,{}).Bytes;
            if(Source.find('\0')!=std::string::npos || !Lui_RunScript(Runtime,Source.c_str(),"Entry")) throw std::runtime_error("script");
        }
        while(Host.HasWindows()) {Host.Pump(); g_usleep(16000);}
        Host.Stop(); return 0;
    } catch(...) {std::fprintf(stderr,"[LUI:GTK] StartupFailed %s\n",Phase); return 1;}
}
