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
static std::string Read(const Fs::path& Path) {
    std::ifstream File(Path,std::ios::binary); Require(bool(File),"fixture read failed"); return {std::istreambuf_iterator<char>{File},std::istreambuf_iterator<char>{}};
}
static const std::string WindowsBytes("\0\xffSYNTHETIC_WINDOWS_NOT_A_DLL",29);
static const std::string LinuxBytes("\0\xfeSYNTHETIC_LINUX_NOT_A_LIBRARY",31);
static Json Sidecar() {
    return {{"SchemaVersion",2},{"AbiVersion",1},{"Provider","Native"},
        {"Extension",{{"Name","NativeMath"},{"Version",1},{"RequiredCapabilities",{"NativeExtensions"}},
            {"Provenance",{{"Kind","LocalVendor"},{"Id","Synthetic-1"}}},
            {"Bindings",Json::array({{{"Platform","WindowsX64"},{"Path","Native/Math.dll"},{"Sha256",Hash(WindowsBytes)}},
                {{"Platform","LinuxX64"},{"Path","Native/libMath.so"},{"Sha256",Hash(LinuxBytes)}}})}}},
        {"Services",Json::array({{{"Name","MathService"},{"Methods",Json::array({{{"Name","Add"},{"Arguments",{"number","number"}},{"Returns","number"}}})},
            {"Signals",Json::array({{{"Name","Completed"},{"Arguments",{"number"}}}})}}})}};
}
static Json Manifest() {
    return {{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",{"NativeExtensions"}},{"Extensions",{"NativeMath"}},
        {"ExtensionDeclarations",{"Types/Math.json"}},{"Assets",Json::array()},{"Diagnostics",{{"Mode","Development"}}},
        {"Modules",{{"Root","Src"},{"Files",{"Main.luau","Unused.luau"}},{"Requires",Json::array()},{"Dependencies",Json::array()}}}};
}
static void Fixture(const Fs::path& Root) {
    Write(Root/"lui.json",Manifest().dump()); Write(Root/"Types/Math.json",Sidecar().dump(2));
    Write(Root/"Native/Math.dll",WindowsBytes); Write(Root/"Native/libMath.so",LinuxBytes);
    Write(Root/"Src/Main.luau","local N:number=app:GetService('MathService'):Add(2,3); app:GetService('MathService').Completed:Connect(function(N:number) end)");
    Write(Root/"Src/Unused.luau","return function() error('NEVER_EXECUTE_NATIVE_APP') end");
}
static void Bad(const Fs::path& Root,Json Value,const char* Code="InvalidDeclaration") {
    Write(Root/"Types/Math.json",Value.dump()); std::string Error;
    Require(!Prepare(Root/"lui.json",{},Error) && Error==std::string("[LUI:Project] ")+Code,Code);
}
static void Cases(const Fs::path& Root,const std::string& Definitions) {
    const auto Input=Root/"Input", Output=Root/"Output"; Fixture(Input); std::string Error;
    auto Sources=Prepare(Input/"lui.json",{},Error); Require(bool(Sources),Error.c_str());
    Require(Sources->GetNativeBinaries().size()==2 && Sources->GetDeclarations().NativeProviders.size()==1,"native capture missing");
    auto Descriptor=Json::parse(Sources->GetDescriptor());
    Require(Sources->GetDeclarations().Digest=="ed21c28f21ad6e0d5a09e1f85dfcad10b474da3956b39ce53a20a72a40da3c9c","portable native registration fingerprint changed");
    Require(Descriptor["NativeBindings"].size()==2 && Descriptor["ExtensionRegistration"]["SchemaVersion"]==2,"native descriptor absent");
    Lui::TypeCheck::Options Check; Check.DevelopmentDiagnostics=true;
    auto Types=Lui::TypeCheck::Check(*Sources,Definitions,Check,Error);
    Require(Types && Types->Diagnostics.empty(),"native metadata types did not infer without loading synthetic bytes");
    NativeObservation Observation; Observation.Name="NativeMath"; Observation.Platform="WindowsX64"; Observation.BinaryHash=Hash(WindowsBytes);
    Observation.AbiVersion=1; Observation.Version=1; Observation.RequiredCapabilities=LUI_CAPABILITY_NATIVE_EXTENSIONS;
    Observation.Services=Sources->GetDeclarations().Services;
    Require(MatchNative(Sources->GetDeclarations(),Observation,Error),"native receipt did not match");
    auto Wrong=Observation; Wrong.Version=2;
    Require(!MatchNative(Sources->GetDeclarations(),Wrong,Error) && Error=="[LUI:Project] IdentityMismatch","wrong version matched");
    Wrong=Observation; Wrong.Name="Other"; Require(!MatchNative(Sources->GetDeclarations(),Wrong,Error),"wrong identity matched");
    Wrong=Observation; Wrong.AbiVersion=2; Require(!MatchNative(Sources->GetDeclarations(),Wrong,Error),"wrong ABI matched");
    Wrong=Observation; Wrong.RequiredCapabilities|=LUI_CAPABILITY_HOST_SERVICES;
    Require(!MatchNative(Sources->GetDeclarations(),Wrong,Error) && Error=="[LUI:Project] CapabilityMismatch","extra capabilities matched");
    Wrong=Observation; Wrong.Platform="MacOS";
    Require(!MatchNative(Sources->GetDeclarations(),Wrong,Error) && Error=="[LUI:Project] UnsupportedPlatform","platform fallback applied");
    Wrong=Observation; Wrong.BinaryHash=Hash(LinuxBytes);
    Require(!MatchNative(Sources->GetDeclarations(),Wrong,Error) && Error=="[LUI:Project] BinaryMismatch","wrong binary matched");
    Wrong=Observation; Wrong.Services.at("MathService").Methods.at("Add").Arguments={"string","number"};
    Require(!MatchNative(Sources->GetDeclarations(),Wrong,Error) && Error=="[LUI:Project] RegistrationMismatch","wrong primitive arguments matched");
    Wrong=Observation; Wrong.Services.at("MathService").Signals.clear(); Require(!MatchNative(Sources->GetDeclarations(),Wrong,Error),"missing signal matched");
    auto Package=CapturePackage(Input/"lui.json",{},Error); Require(bool(Package),Error.c_str());
    Require(Package->GetDigest()=="7a3cd0f9409fd019dad4e5b8561110d062f2dc6c390a0e8adce62f4594312b04","portable native package fingerprint changed");
    const auto Index=Json::parse(Package->GetIndex());
    Require(Index["SchemaVersion"]==2 && Index["Profile"]=="DeclaredContentV2" && Index["NativeBindings"].size()==2,"native package index absent");
    Fs::remove(Input/"Native/Math.dll"); Write(Input/"Types/Math.json","MUTATED_DECLARATION");
    Require(StagePackage(*Package,Output,{},Error),Error.c_str());
    Require(Read(Output/"Native/Math.dll")==WindowsBytes && Read(Output/"Native/libMath.so")==LinuxBytes,"staging reread or changed bytes");
    auto Verified=VerifyPackage(Output,{},Error);
    Require(Verified && Verified->GetDigest()==Package->GetDigest(),"staged native package diverged");
    Write(Output/"Native/libMath.so","MODIFIED_NATIVE_BINARY");
    Require(!VerifyPackage(Output,{},Error) && Error=="[LUI:Project] BinaryMismatch","edited binary passed verification");
    Require(Verified->GetPrepared().GetSources().GetNativeBinaries().at("Native/libMath.so").Bytes==LinuxBytes,"verified binary was not frozen");
    Fixture(Input);
    auto Value=Sidecar(); Value["Extension"]["Bindings"][0]["Sha256"]=std::string(64,'0'); Bad(Input,Value,"BinaryMismatch");
    Value=Sidecar(); Value["Extension"]["Bindings"][0]["Path"]="../Escape.dll"; Bad(Input,Value,"InvalidPath");
    Value=Sidecar(); Value["Extension"]["Bindings"][0]["Platform"]="WindowsARM64"; Bad(Input,Value,"UnsupportedPlatform");
    Value=Sidecar(); Value["Extension"]["Bindings"][1]["Platform"]="WindowsX64"; Bad(Input,Value,"InvalidPath");
    Value=Sidecar(); Value["Extension"]["Bindings"][0]["Path"]="Types/Math.json"; Bad(Input,Value,"InvalidPath");
    Value=Sidecar(); Value["Extension"]["RequiredCapabilities"]={"HostServices"}; Bad(Input,Value);
    Value=Sidecar(); Value["Extension"]["RequiredCapabilities"]={"NativeExtensions","NativeExtensions"}; Bad(Input,Value);
    Value=Sidecar(); Value["Extension"]["RequiredCapabilities"]={"NativeExtensions","Clipboard"}; Bad(Input,Value,"CapabilityMismatch");
    Value=Sidecar(); Value["Extension"]["Version"]=0; Bad(Input,Value);
    Value=Sidecar(); Value["Extension"]["Version"]=UINT64_C(4294967296); Bad(Input,Value);
    Value=Sidecar(); Value["AbiVersion"]=2; Bad(Input,Value);
    Value=Sidecar(); Value["Extension"]["Bindings"][1]["Platform"]="WindowsX64";
    Value["Extension"]["Bindings"][1]["Path"]="Native/Other.dll"; Bad(Input,Value);
    Value=Sidecar(); Value["Extension"]["Bindings"][1]["Path"]="native/libMath.so"; Bad(Input,Value,"DuplicateIdentity");
    Value=Sidecar(); Value["Services"][0]["Name"]="WindowService"; Bad(Input,Value);
    Value=Sidecar(); Value["Extension"]["Provenance"]["Kind"]="Git"; Bad(Input,Value);
    Fixture(Input); auto BadManifest=Manifest(); BadManifest["Extensions"]=Json::array(); Write(Input/"lui.json",BadManifest.dump());
    Require(!Prepare(Input/"lui.json",{},Error) && Error=="[LUI:Project] InvalidManifest","unlisted provider accepted");
    Fixture(Input); Options Limits; Limits.Bounds.BinaryBytes=1;
    Require(!Prepare(Input/"lui.json",Limits,Error) && Error=="[LUI:Project] LimitExceeded","binary byte bound ignored");
    Limits={}; Limits.Bounds.NativeBytes=WindowsBytes.size()+LinuxBytes.size()-1;
    Require(!Prepare(Input/"lui.json",Limits,Error) && Error=="[LUI:Project] LimitExceeded","aggregate native bound ignored");
    Limits={}; ++Limits.Bounds.NativeBytes;
    Require(!Prepare(Input/"lui.json",Limits,Error) && Error=="[LUI:Project] InvalidOptions","raised native bound accepted");
    Limits={}; Limits.Canceled=[] {return true;}; Require(!Prepare(Input/"lui.json",Limits,Error) && Error=="[LUI:Project] Canceled","native cancellation ignored");
    Fixture(Input); Fs::remove(Input/"Native/libMath.so"); Fs::create_hard_link(Input/"Native/Math.dll",Input/"Native/libMath.so");
    Value=Sidecar(); Value["Extension"]["Bindings"][1]["Sha256"]=Hash(WindowsBytes); Bad(Input,Value,"DuplicateIdentity");
    Fs::remove(Input/"Native/libMath.so"); Fixture(Input);
    // A foreign target is required too; no current-OS shortcut or discovery.
    Fs::remove(Input/"Native/libMath.so");
    Require(!Prepare(Input/"lui.json",{},Error) && Error=="[LUI:Project] MissingFile","missing foreign target ignored");
    Fixture(Input);
    auto Mix=Manifest(); Mix["Capabilities"]={"HostServices","NativeExtensions"}; Mix["ExtensionDeclarations"]={"Types/Math.json","Types/Host.json"};
    Write(Input/"lui.json",Mix.dump());
    Write(Input/"Types/Host.json",Json{{"SchemaVersion",1},{"AbiVersion",1},{"Provider","Host"},
        {"Services",Json::array({{{"Name","HostCounter"},{"Methods",Json::array({{{"Name","Count"},{"Arguments",Json::array()},{"Returns","number"}}})},
            {"Signals",Json::array()}}})}}.dump());
    auto Mixed=Prepare(Input/"lui.json",{},Error);
    Require(Mixed && Mixed->GetDeclarations().Services.size()==2,"mixed providers did not capture");
    Observation.Services=Sources->GetDeclarations().Services;
    Require(MatchNative(Mixed->GetDeclarations(),Observation,Error),"host services polluted native provider comparison");
    Fixture(Input);
    // These bytes deliberately are not libraries: no query/init/load may occur.
    const auto Path=(Input/"lui.json").u8string(); LuiProjectOptionsV1 Host{}; Host.StructSize=sizeof(Host); Host.AbiVersion=LUI_PROJECT_ABI_VERSION;
    LuiProject* Handle=nullptr; char Message[128]{};
    Require(Lui_ProjectPrepare(&Host,Path.data(),Path.size(),&Handle,Message,sizeof(Message)),Message);
    std::unique_ptr<LuiProject,decltype(&Lui_ProjectRelease)> Owned(Handle,Lui_ProjectRelease);
    auto* Runtime=Lui_Create(); Require(Runtime!=nullptr,"runtime create failed");
    LuiCapabilityDeclarationV1 Grants{sizeof(Grants),LUI_EXTENSION_ABI_VERSION,LUI_CAPABILITY_NATIVE_EXTENSIONS};
    Require(Lui_DeclareCapabilities(Runtime,&Grants),"native owner grant failed");
    LuiProjectRunOptionsV1 Run{sizeof(Run),LUI_PROJECT_RUN_ABI_VERSION,LUI_PROJECT_RUN_DEVELOPMENT_DIAGNOSTICS,0,0};
    Require(!Lui_RunProject(Runtime,Handle,&Run) && std::string(Lui_GetLastError(Runtime))=="[LUI:Modules] UnsupportedFeature",
        "native execution/loading enabled before owner policy adapter"); Lui_Destroy(Runtime);
    std::printf("[LUI:NativeBindingsTest] Registration %s Package %s\n",Sources->GetDeclarations().Digest.c_str(),Package->GetDigest().c_str());
}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    const auto Root=Fs::temp_directory_path()/("LuiNativeBindings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); Fs::create_directory(Root);
    try {
        std::ifstream File(LUI_TYPES_PATH,std::ios::binary); const std::string Definitions{std::istreambuf_iterator<char>{File},std::istreambuf_iterator<char>{}};
        Require(!Definitions.empty(),"definitions absent"); Cases(Root,Definitions); Fs::remove_all(Root);
        std::puts("[LUI:NativeBindingsTest] Native identity, capability, platform pins, captured bytes, types and matching passed"); return 0;
    } catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:NativeBindingsTest] %s\n",Error.what()); return 1;}
}
