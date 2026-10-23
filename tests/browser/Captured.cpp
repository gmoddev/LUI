#include "../../runtime/project/Project.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include "../../native/abi/LuiProject.h"
#include "../../third_party/picosha2/picosha2.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool Value) { if (!Value) throw std::runtime_error("[LUI:BrowserTest] Captured parity failed"); }
std::string Read(const std::filesystem::path& Path) {
    std::ifstream Input(Path,std::ios::binary);
    Check(bool(Input)); return {std::istreambuf_iterator<char>(Input),{}};
}
std::vector<std::string> Text;
int PointerTarget=0;
int RowTarget=0, InsertTarget=0;
int Property(void*,int Id,const char* Name,const char* Value) {
    if (std::string(Name)=="Text") Text.emplace_back(Value);
    if (std::string(Name)=="Text" && std::string(Value)=="Pointer target") PointerTarget=Id;
    if (std::string(Name)=="Text" && std::string(Value)=="Row 1") RowTarget=Id;
    if (std::string(Name)=="Text" && std::string(Value)=="Insert row") InsertTarget=Id;
    return 1;
}
int Present(void*,int,int,double,double,double,double,int,int) {return 1;}
struct InputDirectory {
    std::filesystem::path Path;
    InputDirectory() {
        Path=std::filesystem::current_path()/("LuiBrowserAcceptance-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Check(std::filesystem::create_directory(Path));
    }
    ~InputDirectory() { std::error_code Error; std::filesystem::remove_all(Path,Error); }
};
}
int main() {
    try {
        const std::filesystem::path Root=LUI_BROWSER_FIXTURE;
        std::map<std::string,std::string> Files;
        for (const auto* Name:{"lui.json","Src/Main.luau","Src/Counter.luau"}) Files.emplace(Name,Read(Root/Name));
        std::string Error;
        auto Captured=Lui::Project::PrepareCaptured(Files,"lui.json",{},Error);
        auto Native=Lui::Project::Prepare(Root/"lui.json",{},Error);
        Check(Captured && Native && Captured->GetDescriptor()==Native->GetDescriptor());
        auto Compiled=Lui::Project::Compile(Captured,{},Error);
        auto NativeCompiled=Lui::Project::Compile(Native,{},Error);
        Check(Compiled && NativeCompiled && Compiled->GetDigest()==NativeCompiled->GetDigest());
        for (size_t Size:{0,1,55,56,63,64,65,1024,1048576}) {
            std::string Bytes(Size,'\0');
            for (size_t Index=0;Index<Size;++Index) Bytes[Index]=static_cast<char>((Index*73+29)&255);
            Check(Lui::Project::Hash(Bytes)==picosha2::hash256_hex_string(Bytes.begin(),Bytes.end()));
        }
        auto Extra=Files; Extra["Unused.luau"]="return true";
        Check(!Lui::Project::PrepareCaptured(Extra,"lui.json",{},Error) && Error.find("UnexpectedFile")!=std::string::npos);
        Extra=Files; Extra.erase("Src/Counter.luau");
        Check(!Lui::Project::PrepareCaptured(Extra,"lui.json",{},Error));
        Extra=Files; Extra["src/Other.luau"]="return true";
        Check(!Lui::Project::PrepareCaptured(Extra,"lui.json",{},Error));
        Lui::Project::Options Canceled; Canceled.Canceled=[] {return true;};
        Check(!Lui::Project::PrepareCaptured(Files,"lui.json",Canceled,Error));
        LuiProjectOptionsV1 Preparation{}; Preparation.StructSize=sizeof(Preparation); Preparation.AbiVersion=1;
        LuiProject* Project=nullptr;
        const auto Path=(Root/"lui.json").u8string();
        Check(Lui_ProjectPrepare(&Preparation,Path.data(),Path.size(),&Project,nullptr,0));
        LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_RESTRICTED,0};
        auto* Runtime=Lui_CreateWithPolicy(&Policy); Check(Runtime);
        LuiPresentationCallbacksV1 Presentation{sizeof(Presentation),1,nullptr,Present};
        Check(Lui_SetPresentationCallbacks(Runtime,&Presentation));
        Lui_SetBackend(Runtime,{nullptr,nullptr,Property,nullptr,nullptr,nullptr});
        LuiProjectRunOptionsV1 Run{sizeof(Run),1,0,0,0};
        Check(Lui_RunProject(Runtime,Project,&Run));
        Check(Lui_Activate(Runtime,2));
        Check(Text.size()>=2 && Text[Text.size()-2]=="Count 0" && Text.back()=="Count 1");
        Lui_Destroy(Runtime); Lui_ProjectRelease(Project);
        auto Manifest=nlohmann::json::parse(Files.at("lui.json"));
        Manifest["Modules"]["Files"].push_back("Scope.luau");
        Manifest["Modules"]["Files"].push_back("VirtualList.luau");
        Files["lui.json"]=Manifest.dump();
        Files["Src/Main.luau"]=Read(Root/"../Acceptance.luau");
        for(const auto* Name:{"Scope","VirtualList"})
            Files[std::string("Src/")+Name+".luau"]=Read(Root/(std::string("../../../examples/conversation/Vendor/LuiUI/")+Name+".luau"));
        Captured=Lui::Project::PrepareCaptured(Files,"lui.json",{},Error); Check(bool(Captured));
        Compiled=Lui::Project::Compile(Captured,{},Error); Check(bool(Compiled));
        InputDirectory Inputs;
        for(const auto& Entry:Files) {
            const auto Target=Inputs.Path/Entry.first; std::filesystem::create_directories(Target.parent_path());
            std::ofstream Output(Target,std::ios::binary); Output.write(Entry.second.data(),static_cast<std::streamsize>(Entry.second.size())); Check(bool(Output));
        }
        const auto AcceptancePath=(Inputs.Path/"lui.json").u8string();
        Native=Lui::Project::Prepare(Inputs.Path/"lui.json",{},Error); Check(bool(Native));
        NativeCompiled=Lui::Project::Compile(Native,{},Error); Check(NativeCompiled && NativeCompiled->GetDigest()==Compiled->GetDigest());
        Check(Lui_ProjectPrepare(&Preparation,AcceptancePath.data(),AcceptancePath.size(),&Project,nullptr,0));
        Runtime=Lui_CreateWithPolicy(&Policy); Check(Runtime);
        Check(Lui_SetPresentationCallbacks(Runtime,&Presentation));
        Lui_SetBackend(Runtime,{nullptr,nullptr,Property,nullptr,nullptr,nullptr});
        Text.clear(); Check(Lui_RunProject(Runtime,Project,&Run)); Lui_Pump(Runtime);
        Check(std::find(Text.begin(),Text.end(),"Shared accepted")!=Text.end() && PointerTarget>0);
        Check(Lui_PointerInput(Runtime,PointerTarget,0,0,1,12,12));
        Check(std::find(Text.begin(),Text.end(),"Pointer 12/12")!=Text.end());
        Check(Lui_PointerInput(Runtime,PointerTarget,2,0,1,12,12));
        Check(std::find(Text.begin(),Text.end(),"Ended 1/1/0")!=Text.end());
        Check(RowTarget>0 && InsertTarget>0 && Lui_FocusChanged(Runtime,RowTarget,1));
        Check(Lui_Activate(Runtime,InsertTarget));
        Check(std::find(Text.begin(),Text.end(),"Inserted")!=Text.end());
        Lui_Destroy(Runtime); Lui_ProjectRelease(Project);
        const auto ModuleCases=nlohmann::json::parse(Read(Root/"../Modules.json"));
        Check(ModuleCases.at("Version")==1);
        for(int Generation=0;Generation<2;++Generation) for(const auto& Case:ModuleCases.at("Cases")) {
            std::cout<<"[LUI:BrowserTest] Native module "<<Case.at("Name").get<std::string>()<<" generation "<<Generation<<"\n";
            std::map<std::string,std::string> CaseFiles;
            auto CaseManifest=nlohmann::json::parse(Read(Root/"lui.json"));
            CaseManifest["Modules"]["Files"]=nlohmann::json::array();
            for(auto Entry=Case.at("Files").begin();Entry!=Case.at("Files").end();++Entry) {
                CaseManifest["Modules"]["Files"].push_back(Entry.key());
                auto Source=Entry.value().get<std::string>();
                if(Entry.key()=="Main.luau" && !Case.contains("CompileError"))
                    Source=ModuleCases.at("Prelude").get<std::string>()+Source+ModuleCases.at("Epilogue").get<std::string>();
                CaseFiles["Src/"+Entry.key()]=std::move(Source);
            }
            CaseFiles["lui.json"]=CaseManifest.dump();
            InputDirectory CaseInputs;
            for(const auto& Entry:CaseFiles) {
                const auto Target=CaseInputs.Path/Entry.first; std::filesystem::create_directories(Target.parent_path());
                std::ofstream Output(Target,std::ios::binary); Output.write(Entry.second.data(),static_cast<std::streamsize>(Entry.second.size())); Check(bool(Output));
            }
            std::string CapturedError,NativeError;
            const auto CaseCaptured=Lui::Project::PrepareCaptured(CaseFiles,"lui.json",{},CapturedError);
            const auto CaseNative=Lui::Project::Prepare(CaseInputs.Path/"lui.json",{},NativeError);
            Check(CaseCaptured && CaseNative);
            const auto CaseCompiled=Lui::Project::Compile(CaseCaptured,{},CapturedError);
            const auto CaseNativeCompiled=Lui::Project::Compile(CaseNative,{},NativeError);
            if(Case.contains("CompileError")) {
                Check(!CaseCompiled && !CaseNativeCompiled && CapturedError==Case.at("CompileError").get<std::string>() && NativeError==CapturedError); continue;
            }
            Check(CaseCompiled && CaseNativeCompiled && CaseCompiled->GetDigest()==CaseNativeCompiled->GetDigest());
            const auto CasePath=(CaseInputs.Path/"lui.json").u8string();
            Check(Lui_ProjectPrepare(&Preparation,CasePath.data(),CasePath.size(),&Project,nullptr,0));
            Runtime=Lui_CreateWithPolicy(&Policy); Check(Runtime);
            Check(Lui_SetPresentationCallbacks(Runtime,&Presentation));
            Lui_SetBackend(Runtime,{nullptr,nullptr,Property,nullptr,nullptr,nullptr}); Text.clear();
            Check(Lui_RunProject(Runtime,Project,&Run));
            Check(std::find(Text.begin(),Text.end(),"Module accepted")==Text.end());
            const auto Turns=Case.value("PumpTurns",1);
            for(int Turn=0;Turn<Turns;++Turn) {
                Check(Lui_Pump(Runtime)>0);
                if(Turn+1<Turns) Check(std::find(Text.begin(),Text.end(),"Module accepted")==Text.end());
            }
            Check(std::find(Text.begin(),Text.end(),"Module accepted")!=Text.end());
            Lui_Destroy(Runtime); Lui_ProjectRelease(Project);
        }
        std::cout<<"[LUI:BrowserTest] Captured/native compilation, SHA-256 and activation parity passed\n";
        return 0;
    } catch (...) {std::cerr<<"[LUI:BrowserTest] Captured parity failed\n";return 1;}
}
