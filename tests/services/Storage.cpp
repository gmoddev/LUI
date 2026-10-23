#include "LuiApplicationServices.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void Require(bool Value,const char* Message) { if (!Value) throw std::runtime_error(Message); }
LuiRuntime* Create(LuiNativeDomainV2* Domain,const std::filesystem::path& Root) {
    LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_RESTRICTED,0}; auto* Runtime = Lui_CreateWithPolicy(&Policy);
    Require(Runtime && Lui_SetNativeDomainV2(Runtime,Domain) == 0,"domain");
    LuiCapabilityDeclarationV1 Grants{sizeof(Grants),1,LUI_CAPABILITY_STORAGE}; Require(Lui_DeclareCapabilities(Runtime,&Grants),"storage grant");
    Require(Lui_InstallStorageV1(Runtime,Root.u8string().c_str()) == 0,"storage install without generic host grant"); return Runtime;
}
void Run(LuiRuntime* Runtime,const char* Source) { Require(Lui_RunScript(Runtime,Source,"StorageTest"),Lui_GetLastError(Runtime)); }
void Await(LuiRuntime* Runtime,LuiNativeDomainV2* Domain) {
    const auto Until = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < Until) {
        Lui_Pump(Runtime); LuiNativeStatsV2 Stats{sizeof(Stats),2}; Lui_GetNativeStatsV2(Domain,&Stats);
        if (!Stats.Requests) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(false,"storage worker deadline");
}
void Retire(LuiRuntime* Runtime,LuiNativeDomainV2* Domain) {
    Lui_Destroy(Runtime); const auto Until = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < Until) {
        Lui_PumpNativeDomainV2(Domain); LuiNativeStatsV2 Stats{sizeof(Stats),2}; Lui_GetNativeStatsV2(Domain,&Stats);
        if (!Stats.Sessions) { Require(!Stats.Bytes && !Stats.Handles && !Stats.Leases && !Stats.ReservedBytes,"storage leaked ownership"); return; }
        std::this_thread::yield();
    }
    Require(false,"storage retirement deadline");
}
}
int main() {
    const auto Root = std::filesystem::temp_directory_path()/("LuiStorage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directory(Root); auto* Domain = Lui_CreateNativeDomainV2(); auto* Runtime = Create(Domain,Root);
        Run(Runtime,
            "assert(app:GetService('PlatformService'):Supports('Storage'))\n"
            "local S=app:GetService('StorageService'); StorageDone=false\n"
            "S:Open(buffer.fromstring('preferences'),nil,function(_,Key,Error)\n"
            " assert(Key and not Error); local Input=buffer.fromstring('data\\000binary')\n"
            " S:Write(Input,Key,function(_,_,Failure) assert(not Failure); StorageDone=true end)\n"
            " buffer.writeu8(Input,0,122)\n"
            "end)\n");
        Await(Runtime,Domain); Run(Runtime,"assert(StorageDone)"); Retire(Runtime,Domain);
        Runtime = Create(Domain,Root);
        Run(Runtime,
            "local S=app:GetService('StorageService'); StorageDone=false; BadKey=false\n"
            "S:Open(buffer.fromstring('../invalid'),nil,function(_,Key,Error) assert(not Key and Error=='InvalidArgument'); BadKey=true end)\n"
            "S:Open(buffer.fromstring('preferences'),nil,function(_,Key,Error)\n"
            " assert(Key and not Error)\n"
            " S:Read(buffer.create(0),Key,function(Bytes,_,Failure)\n"
            "  assert(not Failure and Bytes and buffer.tostring(Bytes)=='data\\000binary')\n"
            "  S:Remove(buffer.create(0),Key,function(_,_,RemoveError)\n"
            "   assert(not RemoveError)\n"
            "   S:Read(buffer.create(0),Key,function(Missing,_,ReadError)\n"
            "    assert(not Missing and not ReadError and Key:Close() and Key:Close()); StorageDone=true\n"
            "    assert(not pcall(function() S:Read(buffer.create(0),Key,function() end) end))\n"
            "   end)\n"
            "  end)\n"
            " end)\n"
            "end)\n");
        Await(Runtime,Domain); Run(Runtime,"assert(StorageDone and BadKey)"); Retire(Runtime,Domain);
        Lui_ReleaseNativeDomainV2(Domain); std::filesystem::remove_all(Root);
        std::puts("[LUI:StorageTest] scoped bytes, handles, missing values and reload passed"); return 0;
    } catch (const std::exception& Error) { std::fprintf(stderr,"[LUI:StorageTest] %s\n",Error.what()); return 1; }
}
