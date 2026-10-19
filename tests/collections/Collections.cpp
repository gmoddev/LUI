#include "LuiProject.h"
#include "Project.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <set>

namespace Fs = std::filesystem;
using Json = nlohmann::json;
static void Require(bool Value,const char* Message) {if(!Value) throw std::runtime_error(Message);}
static std::string Read(const Fs::path& Path) {
    std::ifstream File(Path,std::ios::binary); Require(bool(File),"source fixture missing");
    return {std::istreambuf_iterator<char>(File),std::istreambuf_iterator<char>()};
}
static void Write(const Fs::path& Path,const std::string& Bytes) {
    Fs::create_directories(Path.parent_path()); std::ofstream File(Path,std::ios::binary); File<<Bytes; Require(bool(File),"fixture write failed");
}
struct Recorder {std::set<int> Alive; size_t Peak=0; int FirstButton=0;};
static int LUI_CALL Create(void* Context,int Id,const char* Class) {
    auto& Value=*static_cast<Recorder*>(Context); Value.Alive.insert(Id); Value.Peak=std::max(Value.Peak,Value.Alive.size());
    if(!Value.FirstButton && std::string(Class)=="TextButton") Value.FirstButton=Id; return 1;
}
static int LUI_CALL Property(void*,int,const char*,const char*) {return 1;}
static int LUI_CALL Parent(void*,int,int) {return 1;}
static int LUI_CALL Arrange(void*,int,double,double,double,double,int) {return 1;}
static int LUI_CALL Present(void*,int,int,double,double,double,double,int,int) {return 1;}
static int LUI_CALL Destroy(void* Context,int Id) {static_cast<Recorder*>(Context)->Alive.erase(Id); return 1;}
int main() {
    const auto Root=Fs::temp_directory_path()/("lui-collections-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Require(Root.is_absolute() && Fs::equivalent(Root.parent_path(),Fs::temp_directory_path()),"fixture path escaped temporary root");
    LuiProject* Project=nullptr; LuiRuntime* Runtime=nullptr;
    Recorder Recorded;
    try {
        const Json Files={"Scope.luau","State.luau","VirtualList.luau"}; Json Pins=Json::array();
        for(const auto& File:Files) {
            const auto Name=File.get<std::string>(); const auto Bytes=Read(Fs::path(LUI_UI_LIBRARY_DIR)/Name);
            Write(Root/"Vendor/LuiUI"/Name,Bytes); Pins.push_back({{"Path",Name},{"Sha256",Lui::Project::Hash(Bytes)}});
        }
        Write(Root/"Src/Main.luau",Read(LUI_COLLECTION_CASE));
        Write(Root/"Modules.lock.json",Json{{"SchemaVersion",1},{"Packages",Json::array({{{"Alias","LuiUI"},
            {"Provenance",{{"Kind","LocalVendor"},{"Id","LuiUI-0.1.0"}}},{"Files",Pins}}})}}.dump());
        Json Manifest={{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",Json::array()},{"Extensions",Json::array()},
            {"Assets",Json::array()},{"ExtensionDeclarations",Json::array()},{"Diagnostics",{{"Mode","Development"}}},
            {"Sandbox",{{"MaxMemoryBytes",128*1024*1024},{"MaxInterrupts",1000000}}},
            {"Modules",{{"Root","Src"},{"Files",{"Main.luau"}},{"Requires",{"LuiUI"}},{"Lock","Modules.lock.json"},
                {"Dependencies",Json::array({{{"Alias","LuiUI"},{"Root","Vendor/LuiUI"},{"Files",Files},{"Requires",Json::array()}}})}}}};
        Write(Root/"lui.json",Manifest.dump());
        const auto Utf8=(Root/"lui.json").u8string(); const std::string Path(Utf8.begin(),Utf8.end()); char Error[512]{};
        LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=1;
        Require(Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Project,Error,sizeof(Error)),Error);
        LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_DEVELOPMENT,0}; Runtime=Lui_CreateWithPolicy(&Policy);
        Lui_SetBackend(Runtime,{&Recorded,Create,Property,Parent,Arrange,Destroy});
        LuiPresentationCallbacksV1 Presentation{sizeof(Presentation),1,&Recorded,Present};
        Require(Lui_SetPresentationCallbacks(Runtime,&Presentation),"presentation registration failed");
        LuiProjectRunOptionsV1 Run{sizeof(Run),LUI_PROJECT_RUN_ABI_VERSION,LUI_PROJECT_RUN_DEVELOPMENT_DIAGNOSTICS,0,0};
        Require(Lui_RunProject(Runtime,Project,&Run),Lui_GetLastError(Runtime));
        Lui_Pump(Runtime);
        const auto Tree=Json::parse(Lui_GetPreviewTreeJson(Runtime));
        Require(Tree.size()==2 && Tree[0]["title"]=="Collection regression complete",Lui_GetLastError(Runtime));
        Require(Recorded.Peak<=150 && Recorded.Alive.size()==2,"collection native ownership exceeded its bound");
        Require(Recorded.FirstButton && !Lui_Activate(Runtime,Recorded.FirstButton),"retired row activation reached a new binding");
        Lui_Destroy(Runtime); Runtime=nullptr; Lui_ProjectRelease(Project); Project=nullptr; Fs::remove_all(Root);
        std::puts("[LUI:CollectionTest] 10000 items, bounded realization, stable anchoring, state/scope retirement and stale activation passed"); return 0;
    } catch(const std::exception& Error) {
        if(Runtime) Lui_Destroy(Runtime); if(Project) Lui_ProjectRelease(Project); Fs::remove_all(Root);
        std::fprintf(stderr,"[LUI:CollectionTest] %s\n",Error.what()); return 1;
    }
}
