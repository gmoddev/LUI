#include "Project.h"
#include "Files.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#else
#include <unistd.h>
#endif

namespace Fs = std::filesystem;
using Json = nlohmann::json;
using namespace Lui::Project;
static void Require(bool Value, const char* Label) { if (!Value) throw std::runtime_error(Label); }
static void Write(const Fs::path& Path, const std::string& Bytes) {
    Fs::create_directories(Path.parent_path());
    std::ofstream File(Path, std::ios::binary); File.write(Bytes.data(), Bytes.size());
    Require(bool(File), "fixture write failed");
}
static Json Manifest() {
    return {{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",Json::array()},
        {"Extensions",Json::array()},{"Assets",Json::array()}, {"ExtensionDeclarations",Json::array()},
        {"Diagnostics",{{"Mode","Development"}}},
        {"Modules",{{"Root","Src"},{"Files",{"State.luau","Screens/Home.luau","Main.luau"}},
            {"Requires",{"UiKit"}},{"Dependencies",Json::array({{{"Alias","UiKit"},{"Root","Vendor/UiKit"},
                {"Files",{"Button.luau"}},{"Requires",Json::array()}}})},{"Lock","Modules.lock.json"}}}};
}
static Json Lock() {
    return {{"SchemaVersion",1},{"Packages",Json::array({{{"Alias","UiKit"},
        {"Provenance",{{"Kind","LocalVendor"},{"Id","Fixture-1"}}},
        {"Files",Json::array({{{"Path","Button.luau"},{"Sha256",Hash("return false\n")}}})}}})}};
}
static void Files(const Fs::path& Root) {
    Write(Root/"Src/Main.luau", "local Home = require('./Screens/Home')\n");
    Write(Root/"Src/State.luau", "return {Count=0}\n");
    Write(Root/"Src/Screens/Home.luau", "return function() return require('../State') end\n");
    Write(Root/"Vendor/UiKit/Button.luau", "return false\n");
    Write(Root/"Modules.lock.json", Lock().dump());
    Write(Root/"lui.json", Manifest().dump());
}
static void Bad(const Fs::path& Root, const Json& Value, const char* Code, Options Settings = {}) {
    Write(Root/"lui.json", Value.dump()); std::string Error;
    auto Result = Prepare(Root/"lui.json", Settings, Error);
    Require(!Result && Error == std::string("[LUI:Project] ") + Code, Code);
    Require(Error.find(Root.u8string()) == std::string::npos, "error leaked physical path");
}
static void LinkDirectory(const Fs::path& Link, const Fs::path& Target) {
#ifdef _WIN32
    Fs::create_directory(Link);
    HANDLE File = CreateFileW(Link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    Require(File != INVALID_HANDLE_VALUE, "junction open failed");
    struct Buffer {
        DWORD Tag; WORD Length, Reserved;
        WORD SubstituteOffset, SubstituteLength, PrintOffset, PrintLength;
        wchar_t Names[4096];
    } Data{};
    auto Name = std::wstring(L"\\??\\") + Fs::absolute(Target).wstring();
    Require(Name.size() < 2000, "junction path too large");
    Data.Tag = IO_REPARSE_TAG_MOUNT_POINT;
    Data.SubstituteLength = static_cast<WORD>(Name.size()*2);
    Data.PrintOffset = static_cast<WORD>((Name.size()+1)*2);
    Data.Length = static_cast<WORD>(8 + Data.PrintOffset + 2);
    std::copy(Name.begin(), Name.end(), Data.Names);
    DWORD Returned = 0;
    BOOL Success = DeviceIoControl(File, FSCTL_SET_REPARSE_POINT, &Data, 8 + Data.Length, nullptr,0,&Returned,nullptr);
    CloseHandle(File); Require(Success != 0, "junction setup failed");
#else
    Fs::create_directory_symlink(Target, Link);
#endif
}
static void RemoveLink(const Fs::path& Link) {
#ifdef _WIN32
    Require(RemoveDirectoryW(Link.c_str()) != 0, "junction cleanup failed");
#else
    Fs::remove(Link);
#endif
}
static void Cases(const Fs::path& Suite) {
    auto Root=Suite/"First", Second=Suite/"Second";
    Files(Root); Files(Second);
    std::string Error;
    auto First=Prepare(Root/"lui.json",{},Error), Other=Prepare(Second/"lui.json",{},Error);
    Require(First && Other && First->GetDigest() == Other->GetDigest(), "physical root changed digest");
    Require(First->GetDigest()=="8c5b7aa6c0b1cefcc9a24d6e506e437e7d345ad315cb8675e5dfa752906af41a", "cross-platform digest golden changed");
    Require(First->GetEntry()=="app:Main.luau" && First->GetSources().size()==4, "identity/source ownership failed");
    auto State=First->Resolve("app:Screens/Home.luau","../State",Error);
    Require(State && State==First->Resolve("app:Screens/Home.luau","../State.luau",Error), "extension identity failed");
    Require(First->Resolve("app:Main.luau","@UiKit/Button",Error)->Id=="dep:UiKit/Button.luau", "alias identity failed");
    for (const auto& Request : {"../../Secret","./Sub/..","@UiKit/../Other","C:/Secret","//Server/Share","State","./CON","./State.lua","./%2e%2e/Secret","./State/"})
        Require(!First->Resolve("app:Main.luau",Request,Error) && Error=="[LUI:Project] InvalidPath", "unsafe request accepted");
    Require(!First->Resolve("dep:UiKit/Button.luau","@UiKit/Button",Error) && Error=="[LUI:Project] ForbiddenImport", "owner permissions bypassed");
    Require(!First->Resolve("app:Main.luau","./Main",Error) && Error=="[LUI:Project] EntryImportDenied", "entry import accepted");
    Require(!First->Resolve("app:Main.luau","./Screens",Error) && Error=="[LUI:Project] MissingModule", "directory fallback occurred");
    Require(!First->Resolve("app:Main.luau","./state",Error) && Error=="[LUI:Project] MissingModule", "case-insensitive lookup occurred");
    Require(!First->Resolve("app:Main.luau",std::string_view("./State\0junk",12),Error), "NUL accepted");
    auto Value=Manifest(); std::reverse(Value["Modules"]["Files"].begin(),Value["Modules"]["Files"].end());
    Write(Root/"lui.json",Value.dump(2)); auto Reordered=Prepare(Root/"lui.json",{},Error);
    Require(Reordered && Reordered->GetDigest()==First->GetDigest(), "ordering/format changed digest");
    Write(Root/"Src/State.luau","return {Count=1}\n"); auto Changed=Prepare(Root/"lui.json",{},Error);
    Require(Changed && Changed->GetDigest()!=First->GetDigest() && State->Bytes=="return {Count=0}\n", "snapshot not immutable");
    Files(Root);
    Value=Manifest(); Value["Unknown"]=true; Bad(Root,Value,"InvalidManifest");
    Value=Manifest(); Value["SchemaVersion"]=1; Bad(Root,Value,"InvalidSchema");
    Value=Manifest(); Value["Modules"]["Files"].push_back("state.luau"); Bad(Root,Value,"DuplicateIdentity");
    Value=Manifest(); Value["Modules"]["Root"]="src"; Value["Script"]="src/Main.luau"; Bad(Root,Value,"CaseMismatch");
    Value=Manifest(); Value["Modules"]["Files"][0]="NUL.luau"; Bad(Root,Value,"InvalidPath");
    Value=Manifest(); Value["Modules"]["Files"][0]="../Outside.luau"; Bad(Root,Value,"InvalidPath");
    Value=Manifest(); Value["Modules"]["Dependencies"][0]["Root"]="Src/Vendor"; Bad(Root,Value,"DuplicateIdentity");
    Value=Manifest(); Value["Modules"]["Requires"]={"Missing"}; Bad(Root,Value,"UnknownAlias");
    Value=Manifest(); Value["Modules"]["Dependencies"][0]["Requires"]={"UiKit"}; Bad(Root,Value,"Cycle");
    Value=Manifest(); Value["Capabilities"]={"Shell"}; Bad(Root,Value,"InvalidManifest");
    Value=Manifest(); Value["ExtensionDeclarations"]={"Unsafe.json"}; Bad(Root,Value,"InvalidManifest");
    Value=Manifest(); Value["Diagnostics"]["Mode"]="Bypass"; Bad(Root,Value,"InvalidManifest");
    Value=Manifest(); Value["Modules"].erase("Lock"); Bad(Root,Value,"InvalidLock");
    Value=Manifest(); Value["Script"]="Src/Missing.luau"; Bad(Root,Value,"InvalidManifest");
    Value=Manifest(); Value["Modules"]["Files"].push_back("screens/Other.luau"); Bad(Root,Value,"DuplicateIdentity");
    Value=Manifest(); Value["Modules"]["Files"].push_back("State.luau/Child.luau"); Bad(Root,Value,"DuplicateIdentity");
    Value=Manifest(); Value["Assets"]={"lui.json"}; Bad(Root,Value,"DuplicateIdentity");
    Options Small; Small.Bounds.Files=3; Bad(Root,Manifest(),"LimitExceeded",Small);
    Small={}; Small.Bounds.SourceBytes=4; Bad(Root,Manifest(),"LimitExceeded",Small);
    Small={}; Small.Bounds.FileBytes=3; Bad(Root,Manifest(),"LimitExceeded",Small);
    Small={}; Small.Bounds.ManifestBytes=3; Bad(Root,Manifest(),"LimitExceeded",Small);
    Small={}; Small.Bounds.LockBytes=3; Bad(Root,Manifest(),"LimitExceeded",Small);
    Small={}; Small.Bounds.Owners=1; Bad(Root,Manifest(),"LimitExceeded",Small);
    Small={}; Small.Canceled=[] {return true;}; Bad(Root,Manifest(),"Canceled",Small);
    Write(Root/"lui.json","{\"SchemaVersion\":2,\"SchemaVersion\":2}");
    Require(!Prepare(Root/"lui.json",{},Error) && Error=="[LUI:Project] InvalidJson", "duplicate key accepted");
    auto Duplicate=Manifest().dump(); auto Position=Duplicate.find("\"Root\":\"Src\"");
    Duplicate.replace(Position,12,"\"Root\":\"Src\",\"Root\":\"Src\""); Write(Root/"lui.json",Duplicate);
    Require(!Prepare(Root/"lui.json",{},Error) && Error=="[LUI:Project] InvalidJson", "nested duplicate accepted");
    Write(Root/"lui.json",Manifest().dump());
    Write(Root/"Src/State.luau",std::string("return\0secret",13));
    Require(!Prepare(Root/"lui.json",{},Error) && Error=="[LUI:Project] InvalidSource", "invalid source accepted");
    for (const auto& Invalid : {std::string("\xef\xbb\xbfreturn true"),std::string("\xc0\xaf"),std::string("\xed\xa0\x80")}) {
        Write(Root/"Src/State.luau",Invalid);
        Require(!Prepare(Root/"lui.json",{},Error) && Error=="[LUI:Project] InvalidSource", "invalid UTF-8/BOM accepted");
    }
    Files(Root); Write(Root/"Vendor/UiKit/Button.luau","return true\n");
    Require(!Prepare(Root/"lui.json",{},Error) && Error=="[LUI:Project] DependencyMismatch", "dirty dependency accepted");
    Files(Root); auto BrokenLock=Lock(); BrokenLock["Packages"][0]["Files"][0]["Path"]="Other.luau";
    Write(Root/"Modules.lock.json",BrokenLock.dump());
    Require(!Prepare(Root/"lui.json",{},Error) && Error=="[LUI:Project] InvalidLock", "unlisted dependency accepted");
    Files(Root); Fs::create_hard_link(Root/"Src/State.luau",Root/"Src/Copy.luau");
    Value=Manifest(); Value["Modules"]["Files"].push_back("Copy.luau"); Bad(Root,Value,"DuplicateIdentity");
    Fs::remove(Root/"Src/Copy.luau");
    auto Outside=Suite/"Outside"; Write(Outside/"Secret.luau","CANARY_OUTSIDE_ROOT");
    LinkDirectory(Root/"Src/Linked",Outside);
    Value=Manifest(); Value["Modules"]["Files"].push_back("Linked/Secret.luau"); Bad(Root,Value,"LinkedPath");
    RemoveLink(Root/"Src/Linked"); Files(Root);
    // Replace a directory after validation/opening. The reader must use its pinned object, not reopen a pathname.
    {
        Lui::Project::Internal::Anchor Reader(Root);
        bool Replaced=false;
        auto Pinned=Reader.Read("Src/State.luau",1024,[&] {
            std::error_code Code; Fs::rename(Root/"Src",Root/"OldSrc",Code);
            if (!Code) { Replaced=true; LinkDirectory(Root/"Src",Outside); }
            return false;
        });
        Require(Pinned.Bytes=="return {Count=0}\n", "read followed replacement into escaped target");
#ifdef _WIN32
        Require(!Replaced, "directory sharing guard failed");
#else
        Require(Replaced, "replacement race was not exercised");
#endif
        if (Replaced) { RemoveLink(Root/"Src"); Fs::rename(Root/"OldSrc",Root/"Src"); }
    }
    std::printf("[LUI:ProjectTest] Fixture digest %s\n",First->GetDigest().c_str());
}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    auto Suite=Fs::temp_directory_path()/ ("LuiProjectTests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fs::create_directory(Suite);
    try {
        Require(Hash("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 vector failed");
        Cases(Suite);
        // Suite is an explicitly created task fixture. Link targets are also inside it; links are removed before recursion.
        Fs::remove_all(Suite);
        std::puts("[LUI:ProjectTest] Ownership, resolution, locked inputs, immutable digests, limits and opened-file boundaries passed");
        return 0;
    } catch (const std::exception& Reason) {
        std::fprintf(stderr,"[LUI:ProjectTest] %s\n",Reason.what());
        return 1; // Preserve a failed fixture for diagnosis; never recursively delete an unexamined linked tree.
    }
}
