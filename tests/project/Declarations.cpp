#include "Package.h"
#include "ProjectCheck.h"
#include "LuiProject.h"
#include "Files.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace Fs=std::filesystem;
using Json=nlohmann::json;
using namespace Lui::Project;
static void Require(bool Value,const char* Label) {if(!Value) throw std::runtime_error(Label);}
static void Write(const Fs::path& Path,const std::string& Bytes) {
    Fs::create_directories(Path.parent_path()); std::ofstream File(Path,std::ios::binary); File.write(Bytes.data(),Bytes.size()); Require(bool(File),"fixture write failed");
}
static Json Declaration() {
    return {{"SchemaVersion",1},{"AbiVersion",1},{"Provider","Host"},{"Services",Json::array({{{"Name","Telemetry"},
        {"Methods",Json::array({{{"Name","Count"},{"Arguments",{"string"}},{"Returns","number"}},
            {{"Name","Clear"},{"Arguments",Json::array()},{"Returns","nil"}}})},
        {"Signals",Json::array({{{"Name","Changed"},{"Arguments",{"number","boolean"}}}})}}})}};
}
static Json Manifest() {
    return {{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",{"HostServices"}},{"Extensions",Json::array()},
        {"Assets",Json::array()},{"ExtensionDeclarations",{"Types/Host.json"}},{"Diagnostics",{{"Mode","Development"}}},
        {"Modules",{{"Root","Src"},{"Files",{"Main.luau","Factory.luau"}},{"Requires",Json::array()},{"Dependencies",Json::array()}}}};
}
static void Fixture(const Fs::path& Root) {
    Write(Root/"Types/Host.json",Declaration().dump(2)); Write(Root/"lui.json",Manifest().dump());
    Write(Root/"Src/Main.luau","local Service=app:GetService('Telemetry'); local Count:number=Service:Count('demo'); Service:Clear(); local W:Window=app:GetService('WindowService'):GetWindows()[1]; local C=Service.Changed:Connect(function(N:number,Ready:boolean) end); C:Disconnect()");
    Write(Root/"Src/Factory.luau","return function() return app:GetService('Telemetry'):Count('module') end");
}
static void Bad(const Fs::path& Root,Json Value,const char* Code="InvalidDeclaration") {
    Write(Root/"Types/Host.json",Value.dump()); std::string Error;
    Require(!Prepare(Root/"lui.json",{},Error) && Error==std::string("[LUI:Project] ")+Code,Code);
}
static int Calls=0;
static int LUI_CALL Count(void*,const LuiValueV1* Args,uint32_t Length,LuiValueV1* Result) {
    ++Calls; if(Length!=1 || Args[0].Type!=LUI_VALUE_STRING) return 0; Result->Type=LUI_VALUE_NUMBER; Result->Number=7; return 1;
}
static int LUI_CALL Clear(void*,const LuiValueV1*,uint32_t Length,LuiValueV1* Result) {++Calls; Result->Type=LUI_VALUE_NIL; return Length==0;}
using RuntimePtr=std::unique_ptr<LuiRuntime,decltype(&Lui_Destroy)>;
static RuntimePtr Runtime(const char* Signature="(Self: Telemetry, Name: string) -> number",bool Signal=true,bool Extra=false) {
    RuntimePtr App(Lui_Create(),Lui_Destroy); Require(bool(App),"runtime creation failed");
    LuiCapabilityDeclarationV1 Caps{sizeof(Caps),LUI_EXTENSION_ABI_VERSION,LUI_CAPABILITY_HOST_SERVICES};
    Require(Lui_DeclareCapabilities(App.get(),&Caps)==1,"capabilities failed");
    Require(Lui_RegisterHostMethod(App.get(),"Telemetry","Count",Signature,Count,nullptr)==1,"count registration failed");
    Require(Lui_RegisterHostMethod(App.get(),"Telemetry","Clear","(Self: Telemetry) -> ()",Clear,nullptr)==1,"clear registration failed");
    if(Signal) Require(Lui_RegisterHostSignal(App.get(),"Telemetry","Changed","Signal<number, boolean>")==1,"signal registration failed");
    if(Extra) Require(Lui_RegisterHostMethod(App.get(),"Telemetry","Extra","() -> nil",Clear,nullptr)==1,"extra registration failed");
    return App;
}
static void Cases(const Fs::path& Root,const std::string& Definitions) {
    const auto Input=Root/"Input"; Fixture(Input); std::string Error;
    auto Sources=Prepare(Input/"lui.json",{},Error); Require(bool(Sources),Error.c_str());
    const auto OriginalDigest=Sources->GetDigest(); const auto Registration=Sources->GetDeclarations().Digest;
    Require(Registration=="bfe2330e6f2a382c8c62c748cf8839c6c411569a211456ac6193d74282f735c0","portable registration fingerprint changed");
    Require(Sources->GetDeclarations().Files.size()==1 && Sources->GetDeclarations().Services.size()==1,"sidecar capture absent");
    Lui::TypeCheck::Options Settings; Settings.DevelopmentDiagnostics=true;
    auto Checked=Lui::TypeCheck::Check(*Sources,Definitions,Settings,Error); Require(bool(Checked),Error.c_str());
    for(const auto& Issue:Checked->Diagnostics) std::fprintf(stderr,"[LUI:DeclarationsTest] %s\n",Issue.Message.c_str());
    Require(Checked->Diagnostics.empty(),"generated services/signals/builtins did not infer");
    Require(Checked->DefinitionsDigest!=Hash(Definitions),"merged definition fingerprint absent");
    Write(Input/"Src/Main.luau","local S=app:GetService('Telemetry'); local N:string=S:Count('bad'); S:Count(9)");
    auto Wrong=Prepare(Input/"lui.json",{},Error); Checked=Lui::TypeCheck::Check(*Wrong,Definitions,Settings,Error);
    Require(Checked && !Checked->Diagnostics.empty(),"wrong primitive method input/result type accepted");
    Write(Input/"Src/Main.luau","local S=app:GetService('Telemetry'); S.Changed:Connect(function(N:string) end)");
    Wrong=Prepare(Input/"lui.json",{},Error); Checked=Lui::TypeCheck::Check(*Wrong,Definitions,Settings,Error);
    Require(Checked && !Checked->Diagnostics.empty(),"wrong signal callback type accepted");
    Fixture(Input);
    auto Reordered=Declaration(); std::reverse(Reordered["Services"][0]["Methods"].begin(),Reordered["Services"][0]["Methods"].end());
    Write(Input/"Types/Host.json",Reordered.dump()); Wrong=Prepare(Input/"lui.json",{},Error);
    Require(Wrong && Wrong->GetDeclarations().Digest==Registration && Wrong->GetDigest()!=OriginalDigest,
        "registration order/captured byte fingerprints conflated");
    Require(Lui::TypeCheck::Check(*Sources,Definitions,Settings,Error)->Diagnostics.empty(),"checker reread sidecar");
    auto BadValue=Declaration(); BadValue["Services"][0]["Name"]="WindowService"; Bad(Input,BadValue);
    BadValue=Declaration(); BadValue["Services"][0]["Methods"][0]["Arguments"]={"any"}; Bad(Input,BadValue);
    BadValue=Declaration(); BadValue["Services"][0]["Methods"][0]["Returns"]="number?"; Bad(Input,BadValue);
    BadValue=Declaration(); BadValue["Services"][0]["Methods"][0]["Name"]="end"; Bad(Input,BadValue);
    BadValue=Declaration(); BadValue["Services"][0]["Signals"][0]["Name"]="Count"; Bad(Input,BadValue);
    BadValue=Declaration(); BadValue["Provider"]="Native"; Bad(Input,BadValue,"UnsupportedFeature");
    BadValue=Declaration(); BadValue["AbiVersion"]=2; Bad(Input,BadValue);
    BadValue=Declaration(); BadValue["SchemaVersion"]=1.0; Bad(Input,BadValue);
    BadValue=Declaration(); BadValue["Services"].push_back(BadValue["Services"][0]); Bad(Input,BadValue);
    BadValue=Declaration(); BadValue["Services"][0]["Methods"][0]["Arguments"]=Json::array();
    for(int Index=0;Index<33;++Index) BadValue["Services"][0]["Methods"][0]["Arguments"].push_back("number");
    Bad(Input,BadValue,"LimitExceeded");
    BadValue=Declaration(); BadValue["Services"]=Json::array();
    for(int Index=0;Index<65;++Index) {
        auto Service=Declaration()["Services"][0]; Service["Name"]="Service"+std::to_string(Index); BadValue["Services"].push_back(Service);
    }
    Bad(Input,BadValue,"LimitExceeded");
    BadValue=Declaration(); BadValue["Unexpected"]="PRIVATE_DECL_CANARY"; Bad(Input,BadValue);
    Fixture(Input); const auto Original=Declaration().dump();
    Write(Input/"Types/Host.json","{\"SchemaVersion\":1,\"SchemaVersion\":1}");
    Require(!Prepare(Input/"lui.json",{},Error) && Error=="[LUI:Project] InvalidJson","duplicate sidecar keys accepted");
    Write(Input/"Types/Host.json",std::string(256*1024+1,' '));
    Require(!Prepare(Input/"lui.json",{},Error) && Error=="[LUI:Project] LimitExceeded","sidecar byte bound ignored");
    Fixture(Input);
    // Multiple files share one captured contract with sorted file paths; limits
    // and path/ownership checks apply before compiler or VM entry.
    auto Two=Manifest(); Two["ExtensionDeclarations"]={"Types/Host.json","Types/Other.json"};
    auto Other=Declaration(); Other["Services"][0]["Name"]="Launcher";
    Write(Input/"Types/Other.json",Other.dump()); Write(Input/"lui.json",Two.dump());
    auto Multi=Prepare(Input/"lui.json",{},Error);
    Require(Multi && Multi->GetDeclarations().Files.size()==2 && Multi->GetDeclarations().Services.size()==2,"multiple sidecars did not merge");
    auto Many=Manifest(); Many["ExtensionDeclarations"]=Json::array();
    for(int Index=0;Index<17;++Index) Many["ExtensionDeclarations"].push_back("Types/File"+std::to_string(Index)+".json");
    Write(Input/"lui.json",Many.dump());
    Require(!Prepare(Input/"lui.json",{},Error) && Error=="[LUI:Project] LimitExceeded","sidecar file count bound ignored");
    auto Aggregate=Manifest(); Aggregate["ExtensionDeclarations"]=Json::array();
    for(int Index=0;Index<5;++Index) {
        const auto Path="Types/Aggregate"+std::to_string(Index)+".json";
        auto Value=Declaration(); Value["Services"][0]["Name"]="Aggregate"+std::to_string(Index);
        auto Bytes=Value.dump(); Bytes.resize(256*1024,' '); Write(Input/Path,Bytes); Aggregate["ExtensionDeclarations"].push_back(Path);
    }
    Write(Input/"lui.json",Aggregate.dump());
    Require(!Prepare(Input/"lui.json",{},Error) && Error=="[LUI:Project] LimitExceeded","aggregate declaration bytes ignored");
    auto NoCaps=Manifest(); NoCaps["Capabilities"]=Json::array(); Write(Input/"lui.json",NoCaps.dump());
    Require(!Prepare(Input/"lui.json",{},Error) && Error=="[LUI:Project] InvalidManifest","declarations granted host capability");
    Fixture(Input);
    Settings.Canceled=[] {return true;};
    Require(!Lui::TypeCheck::Check(*Sources,Definitions,Settings,Error) && Error=="[LUI:TypeCheck] Canceled","declared analysis cancellation ignored");
    Settings.Canceled={};
    auto Restricted=Manifest(); Restricted["Diagnostics"]["Mode"]="Restricted";
    Write(Input/"lui.json",Restricted.dump()); auto Private=Prepare(Input/"lui.json",{},Error);
    Require(Private && !Lui::TypeCheck::Check(*Private,Definitions,Settings,Error) && Error=="[LUI:TypeCheck] PolicyNotSupported",
        "declarations bypassed diagnostic policy");
    Fixture(Input);
    auto Package=CapturePackage(Input/"lui.json",{},Error); Require(bool(Package),Error.c_str());
    Write(Input/"Types/Host.json","MUTATED_PRIVATE_DECL_CANARY");
    Require(StagePackage(*Package,Root/"Output",{},Error),Error.c_str()); auto Verified=VerifyPackage(Root/"Output",{},Error);
    Require(Verified && Verified->GetPrepared().GetSources().GetDeclarations().Digest==Package->GetPrepared().GetSources().GetDeclarations().Digest,
        "staging lost captured sidecar/registration");
    Write(Root/"Output/Types/Host.json",Declaration().dump());
    Require(!VerifyPackage(Root/"Output",{},Error) && Error=="[LUI:Project] PackageMismatch","sidecar byte edit passed package verification");
    Fixture(Input);
    // Runtime equality is independent of parameter names/whitespace and checked
    // before entry. The existing native ABI remains v1; callbacks are trusted.
    Write(Input/"Src/Main.luau","assert(app:GetService('Telemetry'):Count('runtime')==7); app:GetService('Telemetry'):Clear()");
    const auto Path=(Input/"lui.json").u8string(); LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=LUI_PROJECT_ABI_VERSION;
    LuiProject* Handle=nullptr; char Message[128]{};
    Require(Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message))==1,Message);
    std::unique_ptr<LuiProject,decltype(&Lui_ProjectRelease)> Owned(Handle,Lui_ProjectRelease);
    LuiProjectRunOptionsV1 Run{sizeof(Run),LUI_PROJECT_RUN_ABI_VERSION,LUI_PROJECT_RUN_DEVELOPMENT_DIAGNOSTICS,0,0};
    for(int Case=0;Case<4;++Case) {
        auto App=Runtime(Case==1?"(Arg: number) -> number":"( Arg: string )  -> number",Case!=2,Case==3); Calls=0;
        const bool Ok=Lui_RunProject(App.get(),Handle,&Run)!=0;
        Require(Ok==(Case==0),"registration mismatch admission incorrect");
        if(!Ok) Require(Calls==0 && std::string(Lui_GetLastError(App.get()))=="[LUI:Modules] RegistrationMismatch" &&
            std::string(Lui_GetPreviewTreeJson(App.get()))=="[]","mismatch ran entry/callbacks or leaked error");
        else Require(Calls==2,"valid host callbacks not invoked");
    }
    auto Invalid=Runtime("(Arg: any) -> number"); Calls=0;
    Require(!Lui_RunProject(Invalid.get(),Handle,&Run) && Calls==0,"unsupported observed metadata accepted");
    auto Missing=RuntimePtr(Lui_Create(),Lui_Destroy); LuiCapabilityDeclarationV1 Caps{sizeof(Caps),LUI_EXTENSION_ABI_VERSION,LUI_CAPABILITY_HOST_SERVICES};
    Require(Missing && Lui_DeclareCapabilities(Missing.get(),&Caps) && !Lui_RunProject(Missing.get(),Handle,&Run) &&
        std::string(Lui_GetLastError(Missing.get()))=="[LUI:Modules] RegistrationMismatch","missing registrations admitted entry");
    Caps.GrantedCapabilities=0; // A different fresh owner has no grants; metadata cannot supply them.
    auto Ungranted=RuntimePtr(Lui_Create(),Lui_Destroy);
    Require(Ungranted && Lui_DeclareCapabilities(Ungranted.get(),&Caps) && !Lui_RunProject(Ungranted.get(),Handle,&Run) &&
        std::string(Lui_GetLastError(Ungranted.get()))=="[LUI:Modules] CapabilityDenied","declarations supplied owner grants");
    std::printf("[LUI:DeclarationsTest] Registration digest %s\n",Registration.c_str());
}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    const auto Root=Fs::temp_directory_path()/("LuiDeclarations-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); Fs::create_directory(Root);
    try {
        std::ifstream File(LUI_TYPES_PATH,std::ios::binary); const std::string Definitions{std::istreambuf_iterator<char>{File},std::istreambuf_iterator<char>{}};
        Require(!Definitions.empty(),"definitions absent"); Cases(Root,Definitions); Fs::remove_all(Root);
        std::puts("[LUI:DeclarationsTest] Captured metadata, generated types, package hashes and pre-entry registration checks passed"); return 0;
    } catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:DeclarationsTest] %s\n",Error.what()); return 1;}
}
