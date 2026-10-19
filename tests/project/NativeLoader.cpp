#include "LuiProject.h"
#include "Project.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace Fs=std::filesystem;
using Json=nlohmann::json;
static void Require(bool Value,const char* Label) {if(!Value) throw std::runtime_error(Label);}
static void Write(const Fs::path& Path,const std::string& Text) {Fs::create_directories(Path.parent_path()); std::ofstream File(Path,std::ios::binary); File<<Text; Require(bool(File),"fixture write failed");}
int main(int Count,char** Arguments) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    try {
        Require(Count==2,"extension path required"); std::ifstream File(Fs::u8path(Arguments[1]),std::ios::binary);
        const std::string Bytes{std::istreambuf_iterator<char>{File},std::istreambuf_iterator<char>{}}; Require(!Bytes.empty(),"extension missing");
        const auto Root=Fs::temp_directory_path()/("LuiVerifiedLoader-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#ifdef _WIN32
        const std::string Platform="WindowsX64", Binary="Native/Sample.dll";
#else
        const std::string Platform="LinuxX64", Binary="Native/Sample.so";
#endif
        Write(Root/Binary,Bytes); Write(Root/"Src/Main.luau","assert(app:GetService('NativeMath'):Add(2,3)==5); assert(app:GetService('NativeMath'):BeginCompletion())");
        Json Methods=Json::array({{{"Name","Add"},{"Arguments",{"number","number"}},{"Returns","number"}},
            {{"Name","Echo"},{"Arguments",{"string"}},{"Returns","string"}},{{"Name","BeginCompletion"},{"Arguments",Json::array()},{"Returns","boolean"}},
            {{"Name","GetCompletionCount"},{"Arguments",Json::array()},{"Returns","number"}},{{"Name","WasCompletionOnOwner"},{"Arguments",Json::array()},{"Returns","boolean"}}});
        Json Declaration{{"SchemaVersion",2},{"AbiVersion",1},{"Provider","Native"},{"Extension",{{"Name","SampleExtension"},{"Version",1},
            {"RequiredCapabilities",{"NativeExtensions"}},{"Provenance",{{"Kind","LocalVendor"},{"Id","Synthetic-test-build"}}},
            {"Bindings",Json::array({{{"Platform",Platform},{"Path",Binary},{"Sha256",Lui::Project::Hash(Bytes)}}})}}},
            {"Services",Json::array({{{"Name","NativeMath"},{"Methods",Methods},{"Signals",Json::array({{{"Name","Completed"},{"Arguments",{"number"}}}})}}})}};
        Write(Root/"Native.json",Declaration.dump());
        Write(Root/"lui.json",Json{{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",{"NativeExtensions"}},{"Extensions",{"SampleExtension"}},
            {"ExtensionDeclarations",{"Native.json"}},{"Assets",Json::array()},{"Diagnostics",{{"Mode","Development"}}},
            {"Modules",{{"Root","Src"},{"Files",{"Main.luau"}},{"Requires",Json::array()},{"Dependencies",Json::array()}}}}.dump());
        const auto Path=(Root/"lui.json").u8string(); LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=1;
        LuiProject* Project=nullptr; char Error[256]{}; Require(Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Project,Error,sizeof(Error)),Error);
        LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,1,0}; auto* Runtime=Lui_CreateWithPolicy(&Policy);
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants),1,LUI_CAPABILITY_NATIVE_EXTENSIONS}; Require(Lui_DeclareCapabilities(Runtime,&Grants),"grant failed");
        LuiProjectNativeOptionsV1 Native{sizeof(Native),1,LUI_PROJECT_NATIVE_TRUSTED,0};
        auto Rejected=[&](Json Changed,const char* Label) {
            Write(Root/"Native.json",Changed.dump()); LuiProject* Invalid=nullptr;
            Require(Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Invalid,Error,sizeof(Error)),Error);
            Require(!Lui_LoadProjectNative(Runtime,Invalid,&Native),Label);
            Require(std::string(Lui_GetExtensionSchemaJson(Runtime)).find("NativeMath")==std::string::npos,"failed load retained registrations");
            Require(std::string(Lui_GetLastError(Runtime)).find(Root.u8string())==std::string::npos,"native failure exposed physical path");
            Lui_ProjectRelease(Invalid);
        };
        auto Changed=Declaration; Changed["Extension"]["Version"]=2; Rejected(Changed,"identity mismatch admitted");
        Changed=Declaration; Changed["Services"][0]["Methods"][0]["Returns"]="string"; Rejected(Changed,"registration mismatch admitted");
        LuiProjectNativeOptionsV1 Untrusted{sizeof(Untrusted),1,0,0};
        Require(!Lui_LoadProjectNative(Runtime,Project,&Untrusted),"implicit native trust admitted");
        Fs::remove(Root/Binary); // Actual load must use captured bytes, not this path.
        Require(Lui_LoadProjectNative(Runtime,Project,&Native),Lui_GetLastError(Runtime));
        LuiProjectRunOptionsV1 Run{sizeof(Run),1,0,0,0}; Require(Lui_RunProject(Runtime,Project,&Run),Lui_GetLastError(Runtime));
        Require(!Lui_LoadProjectNative(Runtime,Project,&Native),"native provider loaded twice");
        Lui_Destroy(Runtime);
        for(int Generation=0;Generation<3;++Generation) {
            Policy.Mode=Generation==2?LUI_DIAGNOSTICS_RESTRICTED:LUI_DIAGNOSTICS_DEVELOPMENT;
            Runtime=Lui_CreateWithPolicy(&Policy); Require(Lui_DeclareCapabilities(Runtime,&Grants),"reload grant failed");
            Require(Lui_LoadProjectNative(Runtime,Project,&Native),Lui_GetLastError(Runtime));
            Require(Lui_RunProject(Runtime,Project,&Run),Lui_GetLastError(Runtime));
            Lui_Destroy(Runtime); // Shutdown joins worker before releasing its image.
        }
        Lui_ProjectRelease(Project); Fs::remove_all(Root);
        std::puts("[LUI:NativeLoaderTest] Captured actual library, identity, registration and execution passed"); return 0;
    } catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:NativeLoaderTest] %s\n",Error.what()); return 1;}
}
