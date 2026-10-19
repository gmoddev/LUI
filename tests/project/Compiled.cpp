#include "Project.h"
#include "LuiProject.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace Fs = std::filesystem;
using Json = nlohmann::json;
using namespace Lui::Project;
extern "C" int LuiProjectCHeaderCheck(void);
static void Require(bool Value, const char* Label) { if (!Value) throw std::runtime_error(Label); }
static void Write(const Fs::path& Path, const std::string& Bytes) {
    Fs::create_directories(Path.parent_path());
    std::ofstream File(Path, std::ios::binary); File.write(Bytes.data(), Bytes.size());
    Require(bool(File), "fixture write failed");
}
static Json Manifest() {
    return {{"SchemaVersion",2}, {"Script","Src/Main.luau"}, {"Capabilities",Json::array()},
        {"Extensions",Json::array()}, {"Assets",Json::array()}, {"ExtensionDeclarations",Json::array()},
        {"Diagnostics",{{"Mode","Development"}}},
        {"Modules",{{"Root","Src"}, {"Files",{"Main.luau","Data.luau","Unused.luau"}},
            {"Requires",Json::array()}, {"Dependencies",Json::array()}}}};
}
static void Files(const Fs::path& Root) {
    Write(Root/"lui.json",Manifest().dump());
    Write(Root/"Src/Main.luau","local Data = require('./Data')\nassert(Data.Count == 3)\n");
    Write(Root/"Src/Data.luau","return {Count = 3}\n");
    // Valid source that MUST NOT execute while preparing/compiling/querying.
    Write(Root/"Src/Unused.luau","error('PRIVATE_COMPILER_CANARY')\nreturn false\n");
}
static std::shared_ptr<const Snapshot> Capture(const Fs::path& Root) {
    std::string Error;
    auto Value = Prepare(Root/"lui.json",{},Error);
    Require(Value && Error.empty(), "capture failed"); return Value;
}
static void FailedCompile(const std::shared_ptr<const Snapshot>& Sources, Options Settings, const char* Code) {
    std::string Error = "old error";
    Require(!Compile(Sources,Settings,Error) && Error == std::string("[LUI:Project] ") + Code, Code);
    Require(Error.find("PRIVATE_COMPILER_CANARY") == std::string::npos, "compiler payload escaped");
}
static LuiProjectOptionsV1 HostOptions() {
    LuiProjectOptionsV1 Value{}; Value.StructSize=sizeof(Value); Value.AbiVersion=LUI_PROJECT_ABI_VERSION; return Value;
}
static LuiProjectSummaryV1 SummaryOptions() {
    LuiProjectSummaryV1 Value{}; Value.StructSize=sizeof(Value); Value.AbiVersion=LUI_PROJECT_ABI_VERSION; return Value;
}
static LuiProjectSourceV1 SourceOptions() {
    LuiProjectSourceV1 Value{}; Value.StructSize=sizeof(Value); Value.AbiVersion=LUI_PROJECT_ABI_VERSION; return Value;
}
static int LUI_CALL Canceled(void* Context) { ++*static_cast<int*>(Context); return 1; }
static void Cases(const Fs::path& Suite) {
    const auto Root=Suite/"First"; Files(Root);
    auto Sources=Capture(Root); std::string Error;
    auto Compiled=Compile(Sources,{},Error);
    Require(Compiled && Error.empty() && Compiled->GetArtifacts().size()==3, "all-source compilation failed");
    Require(Compiled->GetDigest()=="9f8e83b5a5a2559ab6e2b15af030062cbd7589ecc9d2db7597e1dc1760eadb6f",
        "portable compiled fixture digest changed");
    Require(Compiled->GetDigest()!=Sources->GetDigest(), "source/compiled profile digest conflated");
    auto Descriptor=Json::parse(Compiled->GetDescriptor());
    Require(Descriptor["PreparationVersion"]==2 && Descriptor["CompileProfile"]["Name"]=="LuauBytecode" &&
        Descriptor["CompileProfile"]["Debug"]==2 && Descriptor["ArtifactHashes"].size()==3, "compiled profile missing");
    size_t Bytes=0;
    for (const auto& Pair:Compiled->GetArtifacts()) {
        Require(!Pair.second.Bytes.empty() && Pair.second.Bytes.front()!=0 && Hash(Pair.second.Bytes)==Pair.second.Hash,
            "invalid compiled artifact"); Bytes+=Pair.second.Bytes.size();
    }
    Require(Bytes==Compiled->GetBytecodeBytes() && Compiled->GetRetainedBytes()>Bytes, "retained accounting missing");
    const auto Copy=Suite/"Second"; Files(Copy);
    auto Reordered=Manifest(); Reordered["Modules"]["Files"]={"Unused.luau","Data.luau","Main.luau"};
    Write(Copy/"lui.json",Reordered.dump());
    auto Equivalent=Compile(Capture(Copy),{},Error);
    Require(Equivalent && Equivalent->GetDigest()==Compiled->GetDigest() &&
        Equivalent->GetDescriptor()==Compiled->GetDescriptor(), "root/order changed compiled digest");
    // Frozen bytes are used even if a now-unused file becomes invalid on disk.
    Write(Root/"Src/Unused.luau","PRIVATE_COMPILER_CANARY !!! syntax error");
    auto Frozen=Compile(Sources,{},Error);
    Require(Frozen && Frozen->GetDigest()==Compiled->GetDigest(), "compiler reread disk");
    FailedCompile(Capture(Root),{},"CompileFailed");
    Options Limits;
    Limits.Bounds.ArtifactBytes=1; FailedCompile(Sources,Limits,"LimitExceeded");
    Limits={}; Limits.Bounds.BytecodeBytes=1; FailedCompile(Sources,Limits,"LimitExceeded");
    Limits={}; Limits.Bounds.RetainedBytes=1; FailedCompile(Sources,Limits,"LimitExceeded");
    Limits={}; Limits.Bounds.ArtifactBytes=0; FailedCompile(Sources,Limits,"InvalidOptions");
    Limits={}; ++Limits.Bounds.BytecodeBytes; FailedCompile(Sources,Limits,"InvalidOptions");
    FailedCompile({}, {}, "InvalidInput");
    // Admission at exact bytecode limits succeeds; crossing by one fails.
    Limits={}; Limits.Bounds.BytecodeBytes=Bytes;
    Limits.Bounds.ArtifactBytes=0;
    for (const auto& Pair:Compiled->GetArtifacts())
        if (Pair.second.Bytes.size()>Limits.Bounds.ArtifactBytes) Limits.Bounds.ArtifactBytes=Pair.second.Bytes.size();
    Require(Compile(Sources,Limits,Error)!=nullptr, "exact bytecode limit rejected");
    --Limits.Bounds.BytecodeBytes; FailedCompile(Sources,Limits,"LimitExceeded");
    int Queries=0; Limits={}; Limits.Canceled=[&] { return ++Queries==7; };
    FailedCompile(Sources,Limits,"Canceled"); Require(Queries==7,"compiler cancellation not observed");
    Limits.Canceled=[] { throw std::runtime_error("PRIVATE_COMPILER_CANARY"); return false; };
    FailedCompile(Sources,Limits,"CompileFailed");
    Files(Root);
    Write(Root/"Src/Data.luau","return {Count = 4}\n");
    Require(Compile(Capture(Root),{},Error)->GetDigest()!=Compiled->GetDigest(), "source edit did not invalidate digest");
    Files(Root);

    // Worker-callable ABI with explicit version/size, bounded errors and borrowed
    // immutable views. No runtime exists during any of these calls.
    auto Options=HostOptions(); const auto Path=(Root/"lui.json").u8string();
    LuiProject* Handle=nullptr; char Message[128]{};
    Require(Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message)) && Handle && !*Message,
        "host prepare failed");
    auto Summary=SummaryOptions(); auto View=SourceOptions();
    Require(Lui_ProjectGetSummary(Handle,&Summary) && Summary.PreparationVersion==2 && Summary.SourceCount==3 &&
        Summary.CompiledDigest==Compiled->GetDigest() && Summary.SourceDigest==Sources->GetDigest(), "host summary failed");
    Require(Lui_ProjectGetSource(Handle,0,&View) && std::strcmp(View.Id,"app:Data.luau")==0 &&
        std::string(View.Bytecode,View.BytecodeBytes)==Compiled->GetArtifacts().at(View.Id).Bytes, "host view mismatch");
    auto Original=View;
    Require(!Lui_ProjectGetSource(Handle,3,&View) && View.Source==Original.Source, "failed query modified view");
    View.AbiVersion=2;
    Require(!Lui_ProjectGetSource(Handle,0,&View) && View.AbiVersion==2, "source version accepted"); View=SourceOptions();
    --Summary.StructSize; Require(!Lui_ProjectGetSummary(Handle,&Summary),"summary size accepted"); Summary=SummaryOptions();
    // Resolve after deleting disk input; only captured declared data can supply it.
    Fs::remove(Root/"Src/Data.luau");
    Require(Lui_ProjectResolve(Handle,"app:Main.luau",13,"./Data",6,&View,Message,sizeof(Message)) &&
        std::strcmp(View.Id,"app:Data.luau")==0 && std::string(View.Source,View.SourceBytes)=="return {Count = 3}\n",
        "resolve reread disk or changed source");
    Require(!Lui_ProjectResolve(Handle,"app:Main.luau",13,"./Main",6,&View,Message,sizeof(Message)) &&
        std::strcmp(Message,"[LUI:Project] EntryImportDenied")==0, "entry import accepted");
    Require(!Lui_ProjectResolve(Handle,"app:Main.luau",13,"./NoSuch",8,&View,Message,sizeof(Message)) &&
        std::strcmp(Message,"[LUI:Project] MissingModule")==0, "missing module accepted");
    Require(!Lui_ProjectResolve(Handle,"app:Main.luau",13,"./Da\0ta",7,&View,Message,sizeof(Message)),"embedded NUL accepted");
    std::atomic<bool> WorkerPassed{false}; Lui_ProjectRetain(Handle);
    std::thread Worker([&] {
        auto Info=SummaryOptions();
        WorkerPassed=Lui_ProjectGetSummary(Handle,&Info) && Info.CompiledDigest==Compiled->GetDigest();
        Lui_ProjectRelease(Handle);
    });
    // Retained views remain valid while the worker has its own reference.
    Require(Lui_ProjectGetSummary(Handle,&Summary),"parallel owner query failed");
    Worker.join(); Require(WorkerPassed,"worker immutable query failed");
    // Transfer the last reference to a worker. Releasing the original owner
    // must not invalidate views used under the worker's retained reference.
    Lui_ProjectRetain(Handle); auto* Transferred=Handle;
    std::promise<void> Ready; auto Wait=Ready.get_future(); WorkerPassed=false;
    std::thread Recipient([&] {
        Wait.wait(); auto Info=SummaryOptions();
        WorkerPassed=Lui_ProjectGetSummary(Transferred,&Info) && Info.CompiledDigest==Compiled->GetDigest();
        Lui_ProjectRelease(Transferred);
    });
    Lui_ProjectRelease(Handle); Handle=nullptr; Ready.set_value(); Recipient.join();
    Require(WorkerPassed,"transferred reference did not retain snapshot");
    Files(Root);
    std::thread Preparation([&] {
        auto Settings=HostOptions(); char Failure[128]{}; LuiProject* Prepared=nullptr;
        auto Info=SummaryOptions();
        WorkerPassed=Lui_ProjectPrepare(&Settings,Path.data(),Path.size(),&Prepared,Failure,sizeof(Failure)) &&
            Lui_ProjectGetSummary(Prepared,&Info) && Info.CompiledDigest==Compiled->GetDigest();
        Lui_ProjectRelease(Prepared);
    });
    Preparation.join(); Require(WorkerPassed,"preparation could not run without a VM on worker");
    Options.AbiVersion=2;
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message)) && !Handle,"unknown ABI accepted");
    Options=HostOptions(); --Options.StructSize;
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message)),"options size accepted");
    Options=HostOptions(); Options.Reserved=1;
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message)),"reserved flag accepted");
    Options=HostOptions(); Options.BytecodeBytes=64*1024*1024+1;
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message)) &&
        std::strcmp(Message,"[LUI:Project] InvalidOptions")==0,"host raised quota");
    Options=HostOptions(); Options.BytecodeBytes=1;
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message)) && !Handle &&
        std::strcmp(Message,"[LUI:Project] LimitExceeded")==0,"host did not narrow quota");
    Options=HostOptions(); int Calls=0; Options.Canceled=Canceled; Options.CancellationContext=&Calls;
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message)) &&
        std::strcmp(Message,"[LUI:Project] Canceled")==0 && Calls==1,"host cancellation failed");
    Options=HostOptions();
    char Tiny[3]={'A','B','C'};
    Require(!Lui_ProjectPrepare(&Options,"\0",1,&Handle,Tiny,2) && Tiny[0]=='[' && Tiny[1]==0 && Tiny[2]=='C',
        "error buffer truncation overflow");
    Tiny[0]='A'; Require(!Lui_ProjectPrepare(&Options,"\0",1,&Handle,Tiny,1) && Tiny[0]==0,"one-byte error buffer failed");
    Require(!Lui_ProjectPrepare(&Options,"\xff",1,&Handle,nullptr,0),"invalid UTF8 accepted");
    Require(!Lui_ProjectPrepare(&Options,Path.data(),4097,&Handle,Message,sizeof(Message)),"oversize path accepted");
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,nullptr,1),"null error buffer accepted");
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),nullptr,Message,sizeof(Message)),"null output accepted");
    Write(Root/"Src/Unused.luau","PRIVATE_COMPILER_CANARY !!!");
    Require(!Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Handle,Message,sizeof(Message)) && !Handle &&
        std::strcmp(Message,"[LUI:Project] CompileFailed")==0,"ABI leaked compiler text");
    Require(LuiProjectCHeaderCheck(),"C host ABI check failed");
    std::printf("[LUI:ProjectTest] Compiled fixture digest %s\n",Compiled->GetDigest().c_str());
}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    const auto Suite=Fs::temp_directory_path()/ ("LuiCompiledTests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fs::create_directory(Suite);
    try {
        Cases(Suite);
        // Explicit fresh fixture root; this suite creates no links or external paths.
        Fs::remove_all(Suite);
        std::puts("[LUI:ProjectTest] Compiled snapshots, quotas, cancellation and C host lifetime passed"); return 0;
    } catch (const std::exception& Reason) {
        std::fprintf(stderr,"[LUI:ProjectTest] %s\n",Reason.what()); return 1;
    }
}
