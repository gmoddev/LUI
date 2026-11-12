#include "Backend.h"
#include "LuiProject.h"
#include "Files.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <cstdlib>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include "../../runtime/diagnostics/NativeHost.h"

static void LUI_CALL NativeLog(void* Context, const char* Level, const char* Message) {
    if (std::strcmp(Level, "Error") == 0) static_cast<Lui::Diagnostics::NativeHost*>(Context)->Report(Message);
    else std::fprintf(stdout, "[LUI:%s] %s\n", Level, Message);
}

int main(int Count,char** Arguments) {
    // This dedicated host may lose its collector. Keep writes bounded and avoid
    // blocking the scheduler or terminating the app on a closed diagnostic pipe.
    std::signal(SIGPIPE, SIG_IGN);
    const int ErrorFlags = fcntl(STDERR_FILENO, F_GETFL);
    if (ErrorFlags >= 0) fcntl(STDERR_FILENO, F_SETFL, ErrorFlags | O_NONBLOCK);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    const char* Phase="Arguments";
    Lui::Diagnostics::NativeHost Diagnostics;
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
            char Error[256]{}; if(!Lui_ProjectPrepare(&Options,Utf8.data(),Utf8.size(),&Project,Error,sizeof(Error))) {
                Diagnostics.Report(Error); throw std::runtime_error("preparation");
            }
            LuiProjectSummaryV1 Summary{}; Summary.StructSize=sizeof(Summary); Summary.AbiVersion=1;
            Phase="Summary";
            if(!Lui_ProjectGetSummary(Project,&Summary) || Summary.DescriptorBytes>2*1024*1024) throw std::runtime_error("summary");
            Manifest=nlohmann::json::parse(Summary.Descriptor,Summary.Descriptor+Summary.DescriptorBytes).at("Manifest");
            if(Manifest.at("Diagnostics").at("Mode")=="Restricted") PolicyMode=LUI_DIAGNOSTICS_RESTRICTED;
        }
        Phase="Display";
        if(!Lui::Gtk::Backend::Initialize()) {Diagnostics.Report("[LUI:Diagnostics] StartupFailed"); std::fputs("[LUI:GTK] DisplayUnavailable\n",stderr); return 1;}
        Phase="Runtime";
        Lui::Gtk::Backend Host(PolicyMode); auto* Runtime=Host.GetRuntime();
        Lui_SetLogCallback(Runtime, &Diagnostics, NativeLog);
        if(Project) {
            Phase="Capabilities";
            const std::map<std::string,uint64_t> Known{{"NativeExtensions",1},{"HostServices",2},{"Clipboard",4},{"Dialogs",8},{"network.client",16},{"network.server",32},{"network.raw",64},{"Storage",128},{"Notifications",256},{"Tray",512}};
            uint64_t Requested=0; for(const auto& Name:Manifest.at("Capabilities")) Requested|=Known.at(Name.get<std::string>());
            if((Requested&1) && !NativeAllowed) {Diagnostics.Report("[LUI:Project] CapabilityDenied"); throw std::runtime_error("native trust");}
            LuiCapabilityDeclarationV1 Caps{sizeof(Caps),1,Requested}; if(!Lui_DeclareCapabilities(Runtime,&Caps)) throw std::runtime_error("grants");
            if(Manifest.contains("Storage")) {
                Phase="StorageUnavailable";
                const auto* Home=std::getenv("HOME"); const auto Root=Path.parent_path().u8string();
                if(!Home || Lui_InstallProjectStorageV1(Runtime,Project,Home,Root.c_str())) {Diagnostics.Report("[LUI:Project] StorageUnavailable"); throw std::runtime_error("storage provisioning");}
            }
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
        Diagnostics.Running = true;
        while(Host.HasWindows()) {Host.Pump(); g_usleep(16000);}
        Host.Stop(); return 0;
    } catch(...) {if (!Diagnostics.StartupReported) Diagnostics.Report("[LUI:Diagnostics] StartupFailed"); std::fprintf(stderr,"[LUI:GTK] StartupFailed %s\n",Phase); return 1;}
}
