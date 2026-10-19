#include "Package.h"
#include "Files.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#endif
namespace Fs = std::filesystem;
using Json = nlohmann::json;
using namespace Lui::Project;
static void Require(bool Value, const char* Label) { if (!Value) throw std::runtime_error(Label); }
static void Write(const Fs::path& Path, const std::string& Bytes) {
    Fs::create_directories(Path.parent_path()); std::ofstream File(Path, std::ios::binary);
    File.write(Bytes.data(), Bytes.size()); Require(bool(File), "fixture write failed");
}
static std::string Read(const Fs::path& Path) {
    std::ifstream File(Path, std::ios::binary); Require(bool(File), "fixture read failed");
    return {std::istreambuf_iterator<char>{File}, std::istreambuf_iterator<char>{}};
}
static void LinkDirectory(const Fs::path& Link, const Fs::path& Target) {
#ifdef _WIN32
    Fs::create_directory(Link);
    HANDLE File=CreateFileW(Link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    Require(File!=INVALID_HANDLE_VALUE,"junction open failed");
    struct Buffer { DWORD Tag; WORD Length,Reserved,SubstituteOffset,SubstituteLength,PrintOffset,PrintLength; wchar_t Names[4096]; } Data{};
    const auto Name=std::wstring(L"\\??\\")+Fs::absolute(Target).wstring();
    Require(Name.size()<2000,"junction name too large"); Data.Tag=IO_REPARSE_TAG_MOUNT_POINT;
    Data.SubstituteLength=static_cast<WORD>(Name.size()*2); Data.PrintOffset=static_cast<WORD>((Name.size()+1)*2);
    Data.Length=static_cast<WORD>(8+Data.PrintOffset+2); std::copy(Name.begin(),Name.end(),Data.Names);
    DWORD Returned=0; const auto Success=DeviceIoControl(File,FSCTL_SET_REPARSE_POINT,&Data,8+Data.Length,nullptr,0,&Returned,nullptr);
    CloseHandle(File); Require(Success!=0,"junction setup failed");
#else
    Fs::create_directory_symlink(Target,Link);
#endif
}
static void RemoveLink(const Fs::path& Link) {
#ifdef _WIN32
    Require(RemoveDirectoryW(Link.c_str())!=0,"junction removal failed");
#else
    Fs::remove(Link);
#endif
}
static void Fixture(const Fs::path& Root) {
    const std::string Vendor = "return false\n";
    Write(Root/"Src/Main.luau", "local Data=require('./Data'); assert(Data.Count==3); assert(require('@Kit/False')==false)\n");
    Write(Root/"Src/Data.luau", "return {Count=3}\n");
    Write(Root/"Src/Unused.luau", "return function() error('DO_NOT_EXECUTE') end\n");
    Write(Root/"Vendor/Kit/False.luau", Vendor);
    Write(Root/"Icon.bin", std::string("\0\xff\x01\r\n",5));
    Write(Root/"Empty.bin", "");
    Write(Root/"Ignored.txt", "UNDECLARED_CANARY");
    Write(Root/"Modules.lock.json", Json{{"SchemaVersion",1},{"Packages",Json::array({{{"Alias","Kit"},
        {"Provenance",{{"Kind","LocalVendor"},{"Id","Synthetic-1"}}},
        {"Files",Json::array({{{"Path","False.luau"},{"Sha256",Hash(Vendor)}}})}}})}}.dump(2));
    Write(Root/"app.manifest.json", Json{{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",Json::array()},
        {"Extensions",Json::array()},{"ExtensionDeclarations",Json::array()},{"Assets",{"Icon.bin","Empty.bin"}},
        {"Diagnostics",{{"Mode","Development"}}},{"Modules",{{"Root","Src"},{"Files",{"Main.luau","Unused.luau","Data.luau"}},
        {"Requires",{"Kit"}},{"Lock","Modules.lock.json"},{"Dependencies",Json::array({{{"Alias","Kit"},{"Root","Vendor/Kit"},
        {"Files",{"False.luau"}},{"Requires",Json::array()}}})}}}}.dump(2));
}
static void Cases(const Fs::path& Root) {
    const auto Input=Root/"Input", Other=Root/"Other", Output=Root/"Output";
    Fixture(Input); Fixture(Other); std::string Error;
    auto Package=CapturePackage(Input/"app.manifest.json",{},Error);
    Require(bool(Package), Error.c_str());
    Require(Package->GetDigest()=="069f0a45166b8e314bd2895d4013a90d4622680c8a4e17c7171305f9e0a27411", "portable package fixture digest changed");
    auto Manifest=Json::parse(Read(Other/"app.manifest.json"));
    Manifest["Modules"]["Files"]={"Data.luau","Unused.luau","Main.luau"}; Manifest["Assets"]={"Empty.bin","Icon.bin"};
    Write(Other/"app.manifest.json", Manifest.dump());
    auto Same=CapturePackage(Other/"app.manifest.json",{},Error);
    Require(Same && Package->GetIndex()==Same->GetIndex(), "root/list/format changed package index");
    auto Index=Json::parse(Package->GetIndex());
    Require(Index["SchemaVersion"]==1 && Index["Profile"]=="DeclaredContentV1" && Index["Sources"].size()==4 &&
        Index["Assets"].size()==2 && Index["Metadata"].size()==2 && Index["Dependencies"][0]["Provenance"]["Id"]=="Synthetic-1",
        "declared inputs/provenance missing");
    Require(Package->GetIndex().find(Input.u8string())==std::string::npos, "physical root entered index");
    // Staging is entirely captured: even deleted input metadata/assets/sources cannot change it.
    Fs::remove(Input/"app.manifest.json"); Fs::remove(Input/"Modules.lock.json");
    Fs::remove(Input/"Icon.bin"); Write(Input/"Src/Data.luau", "return 'MUTATED_INPUT'");
    Require(StagePackage(*Package,Output,{},Error), Error.c_str());
    Require(!Fs::exists(Output/"Ignored.txt") && !Fs::exists(Output/"app.manifest.json") && Fs::exists(Output/"Src/Unused.luau"),
        "undeclared copied or unused source omitted");
    Require(Read(Output/"Icon.bin")==std::string("\0\xff\x01\r\n",5) && Read(Output/"Empty.bin").empty(), "binary/empty assets changed");
    auto Verified=VerifyPackage(Output,{},Error);
    Require(Verified && Verified->GetDigest()==Package->GetDigest() &&
        Verified->GetPrepared().GetDigest()==Package->GetPrepared().GetDigest(), "staged preparation diverged");
    Require(!StagePackage(*Package,Output,{},Error) && Error=="[LUI:Project] OutputExists", "existing output overwritten");
    Write(Output/"Src/Data.luau", "return {Count=9}\n");
    Require(!VerifyPackage(Output,{},Error) && Error=="[LUI:Project] PackageMismatch", "edited source passed hash check");
    Require(Verified->GetPrepared().GetSources().GetSource("app:Data.luau")->Bytes=="return {Count=3}\n", "verified snapshot reread disk");
    Write(Output/"Src/Data.luau", Package->GetPrepared().GetSources().GetSource("app:Data.luau")->Bytes);
    Write(Output/"Icon.bin", "EDITED_ASSET"); Require(!VerifyPackage(Output,{},Error), "edited asset passed verification");
    Write(Output/"Icon.bin", Package->GetFiles().at("Icon.bin").Bytes);
    Write(Output/"Vendor/Kit/False.luau", "return true\n");
    Require(!VerifyPackage(Output,{},Error) && Error=="[LUI:Project] DependencyMismatch", "edited dependency passed lock check");
    Write(Output/"Vendor/Kit/False.luau", "return false\n");
    Write(Output/"lui.json", Read(Output/"lui.json")+"\n");
    Require(!VerifyPackage(Output,{},Error) && Error=="[LUI:Project] PackageMismatch", "metadata byte edit passed verification");
    Write(Output/"lui.json", Package->GetFiles().at("lui.json").Bytes);
    auto Wrong=Index; Wrong["SchemaVersion"]=2; Write(Output/"lui.project-index.json", Wrong.dump());
    Require(!VerifyPackage(Output,{},Error) && Error=="[LUI:Project] PackageMismatch", "index version accepted");
    Wrong=Index; Wrong["Sources"][0]["Path"]="../PRIVATE_ESCAPE"; Write(Output/"lui.project-index.json", Wrong.dump());
    Require(!VerifyPackage(Output,{},Error) && Error=="[LUI:Project] PackageMismatch" && Error.find("PRIVATE_ESCAPE")==std::string::npos,
        "untrusted index directed IO or leaked payload");
    Write(Output/"lui.project-index.json", Package->GetIndex());
    PackageOptions Bounds; Bounds.AssetBytes=4;
    Require(!CapturePackage(Other/"app.manifest.json",Bounds,Error) && Error=="[LUI:Project] LimitExceeded", "asset file bound ignored");
    Bounds={}; Bounds.TotalAssetBytes=4;
    Require(!CapturePackage(Other/"app.manifest.json",Bounds,Error), "asset aggregate bound ignored");
    Bounds={}; Bounds.IndexBytes=8; Require(!VerifyPackage(Output,Bounds,Error), "index bound ignored");
    Bounds={}; ++Bounds.AssetBytes; Require(!CapturePackage(Other/"app.manifest.json",Bounds,Error) && Error=="[LUI:Project] InvalidOptions", "raised asset bound accepted");
    Bounds={}; Bounds.Preparation.Bounds.RetainedBytes=Package->GetPrepared().GetRetainedBytes();
    Require(!CapturePackage(Other/"app.manifest.json",Bounds,Error), "combined retained package bound ignored");
    const auto Canceled=Root/"Canceled"; int Calls=0;
    Require(!StagePackage(*Package,Canceled,[&] { return ++Calls==3; },Error) && Error=="[LUI:Project] Canceled", "stage cancellation ignored");
    Require(!Fs::exists(Canceled/"lui.project-index.json") && !VerifyPackage(Canceled,{},Error), "partial package committed");
    Bounds={}; Bounds.Preparation.Canceled=[] {return true;};
    Require(!VerifyPackage(Output,Bounds,Error) && Error=="[LUI:Project] Canceled", "verification cancellation ignored");
    // Reserved index paths and file-versus-directory/case collisions fail before output creation.
    Manifest["Modules"]["Root"]="lui.project-index.json"; Manifest["Script"]="lui.project-index.json/Main.luau";
    for (const auto& File : Manifest["Modules"]["Files"]) Write(Other/"lui.project-index.json"/File.get<std::string>(), "return false");
    Write(Other/"app.manifest.json", Manifest.dump());
    Require(!CapturePackage(Other/"app.manifest.json",{},Error) && Error=="[LUI:Project] DuplicateIdentity", "reserved index directory accepted");
    Fs::rename(Output/"Src",Output/"SavedSrc"); LinkDirectory(Output/"Src",Output/"SavedSrc");
    Require(!VerifyPackage(Output,{},Error) && Error=="[LUI:Project] LinkedPath", "linked staged directory accepted");
    RemoveLink(Output/"Src"); Fs::rename(Output/"SavedSrc",Output/"Src");
    // An anchored writer cannot follow a link to an outside destination.
    const auto WriterRoot=Root/"Writer", Outside=Root/"Outside"; Fs::create_directory(WriterRoot); Fs::create_directory(Outside);
    LinkDirectory(WriterRoot/"Link",Outside);
    Internal::Anchor Writer(WriterRoot); bool Rejected=false;
    try { Writer.Write("Link/Escaped.bin","PRIVATE"); } catch(const Internal::Failure&) {Rejected=true;}
    Require(Rejected && !Fs::exists(Outside/"Escaped.bin"), "writer followed linked parent"); RemoveLink(WriterRoot/"Link");
    Writer.Write("Existing.bin","ORIGINAL"); Rejected=false;
    try {Writer.Write("Existing.bin","CHANGED");} catch(const Internal::Failure&) {Rejected=true;}
    Require(Rejected && Read(WriterRoot/"Existing.bin")=="ORIGINAL", "writer overwrote existing leaf");
    std::printf("[LUI:PackageTest] Fixture digest %s\n",Package->GetDigest().c_str());
}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    const auto Root=Fs::temp_directory_path()/("LuiPackageTests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fs::create_directory(Root);
    try {Cases(Root); Fs::remove_all(Root); std::puts("[LUI:PackageTest] Captured staging, deterministic index, verified inputs, collisions and bounds passed"); return 0;}
    catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:PackageTest] %s\n",Error.what()); return 1;}
}
