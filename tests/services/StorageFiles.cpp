#include "../../runtime/services/StorageFiles.h"
#include "../../runtime/project/Files.h"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#else
#include <sys/stat.h>
#endif

namespace Fs = std::filesystem;
using Lui::Native::StorageFiles;
namespace {
void Require(bool Value,const char* Message) { if (!Value) throw std::runtime_error(Message); }
template<class Action> void Rejected(Action&& Work,const char* Message) {
    bool Failed = false; try { Work(); } catch (const Lui::Project::Internal::Failure&) { Failed = true; }
    Require(Failed,Message);
}
void Sparse(const Fs::path& Path) {
#if defined(_WIN32)
    HANDLE File = CreateFileW(Path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Require(File != INVALID_HANDLE_VALUE,"sparse create"); DWORD Count = 0; LARGE_INTEGER Size{}; Size.QuadPart = 4*1024*1024;
    const bool Ok = DeviceIoControl(File,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&Count,nullptr) &&
        SetFilePointerEx(File,Size,nullptr,FILE_BEGIN) && SetEndOfFile(File);
    CloseHandle(File); Require(Ok,"NTFS sparse quota fixture");
#else
    { std::ofstream File(Path,std::ios::binary); Require(bool(File),"sparse create"); }
    Fs::resize_file(Path,4*1024*1024);
#endif
}
void DirectoryLink(const Fs::path& Link,const Fs::path& Target,bool Empty=true) {
#if defined(_WIN32)
    Fs::create_directory(Link);
    HANDLE Handle=CreateFileW(Link.c_str(),GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    Require(Handle!=INVALID_HANDLE_VALUE,"junction open");
    struct Buffer { DWORD Tag; WORD Length,Reserved,SubstituteOffset,SubstituteLength,PrintOffset,PrintLength; wchar_t Names[4096]; } Data{};
    const auto Name=std::wstring(L"\\??\\")+Fs::absolute(Target).wstring();
    Require(Name.size()<2000,"junction path bound");
    Data.Tag=IO_REPARSE_TAG_MOUNT_POINT; Data.SubstituteLength=static_cast<WORD>(Name.size()*2);
    Data.PrintOffset=static_cast<WORD>((Name.size()+1)*2); Data.Length=static_cast<WORD>(8+Data.PrintOffset+2);
    std::copy(Name.begin(),Name.end(),Data.Names); DWORD Returned=0;
    const auto Success=DeviceIoControl(Handle,FSCTL_SET_REPARSE_POINT,&Data,8+Data.Length,nullptr,0,&Returned,nullptr);
    const auto Error=GetLastError(); CloseHandle(Handle);
    Require(Empty ? Success!=0 : (!Success && Error==ERROR_DIR_NOT_EMPTY),"junction empty-directory control");
#else
    Fs::create_directory_symlink(Target,Link);
#endif
}
void Cases(const Fs::path& Root) {
    const auto ScopeA=std::string(64,'a'), ScopeB=std::string(64,'b');
    {
        StorageFiles Store(Root,ScopeA);
#if defined(_WIN32)
        DirectoryLink(Root/"LUI"/"Applications"/ScopeA,Root,false);
        std::error_code Locked;
        Require(!Fs::remove(Root/"LUI"/"Applications"/ScopeA/".lui-lock",Locked) && bool(Locked),"live lock deletion accepted");
#endif
        Store.Write("value","persisted",[] { return false; });
        Rejected([&] { StorageFiles Duplicate(Root,ScopeA); },"provisioned scope lock");
        StorageFiles Other(Root,ScopeB); std::string Result;
        Require(!Other.Read("value",Result),"provisioned scopes shared data");
        Rejected([&] { StorageFiles Invalid(Root,"../escape"); },"provisioning path accepted");
    }
    { StorageFiles Store(Root,ScopeA); std::string Result; Require(Store.Read("value",Result) && Result=="persisted","provisioned restart"); }
    const auto LinkedBase=Root/"LinkedBase"; Fs::create_directory(LinkedBase);
#if defined(_WIN32)
    const auto Link=LinkedBase/"LUI";
#else
    const auto Link=LinkedBase/".lui";
#endif
    DirectoryLink(Link,Root);
    Rejected([&] { StorageFiles Store(LinkedBase,ScopeA); },"linked provisioning parent admitted");
    Fs::remove(Link);
#if !defined(_WIN32)
    const auto Shared=Root/"Shared"; Fs::create_directory(Shared); Fs::create_directory(Shared/".lui");
    Require(chmod((Shared/".lui").c_str(),0755)==0,"permissions fixture");
    Rejected([&] { StorageFiles Store(Shared,ScopeA); },"nonprivate provisioning directory admitted");
#endif
    Fs::create_directory(Root/"Data");
    const auto Scope = Root/"Data";
    {
        StorageFiles Store(Scope); std::string Result;
        Store.Write("value","old",[] { return false; }); Require(Store.Read("value",Result) && Result == "old","initial data");
        Rejected([&] { StorageFiles Duplicate(Scope); },"scope lock did not exclude another owner");
        Store.Write("value",std::string_view("new\0bytes",9),[] { return false; });
        Require(Store.Read("value",Result) && Result == std::string("new\0bytes",9),"whole binary replacement");
        int Checks = 0;
        Rejected([&] { Store.Write("value",std::string(131072,'x'),[&] { return ++Checks == 3; }); },"mid-write cancellation");
        Require(Store.Read("value",Result) && Result == std::string("new\0bytes",9) && !Fs::exists(Scope/".lui-write"),"cancel preserved prior file and removed temporary");
        Rejected([&] { Store.Write("../escape","x",[] { return false; }); },"path key accepted");
        Rejected([&] { Store.Write("value",std::string(4*1024*1024+1,'x'),[] { return false; }); },"oversized file accepted");
        Store.Write("empty","",[] { return false; }); Require(Store.Read("empty",Result) && Result.empty(),"empty differs from missing");
        Store.Remove("value"); Store.Remove("value"); Require(!Store.Read("value",Result),"idempotent removal");
    }
    { StorageFiles Reopened(Scope); std::string Result; Require(Reopened.Read("empty",Result) && Result.empty(),"scope reopens after lock release"); }
    Fs::create_directory(Root/"Count");
    {
        StorageFiles Store(Root/"Count");
        for (int Index = 0; Index < 128; ++Index) Store.Write("key"+std::to_string(Index),"",[] { return false; });
        Rejected([&] { Store.Write("extra","",[] { return false; }); },"file count quota");
    }
    Fs::create_directory(Root/"Bytes");
    {
        StorageFiles Store(Root/"Bytes");
        for (int Index = 0; Index < 16; ++Index) Sparse(Root/"Bytes"/("K-key"+std::to_string(Index)+".bin"));
        Rejected([&] { Store.Write("extra","x",[] { return false; }); },"stored-byte quota");
    }
    Fs::create_directory(Root/"Links"); { std::ofstream File(Root/"External",std::ios::binary); File << "private"; }
    Fs::create_hard_link(Root/"External",Root/"Links/K-linked.bin");
    Rejected([&] { StorageFiles Store(Root/"Links"); },"hardlinked data admitted");
    Fs::remove(Root/"Links/K-linked.bin");
    std::error_code Code; Fs::create_symlink(Root/"External",Root/"Links/K-linked.bin",Code);
    if (!Code) Rejected([&] { StorageFiles Store(Root/"Links"); },"symlinked data admitted");
    Fs::create_directory(Root/"Unexpected"); { std::ofstream File(Root/"Unexpected/other"); File << "preserve"; }
    Rejected([&] { StorageFiles Store(Root/"Unexpected"); },"non-scope file admitted");
    Require(Fs::file_size(Root/"Unexpected/other") == 8,"configuration changed an unexpected file");
}
}
int main(int Count,char** Args) {
    const auto Root = Fs::temp_directory_path()/("LuiStorageFiles-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try { Fs::create_directory(Root);
#if !defined(_WIN32)
        if (Count == 2 && std::string(Args[1]) == "--fifo") {
            const auto Data = Root/"K-value.bin";
            Require(mkfifo(Data.c_str(),0600) == 0,"FIFO fixture");
            Rejected([&] { StorageFiles Store(Root); },"FIFO scan admitted");
            Fs::remove(Data);
            { StorageFiles Store(Root); std::string Output;
              Require(mkfifo(Data.c_str(),0600) == 0,"FIFO replacement fixture");
              Rejected([&] { Store.Read("value",Output); },"FIFO read admitted"); }
            Fs::remove_all(Root); std::puts("[LUI:StorageFilesTest] FIFO scan/read rejected without blocking"); return 0;
        }
#endif
        Cases(Root); Fs::remove_all(Root);
        std::puts("[LUI:StorageFilesTest] native scope, replacement, cancellation, locking, links and quotas passed"); return 0;
    } catch (const std::exception& Error) { std::fprintf(stderr,"[LUI:StorageFilesTest] %s\n",Error.what()); return 1; }
}
