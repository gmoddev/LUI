#include "LuiFiles.h"
#include "LuiOwnerWake.h"
#include "LuiProject.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdio>
#include <stdexcept>
#include <string>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace Fs=std::filesystem;
static_assert(sizeof(LuiFilePickerCallbacksV1)==32,"x64 selected-picker layout");
static void Require(bool Value,const char* Message) {if(!Value) throw std::runtime_error(Message);}
struct Owner {
    LuiRuntime* Runtime=nullptr;
    Fs::path Root;
    bool Finished=false,Pending=false,Failed=false,Dismissed=false;
    uint64_t Canceled=0,Last=0;
    uint32_t Refusal=0;
    std::thread::id Thread=std::this_thread::get_id();
};
static uint32_t LUI_CALL Pick(void* Pointer,uint64_t Request,uint32_t Kind,const char*) {
    auto& Self=*static_cast<Owner*>(Pointer); Require(std::this_thread::get_id()==Self.Thread,"picker on worker"); Self.Last=Request;
    if(Self.Pending) return 0;
    if(Self.Refusal) return Self.Refusal;
    if(Self.Dismissed) {Lui_CompleteFilePickerV1(Self.Runtime,Request,nullptr,0); return 0;}
    const auto Path=(Kind==1?Self.Root/"input.txt":Kind==2?Self.Root/"output.txt":Self.Root).u8string();
    Require(!Lui_CompleteFilePickerV1(Self.Runtime,Request,Path.c_str(),0),"picker completion"); return 0;
}
static void LUI_CALL Cancel(void* Pointer,uint64_t Request) {auto& Self=*static_cast<Owner*>(Pointer); Self.Canceled=Request;}
static void LUI_CALL Log(void* Pointer,const char* Level,const char* Message) {
    auto& Self=*static_cast<Owner*>(Pointer);
    if(std::string(Level)=="Error") Self.Failed=true;
    if(std::string(Message)=="[LUI:FilesTest] Finished") Self.Finished=true;
}
static void Run(Owner& Self,const char* Code) {Require(Lui_RunScript(Self.Runtime,Code,"SelectedFilesTest")==1,Lui_GetLastError(Self.Runtime));}
static void Pump(Owner& Self) {
    const auto Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!Self.Finished && !Self.Failed && std::chrono::steady_clock::now()<Deadline) {Lui_Pump(Self.Runtime); std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    Require(!Self.Failed,"file callback assertion failed");
    Require(Self.Finished,"file completion missing"); Self.Finished=false;
}
int main() {
#if defined(_WIN32)
    SetErrorMode(0x8003);
#endif
    Owner Self; Self.Root=Fs::temp_directory_path()/("LuiFileProvider-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fs::create_directory(Self.Root); {std::ofstream File(Self.Root/"input.txt"); File<<"alpha";}
    LuiNativeDomainV2* Domain=nullptr;
    try {
        Self.Runtime=Lui_Create(); Require(Self.Runtime,"runtime");
        LuiFilePickerCallbacksV1 Callbacks{sizeof(Callbacks),1,&Self,Pick,Cancel};
        LuiCapabilityDeclarationV1 Denied{sizeof(Denied),1,0}; Require(Lui_DeclareCapabilities(Self.Runtime,&Denied),"deny setup");
        Require(Lui_InstallFilesV1(Self.Runtime,&Callbacks)==LUI_NATIVE_CAPABILITY_DENIED,"missing dialog grant admitted");
        Lui_Destroy(Self.Runtime); Self.Runtime=Lui_Create(); Require(Self.Runtime,"granted runtime");
        LuiCapabilityDeclarationV1 Caps{sizeof(Caps),1,LUI_CAPABILITY_DIALOGS}; Require(Lui_DeclareCapabilities(Self.Runtime,&Caps),"grant");
        Require(!Lui_InstallFilesV1(Self.Runtime,&Callbacks),"provider install");
        Require(Lui_InstallFilesV1(Self.Runtime,&Callbacks)==LUI_NATIVE_INVALID_ARGUMENT,"duplicate install");
        Domain=Lui_GetOwnerDomainV1(Self.Runtime); Require(Domain,"owner domain");
        Lui_SetLogCallback(Self.Runtime,&Self,Log);
        Run(Self,R"(
local Service=app:GetService('FileService')
local Empty=buffer.create(0)
Service:OpenFile(Empty,nil,function(Name,File,Failure)
    assert(not Failure and File and buffer.tostring(Name)=='input.txt')
    Service:Read(Empty,File,function(Bytes,_,ReadFailure)
        assert(not ReadFailure and buffer.tostring(Bytes)=='alpha')
        Service:Write(buffer.fromstring('denied'),File,function(_,_,WriteFailure)
            assert(WriteFailure=='CapabilityDenied'); File:Close(); print('[LUI:FilesTest] Finished')
        end)
    end)
end)
)"); Pump(Self);
        Run(Self,R"(
local S=app:GetService('FileService'); local E=buffer.create(0)
S:OpenFolder(E,nil,function(_,Folder,Failure)
    assert(not Failure and Folder)
    S:List(E,Folder,function(Bytes,_,ListFailure)
        assert(not ListFailure and string.find(buffer.tostring(Bytes),'input.txt\tFile\t5',1,true))
        S:Open(buffer.fromstring('input.txt'),Folder,function(_,Child,OpenFailure)
            assert(not OpenFailure and Child)
            S:Read(E,Child,function(Data,_,ReadFailure)
                assert(not ReadFailure and buffer.tostring(Data)=='alpha'); Child:Close(); Folder:Close(); print('[LUI:FilesTest] Finished')
            end)
        end)
    end)
end)
)"); Pump(Self);
        Run(Self,R"(
local S=app:GetService('FileService'); local E=buffer.create(0)
S:SaveFile(buffer.fromstring('output.txt'),nil,function(_,File,Failure)
    assert(not Failure and File)
    S:Write(buffer.fromstring('saved\0bytes'),File,function(_,_,WriteFailure)
        assert(not WriteFailure)
        S:Read(E,File,function(Bytes,_,ReadFailure)
            assert(not ReadFailure and buffer.tostring(Bytes)=='saved\0bytes'); File:Close(); print('[LUI:FilesTest] Finished')
        end)
    end)
end)
)"); Pump(Self);
        Self.Refusal=10;
        Run(Self,"app:GetService('FileService'):OpenFile(buffer.create(0),nil,function(B,H,E) assert(not B and not H and E=='ProviderFailure'); print('[LUI:FilesTest] Finished') end)"); Pump(Self);
        Self.Refusal=0; Self.Dismissed=true;
        Run(Self,"app:GetService('FileService'):OpenFile(buffer.create(0),nil,function(B,H,E) assert(not B and not H and E=='Canceled'); print('[LUI:FilesTest] Finished') end)"); Pump(Self);
        Self.Dismissed=false; Self.Pending=true;
        Run(Self,"app:GetService('FileService'):OpenFile(buffer.create(0),nil,function(B,H,E) assert(not B and not H and E=='Canceled'); print('[LUI:FilesTest] Finished') end)");
        Require(Lui_CompleteFilePickerV1(Self.Runtime,Self.Last,nullptr,10)==LUI_NATIVE_INVALID_ARGUMENT,"invalid terminal status accepted");
        Require(Lui_CompleteFilePickerV1(Self.Runtime,Self.Last,nullptr,0)==LUI_NATIVE_CANCELED,"invalid status lost pending picker"); Pump(Self);
        Run(Self,"local R=app:GetService('FileService'):OpenFile(buffer.create(0),nil,function(B,H,E) assert(not B and not H and E=='Canceled'); print('[LUI:FilesTest] Finished') end); assert(R:Cancel())"); Pump(Self);
        Require(Self.Canceled==Self.Last,"picker cancellation not forwarded");
        Require(Lui_CompleteFilePickerV1(Self.Runtime,Self.Last,(Self.Root/"input.txt").u8string().c_str(),0)==LUI_NATIVE_CLOSED,"late picker accepted");
        Run(Self,"app:GetService('FileService'):OpenFile(buffer.create(0),nil,function() error('RETIRED_CALLBACK') end)");
        Lui_Destroy(Self.Runtime); Self.Runtime=nullptr;
        const auto Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5); LuiNativeStatsV2 Stats{sizeof(Stats),2};
        do {Lui_PumpNativeDomainV2(Domain); Lui_GetNativeStatsV2(Domain,&Stats); if(!Stats.Sessions) break; std::this_thread::sleep_for(std::chrono::milliseconds(1));} while(std::chrono::steady_clock::now()<Deadline);
        Require(!Stats.Sessions && !Stats.Bytes && !Stats.Requests && !Stats.Handles && !Stats.Leases && !Stats.Events && !Stats.ReservedBytes,"provider retirement leaked");
        Lui_ReleaseNativeDomainV2(Domain); Domain=nullptr;
        // Installed host authority must not survive narrowing to a project's
        // smaller capability declaration.
        Self.Runtime=Lui_Create(); Require(Self.Runtime,"narrowed runtime");
        Require(Lui_DeclareCapabilities(Self.Runtime,&Caps) && !Lui_InstallFilesV1(Self.Runtime,&Callbacks),"narrowed setup");
        Fs::create_directories(Self.Root/"Project/Src");
        {std::ofstream File(Self.Root/"Project/Src/Main.luau"); File<<"assert(not pcall(function() app:GetService('FileService') end))";}
        {std::ofstream File(Self.Root/"Project/lui.json"); File<<R"({"SchemaVersion":2,"Script":"Src/Main.luau","Capabilities":[],"Extensions":[],"Assets":[],"ExtensionDeclarations":[],"Diagnostics":{"Mode":"Development"},"Modules":{"Root":"Src","Files":["Main.luau"],"Requires":[],"Dependencies":[]}})";}
        LuiProject* Project=nullptr; LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=1;
        char Error[256]{}; const auto Manifest=(Self.Root/"Project/lui.json").u8string();
        Require(Lui_ProjectPrepare(&Options,Manifest.data(),Manifest.size(),&Project,Error,sizeof(Error)),"narrowed project prepare");
        LuiProjectRunOptionsV1 RunOptions{sizeof(RunOptions),1,LUI_PROJECT_RUN_DEVELOPMENT_DIAGNOSTICS,0,0};
        const int Narrowed=Lui_RunProject(Self.Runtime,Project,&RunOptions); Lui_ProjectRelease(Project);
        Require(Narrowed,Lui_GetLastError(Self.Runtime));
        Domain=Lui_GetOwnerDomainV1(Self.Runtime); Lui_Destroy(Self.Runtime); Self.Runtime=nullptr;
        const auto FinalDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do {Lui_PumpNativeDomainV2(Domain); Lui_GetNativeStatsV2(Domain,&Stats); if(!Stats.Sessions) break; std::this_thread::sleep_for(std::chrono::milliseconds(1));} while(std::chrono::steady_clock::now()<FinalDeadline);
        Require(!Stats.Sessions && !Stats.Leases,"narrowed provider retirement"); Lui_ReleaseNativeDomainV2(Domain); Domain=nullptr;
        Fs::remove_all(Self.Root);
        std::puts("[LUI:Files] Owner callbacks, native IO, grants, cancellation and retirement passed"); return 0;
    } catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:Files] %s\n",Error.what()); if(Self.Runtime) Lui_Destroy(Self.Runtime); return 1;}
}
