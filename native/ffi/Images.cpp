#include "Core.h"
#include "../../third_party/picosha2/picosha2.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace Lui::Ffi {
namespace fs = std::filesystem;
namespace {
bool Identifier(const std::string& Name) {
    return !Name.empty() && Name.size()<=256 && Name.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_@.")==std::string::npos;
}
#ifdef _WIN32
struct File {
    HANDLE Handle;
    explicit File(HANDLE Value):Handle(Value){Require(Value!=INVALID_HANDLE_VALUE,"LibraryUnavailable");}
    ~File(){CloseHandle(Handle);}
};
using ImageHandle=HMODULE;
fs::path Program() {
    std::wstring Name(32768,L'\0'); auto N=GetModuleFileNameW(nullptr,Name.data(),static_cast<DWORD>(Name.size()));
    Require(N && N<Name.size(),"ExecutablePathUnavailable"); Name.resize(N); return Name;
}
fs::path System(const std::string& Alias) {
    wchar_t Folder[MAX_PATH+1]; const auto N=GetSystemDirectoryW(Folder,MAX_PATH+1);
    Require(N && N<=MAX_PATH,"SystemLibraryUnavailable");
    static const std::map<std::string,std::string> Names{{"process","kernel32.dll"},{"math","ucrtbase.dll"},{"sqlite","winsqlite3.dll"}};
    auto It=Names.find(Alias); Require(It!=Names.end(),"UnknownSystemLibrary"); return fs::path(Folder)/It->second;
}
#else
using ImageHandle=void*;
struct File {
    int Handle;
    explicit File(int Value):Handle(Value){Require(Value>=0,"LibraryUnavailable");}
    ~File(){if(Handle>=0)close(Handle);}
};
fs::path Program(){return fs::read_symlink("/proc/self/exe");}
fs::path System(const std::string& Alias) {
    static const std::map<std::string,std::string> Names{{"process","libc.so.6"},{"math","libm.so.6"},{"sqlite","libsqlite3.so.0"}};
    auto It=Names.find(Alias); Require(It!=Names.end(),"UnknownSystemLibrary");
    for(const char* Dir:{"/usr/lib/x86_64-linux-gnu","/lib/x86_64-linux-gnu","/usr/lib64","/lib64"}) {
        auto Path=fs::path(Dir)/It->second; if(fs::is_regular_file(Path))return Path;
    }
    throw Failure("SystemLibraryUnavailable");
}
#endif
struct Loaded final:Image {
    ImageHandle Handle{};
    std::shared_ptr<File> Captured;
    void* Symbol(const std::string& Name) const override {
        Require(Identifier(Name),"InvalidSymbol");
#ifdef _WIN32
        auto P=reinterpret_cast<void*>(GetProcAddress(Handle,Name.c_str()));
#else
        auto P=dlsym(Handle,Name.c_str());
#endif
        Require(P!=nullptr,"SymbolUnavailable"); return P;
    }
};
}
std::shared_ptr<Image> OpenImage(const Json& Req) {
    const auto Kind=Req.at("Kind").get<std::string>(), Name=Req.at("Name").get<std::string>();
    Require(Name.size()<=4096 && Name.find('\0')==std::string::npos,"InvalidPath");
    const std::string Hash=Req.value("Sha256",std::string{});
    Require(Hash.empty() || (Hash.size()==64 && Hash.find_first_not_of("0123456789abcdef")==Hash.npos),"InvalidDigest");
    fs::path Path;
    if(Kind=="System") Path=System(Name);
    else if(Kind=="Selected") {Path=fs::u8path(Name);Require(Path.is_absolute(),"AbsolutePathRequired");}
    else if(Kind=="Packaged"){
        Require(Identifier(Name) && Name!="." && Name!=".." && !Hash.empty(),"InvalidPackagedLibrary");
        auto Home=Program().parent_path();
#ifndef _WIN32
        if(Home.filename()=="bin") Home=Home.parent_path();
#endif
        // Schema-v2 assets are captured as single filenames at the application root.
        Path=Home/"Application"/Name;
    } else throw Failure("InvalidSelection");
    std::error_code PathError;
    Path=fs::canonical(Path,PathError);
    Require(!PathError,"LibraryUnavailable");
    const auto Key=Path.u8string()+"\n"+Hash;
    static std::mutex Mutex;
    static std::map<std::string,std::shared_ptr<Loaded>> Images;
    std::lock_guard<std::mutex> Lock(Mutex);
    if(auto It=Images.find(Key);It!=Images.end())return It->second;
    Require(Images.size()<64,"ImageLimit");
    auto Out=std::make_shared<Loaded>();
#ifdef _WIN32
    std::vector<std::shared_ptr<File>> Parents;
    if(!Hash.empty()){
        fs::path Dir=Path.root_path();
        for(const auto& Part:Path.parent_path().relative_path()){
            Dir/=Part;
            auto Guard=std::make_shared<File>(CreateFileW(Dir.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            FILE_ATTRIBUTE_TAG_INFO Attr{};
            Require(GetFileInformationByHandleEx(Guard->Handle,FileAttributeTagInfo,&Attr,sizeof(Attr)) &&
                !(Attr.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"UnstableLibraryPath");
            Parents.push_back(std::move(Guard));
        }
        auto Input=std::make_shared<File>(CreateFileW(Path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        BY_HANDLE_FILE_INFORMATION Info{}; Require(GetFileInformationByHandle(Input->Handle,&Info) && !(Info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
            && !Info.nFileSizeHigh && Info.nFileSizeLow>0 && Info.nFileSizeLow<=64*1024*1024,"InvalidImage");
        std::vector<unsigned char> Bytes(Info.nFileSizeLow);DWORD Got=0;
        Require(ReadFile(Input->Handle,Bytes.data(),Info.nFileSizeLow,&Got,nullptr) && Got==Bytes.size() &&
            picosha2::hash256_hex_string(Bytes.begin(),Bytes.end())==Hash,"DigestMismatch");
        Out->Captured=std::move(Input);
    }
    Out->Handle=LoadLibraryExW(Path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    if(!Hash.empty()){
        std::ifstream Input(Path,std::ios::binary|std::ios::ate); const std::streamoff Size=Input.tellg();
        Require(Size>0 && Size<=64*1024*1024,"InvalidImage");
        std::vector<char> Bytes(static_cast<size_t>(Size));Input.seekg(0);Input.read(Bytes.data(),Size);
        Require(Input && picosha2::hash256_hex_string(Bytes.begin(),Bytes.end())==Hash,"DigestMismatch");
        auto F=std::make_shared<File>(static_cast<int>(syscall(SYS_memfd_create,"lui-ffi",MFD_CLOEXEC|MFD_ALLOW_SEALING)));
        size_t At=0;
        while(At<Bytes.size()){
            const auto Written=write(F->Handle,Bytes.data()+At,Bytes.size()-At);
            if(Written<0 && errno==EINTR)continue;
            Require(Written>0,"CaptureFailed"); At+=static_cast<size_t>(Written);
        }
        Require(fcntl(F->Handle,F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)==0,"CaptureFailed");
        Path=fs::path("/proc/self/fd")/std::to_string(F->Handle); Out->Captured=std::move(F);
    }
    Out->Handle=dlopen(Path.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
    Require(Out->Handle!=nullptr,"LoadFailed");
    Images.emplace(Key,Out);return Out;
}
void PinCallbackModule(){
    static const bool Pinned=[]{
#ifdef _WIN32
        HMODULE Module{};
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&PinCallbackModule),&Module)!=0;
#else
        Dl_info Info{};
        if(dladdr(reinterpret_cast<void*>(&PinCallbackModule),&Info)==0)return false;
        return dlopen(Info.dli_fname,RTLD_NOW|RTLD_NOLOAD|RTLD_NODELETE)!=nullptr;
#endif
    }();
    Require(Pinned,"CallbackImageUnavailable");
}
}
