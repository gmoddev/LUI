#include "LuiNativeServices.h"
#include "LuiProject.h"
#include "Project.h"
#include "ProjectCheck.h"
#include "Package.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace Fs = std::filesystem;
using Json = nlohmann::json;
namespace {
void Require(bool Value, const char* Message) { if (!Value) throw std::runtime_error(Message); }
void Write(const Fs::path& Path, const std::string& Bytes) {
    Fs::create_directories(Path.parent_path()); std::ofstream File(Path, std::ios::binary);
    File.write(Bytes.data(), Bytes.size()); Require(bool(File), "fixture write");
}
std::string Read(const Fs::path& Path) {
    std::ifstream File(Path, std::ios::binary); Require(bool(File), "fixture read");
    return {std::istreambuf_iterator<char>(File), {}};
}
Json Method(const char* Name, uint32_t Input, uint32_t Result, uint32_t InputKind = 0, uint32_t ResultKind = 0) {
    return {{"Name",Name},{"Profile","OwnedAsync"},{"MaxInputBytes",Input},{"MaxResultBytes",Result},
        {"InputKind",InputKind},{"ResultKind",ResultKind},{"DeadlineMilliseconds",30000}};
}
void Cases(const Fs::path& Root, const char* Rust) {
#ifdef _WIN32
    const std::string Platform = "WindowsX64", Binary = "Native/Proof.dll";
#else
    const std::string Platform = "LinuxX64", Binary = "Native/Proof.so";
#endif
    const auto Bytes = Rust ? Read(Fs::u8path(Rust)) : "SYNTHETIC_NONEXECUTABLE_BINARY";
    Write(Root/Binary, Bytes);
    auto Methods = Json::array({Method("Echo",8*1024*1024,8*1024*1024),Method("Counter",0,8,0,1),Method("Increment",0,8,1,0),
        Method("Hold",65536,65536),Method("Wake",0,0),Method("Fail",0,0),Method("Progress",65536,65536),Method("StallShutdown",0,0)});
    Json Sidecar{{"SchemaVersion",3},{"AbiVersion",2},{"Provider","Native"},
        {"Extension",{{"Name","RustProof"},{"Version",1},{"RequiredCapabilities",{"NativeExtensions","HostServices"}},
            {"Provenance",{{"Kind","LocalVendor"},{"Id","Synthetic-Rust-fixture"}}},
            {"Bindings",Json::array({{{"Platform",Platform},{"Path",Binary},{"Sha256",Lui::Project::Hash(Bytes)}}})}}},
        {"Services",Json::array({{{"Name","RustProof"},{"Methods",Methods},{"ResourceKindCount",1}}})}};
    Json Manifest{{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",{"NativeExtensions","HostServices"}},
        {"Extensions",{"RustProof"}},{"ExtensionDeclarations",{"Native.json"}},{"Assets",Json::array()},
        {"Diagnostics",{{"Mode","Development"}}},
        {"Modules",{{"Root","Src"},{"Files",{"Main.luau"}},{"Requires",Json::array()},{"Dependencies",Json::array()}}}};
    Write(Root/"lui.json",Manifest.dump()); Write(Root/"Native.json",Sidecar.dump());
    Write(Root/"Src/Main.luau",
        "local Service=app:GetService('RustProof')\n"
        "local Request=Service:Echo(buffer.fromstring('synthetic'),nil,function(Result,Handle,Error)\n"
        " assert(Error=='Canceled' and not Handle and not Result)\n"
        "end)\nlocal Accepted:boolean=Request:Cancel()\n");
    std::string Error; auto Sources = Lui::Project::Prepare(Root/"lui.json", {}, Error);
    Require(bool(Sources), Error.c_str());
    Require(Sources->GetDeclarations().NativeProviders.at("RustProof").AbiVersion == 2, "ABI metadata lost");
    Require(Json::parse(Sources->GetDeclarations().Descriptor)["SchemaVersion"] == 3, "owned profile negotiation");
    Lui::TypeCheck::Options Options; Options.DevelopmentDiagnostics = true;
    auto Checked = Lui::TypeCheck::Check(*Sources, Read(Fs::u8path(LUI_TYPES_PATH)), Options, Error);
    Require(Checked && Checked->Diagnostics.empty(), "owned declarations did not typecheck");
    auto Broken = Sidecar; Broken["AbiVersion"] = 3; Write(Root/"Native.json",Broken.dump());
    Require(!Lui::Project::Prepare(Root/"lui.json",{},Error), "unknown ABI accepted");
    Broken = Sidecar; Broken["Services"][0]["Methods"][0]["Profile"] = "Any"; Write(Root/"Native.json",Broken.dump());
    Require(!Lui::Project::Prepare(Root/"lui.json",{},Error), "unknown value profile accepted");
    Broken = Sidecar; Broken["Services"][0]["Methods"][0]["InputKind"] = 2; Write(Root/"Native.json",Broken.dump());
    Require(!Lui::Project::Prepare(Root/"lui.json",{},Error), "unknown handle kind accepted");
    Write(Root/"Native.json",Sidecar.dump());
    if (Rust) {
        const auto Path = (Root/"lui.json").u8string(); LuiProjectOptionsV1 Settings{sizeof(Settings),1};
        LuiProject* Project = nullptr; char Detail[256]{};
        Require(Lui_ProjectPrepare(&Settings,Path.data(),Path.size(),&Project,Detail,sizeof(Detail)), Detail);
        // Remove the source binary: execution must use the captured immutable bytes.
        Fs::remove(Root/Binary);
        LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_RESTRICTED,0}; auto* Runtime = Lui_CreateWithPolicy(&Policy);
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants),1,3}; Require(Lui_DeclareCapabilities(Runtime,&Grants),"grant");
        LuiProjectNativeOptionsV1 Native{sizeof(Native),1,LUI_PROJECT_NATIVE_TRUSTED,0};
        Require(Lui_LoadProjectNative(Runtime,Project,&Native),Lui_GetLastError(Runtime));
        LuiProjectRunOptionsV1 Run{sizeof(Run),1,0,0,0}; Require(Lui_RunProject(Runtime,Project,&Run),Lui_GetLastError(Runtime));
        Lui_Pump(Runtime); Lui_Destroy(Runtime); Lui_ProjectRelease(Project);
    }
}
}
int main(int Count, char** Arguments) {
    const auto Root = Fs::temp_directory_path()/("LuiOwnedProject-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try { Cases(Root,Count == 2 ? Arguments[1] : nullptr); Fs::remove_all(Root);
        std::puts("[LUI:OwnedProjectTest] versioned metadata, checking and captured binding passed"); return 0;
    } catch (const std::exception& Error) { std::fprintf(stderr,"[LUI:OwnedProjectTest] %s\n",Error.what()); return 1; }
}
