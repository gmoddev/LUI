#include "LiveStore.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Lui::Standard {
namespace {
constexpr sqlite3_int64 FileLimit=136LL*1024*1024;
#if defined(_WIN32)
using Handle=HANDLE;
const Handle Invalid=INVALID_HANDLE_VALUE;
void Close(Handle H) {if(H!=Invalid) CloseHandle(H);}
bool Position(Handle H,sqlite3_int64 Offset) {LARGE_INTEGER At{}; At.QuadPart=Offset; return SetFilePointerEx(H,At,nullptr,FILE_BEGIN)!=0;}
bool Regular(Handle H) {
    BY_HANDLE_FILE_INFORMATION Info{};
    return GetFileType(H)==FILE_TYPE_DISK && GetFileInformationByHandle(H,&Info) && Info.nNumberOfLinks==1 &&
        !(Info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
#else
using Handle=int;
constexpr Handle Invalid=-1;
void Close(Handle H) {if(H>=0) close(H);}
bool Regular(Handle H) {
    struct stat S{};
    return fstat(H,&S)==0 && S_ISREG(S.st_mode) && S.st_nlink==1 && S.st_uid==geteuid();
}
#endif
struct File {sqlite3_file Base; Handle H;
#if !defined(_WIN32)
    int JournalDirectory;
#endif
};
Handle Native(sqlite3_file* F) {return reinterpret_cast<File*>(F)->H;}
int End(sqlite3_file* F) {Close(Native(F)); F->pMethods=nullptr; return SQLITE_OK;}
int Read(sqlite3_file* F,void* Buffer,int Length,sqlite3_int64 Offset) {
    if(Length<0 || Offset<0) return SQLITE_IOERR_READ;
    auto* Bytes=static_cast<unsigned char*>(Buffer);
    int Done=0;
#if defined(_WIN32)
    if(!Position(Native(F),Offset)) return SQLITE_IOERR_SEEK;
#endif
    while(Done<Length) {
#if defined(_WIN32)
        DWORD Count=0;
        if(!ReadFile(Native(F),Bytes+Done,Length-Done,&Count,nullptr)) return SQLITE_IOERR_READ;
#else
        const auto Count=pread(Native(F),Bytes+Done,Length-Done,Offset+Done);
        if(Count<0 && errno==EINTR) continue;
        if(Count<0) return SQLITE_IOERR_READ;
#endif
        if(!Count) {std::memset(Bytes+Done,0,Length-Done); return SQLITE_IOERR_SHORT_READ;}
        Done+=static_cast<int>(Count);
    }
    return SQLITE_OK;
}
int Write(sqlite3_file* F,const void* Buffer,int Length,sqlite3_int64 Offset) {
    if(Length<0 || Offset<0 || Offset>FileLimit-Length || !Regular(Native(F))) return SQLITE_FULL;
    const auto* Bytes=static_cast<const unsigned char*>(Buffer);
    int Done=0;
#if defined(_WIN32)
    if(!Position(Native(F),Offset)) return SQLITE_IOERR_SEEK;
#endif
    while(Done<Length) {
#if defined(_WIN32)
        DWORD Count=0;
        if(!WriteFile(Native(F),Bytes+Done,Length-Done,&Count,nullptr)) return GetLastError()==ERROR_DISK_FULL?SQLITE_FULL:SQLITE_IOERR_WRITE;
#else
        const auto Count=pwrite(Native(F),Bytes+Done,Length-Done,Offset+Done);
        if(Count<0 && errno==EINTR) continue;
        if(Count<0) return errno==ENOSPC?SQLITE_FULL:SQLITE_IOERR_WRITE;
#endif
        if(!Count) return SQLITE_IOERR_WRITE;
        Done+=static_cast<int>(Count);
    }
    return SQLITE_OK;
}
int Truncate(sqlite3_file* F,sqlite3_int64 Size) {
    if(Size<0 || Size>FileLimit || !Regular(Native(F))) return SQLITE_FULL;
#if defined(_WIN32)
    return Position(Native(F),Size) && SetEndOfFile(Native(F))?SQLITE_OK:SQLITE_IOERR_TRUNCATE;
#else
    return ftruncate(Native(F),Size)==0?SQLITE_OK:SQLITE_IOERR_TRUNCATE;
#endif
}
int Sync(sqlite3_file* F,int) {
#if defined(_WIN32)
    return FlushFileBuffers(Native(F))?SQLITE_OK:SQLITE_IOERR_FSYNC;
#else
    if (fsync(Native(F))) return SQLITE_IOERR_FSYNC;
    auto& Directory=reinterpret_cast<File*>(F)->JournalDirectory;
    if (Directory>=0 && fsync(Directory)) return SQLITE_IOERR_DIR_FSYNC;
    Directory=-1;
    return SQLITE_OK;
#endif
}
int Length(sqlite3_file* F,sqlite3_int64* Size) {
#if defined(_WIN32)
    LARGE_INTEGER V{}; if(!GetFileSizeEx(Native(F),&V)) return SQLITE_IOERR_FSTAT; *Size=V.QuadPart;
#else
    struct stat V{}; if(fstat(Native(F),&V)) return SQLITE_IOERR_FSTAT; *Size=V.st_size;
#endif
    return *Size<=FileLimit?SQLITE_OK:SQLITE_FULL;
}
int Lock(sqlite3_file*,int) {return SQLITE_OK;} // The outer gate owns the entire connection lifetime.
int Reserved(sqlite3_file*,int* Value) {*Value=0;return SQLITE_OK;}
int Control(sqlite3_file*,int,void*) {return SQLITE_NOTFOUND;}
int Sector(sqlite3_file*) {return 4096;}
int Flags(sqlite3_file*) {return 0;}
const sqlite3_io_methods Methods={1,End,Read,Write,Truncate,Sync,Length,Lock,Lock,Reserved,Control,Sector,Flags};
const char* Leaf(const char* Name) {
    if(!Name) return nullptr;
    if(!std::strcmp(Name,"/lui.db") || !std::strcmp(Name,"lui.db")) return "lui.db";
    if(!std::strcmp(Name,"/lui.db-journal")) return "lui.db-journal";
    return nullptr;
}
}
struct LiveStore::State {
    sqlite3_vfs Vfs{};
    sqlite3_vfs* System=nullptr;
    std::string Name;
    Handle Root=Invalid, Gate=Invalid;
    bool Registered=false;
#if defined(_WIN32)
    std::filesystem::path Path;
    std::vector<Handle> Ancestors;
#endif
    ~State() {
        if(Registered) sqlite3_vfs_unregister(&Vfs);
        Close(Gate); Close(Root);
#if defined(_WIN32)
        for(Handle H:Ancestors) Close(H);
#endif
    }
    Handle Get(const char* Name,bool Create,bool Exclusive,bool Delete=false) const {
#if defined(_WIN32)
        const auto FilePath=Path/Name;
        Handle H=CreateFileW(FilePath.c_str(),GENERIC_READ|GENERIC_WRITE|(Delete?DELETE:0),
            FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr,Create?(Exclusive?CREATE_NEW:OPEN_ALWAYS):OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
#else
        (void)Delete;
        Handle H=openat(Root,Name,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK|(Create?O_CREAT:0)|(Exclusive?O_EXCL:0),0600);
#endif
        if(H!=Invalid && !Regular(H)) {Close(H);return Invalid;}
        return H;
    }
    static State& Owner(sqlite3_vfs* V) {return *static_cast<State*>(V->pAppData);}
    static int OpenFile(sqlite3_vfs* V,const char* Path,sqlite3_file* F,int Options,int* Actual) noexcept {
        F->pMethods=nullptr;
        const char* Name=Leaf(Path);
        if(!Name || !(Options&SQLITE_OPEN_READWRITE) ||
            (std::strcmp(Name,"lui.db")==0 ? !(Options&SQLITE_OPEN_MAIN_DB) : !(Options&SQLITE_OPEN_MAIN_JOURNAL))) return SQLITE_CANTOPEN;
        try {
            Handle H=Owner(V).Get(Name,(Options&SQLITE_OPEN_CREATE)!=0,(Options&SQLITE_OPEN_EXCLUSIVE)!=0);
            if(H==Invalid) return SQLITE_CANTOPEN;
            auto* Opened=reinterpret_cast<File*>(F);
            Opened->H=H;
#if !defined(_WIN32)
            Opened->JournalDirectory=(Options&SQLITE_OPEN_MAIN_JOURNAL)?Owner(V).Root:-1;
#endif
            F->pMethods=&Methods; if(Actual) *Actual=Options;
            return SQLITE_OK;
        } catch(...) {return SQLITE_NOMEM;}
    }
    static int Access(sqlite3_vfs* V,const char* Path,int,int* Present) noexcept {
        *Present=0; const char* Name=Leaf(Path);
        if(!Name) return SQLITE_OK;
        try {
#if defined(_WIN32)
            const DWORD A=GetFileAttributesW((Owner(V).Path/Name).c_str());
            if(A==INVALID_FILE_ATTRIBUTES) return GetLastError()==ERROR_FILE_NOT_FOUND?SQLITE_OK:SQLITE_IOERR_ACCESS;
            if(A&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) return SQLITE_IOERR_ACCESS;
#else
            struct stat S{};
            if(fstatat(Owner(V).Root,Name,&S,AT_SYMLINK_NOFOLLOW)) return errno==ENOENT?SQLITE_OK:SQLITE_IOERR_ACCESS;
            if(!S_ISREG(S.st_mode) || S.st_nlink!=1 || S.st_uid!=geteuid()) return SQLITE_IOERR_ACCESS;
#endif
            *Present=1;return SQLITE_OK;
        } catch(...) {return SQLITE_IOERR_ACCESS;}
    }
    static int Remove(sqlite3_vfs* V,const char* Path,int Flush) noexcept {
        const char* Name=Leaf(Path);
        if(!Name || std::strcmp(Name,"lui.db-journal")) return SQLITE_IOERR_DELETE;
        try {
#if defined(_WIN32)
            (void)Flush;
            Handle H=Owner(V).Get(Name,false,false,true); if(H==Invalid) return SQLITE_IOERR_DELETE;
            FILE_DISPOSITION_INFO D{TRUE}; bool Ok=SetFileInformationByHandle(H,FileDispositionInfo,&D,sizeof(D))!=0; Close(H);
            return Ok?SQLITE_OK:SQLITE_IOERR_DELETE;
#else
            auto& S=Owner(V);
            if(unlinkat(S.Root,Name,0)) return errno==ENOENT?SQLITE_IOERR_DELETE_NOENT:SQLITE_IOERR_DELETE;
            return !Flush || !fsync(S.Root)?SQLITE_OK:SQLITE_IOERR_DIR_FSYNC;
#endif
        } catch(...) {return SQLITE_IOERR_DELETE;}
    }
};
LiveStore::LiveStore(const char* Directory):Data(std::make_unique<State>()) {
    auto& S=*Data;
    auto P=std::filesystem::u8path(Directory);
    if (P.has_relative_path() && P.filename().empty()) P=P.parent_path();
    if(!P.is_absolute() || P!=P.lexically_normal()) throw std::runtime_error("InvalidRoot");
    for(const auto& Part:P.relative_path()) if(Part=="." || Part=="..") throw std::runtime_error("InvalidRoot");
#if defined(_WIN32)
    if(P.root_name().wstring().size()!=2 || P.root_name().wstring()[1]!=L':') throw std::runtime_error("InvalidRoot");
    auto Anchor=[&](const std::filesystem::path& Path) {
        Handle H=CreateFileW(Path.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|(Path==P?0:FILE_SHARE_WRITE),
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION I{};
        if(H==Invalid || !GetFileInformationByHandle(H,&I) || !(I.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (I.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
            Close(H);throw std::runtime_error("InvalidRoot");
        }
        return H;
    };
    S.Path=P.root_path(); S.Root=Anchor(S.Path);
    for(const auto& Part:P.relative_path()) {S.Path/=Part;S.Ancestors.push_back(S.Root);S.Root=Invalid;S.Root=Anchor(S.Path);}
#else
    S.Root=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    for(const auto& Part:P.relative_path()) {
        const int Next=openat(S.Root,Part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(Next<0) throw std::runtime_error("InvalidRoot");
        Close(S.Root);S.Root=Next;
    }
    struct stat Info{}; if(fstat(S.Root,&Info) || Info.st_uid!=geteuid() || (Info.st_mode&0077)) throw std::runtime_error("PrivateDirectoryRequired");
#endif
    S.Gate=S.Get("lui.lock",true,false);
    if(S.Gate==Invalid) throw std::runtime_error("InvalidGate");
    // The permanently held lock entry also keeps a Windows root nonempty while its directory handle forbids renames.
    static std::atomic<uint64_t> Sequence{0}; S.Name="LuiLive"+std::to_string(++Sequence);
    S.System=sqlite3_vfs_find(nullptr); if(!S.System) throw std::runtime_error("MissingVfs");
    auto& V=S.Vfs; V.iVersion=1; V.szOsFile=sizeof(File); V.mxPathname=96; V.zName=S.Name.c_str(); V.pAppData=&S;
    V.xOpen=State::OpenFile;V.xDelete=State::Remove;V.xAccess=State::Access;
    V.xFullPathname=[](sqlite3_vfs*,const char* N,int Capacity,char* Out) {
        if(!N || std::strcmp(N,"lui.db") || Capacity<8) return SQLITE_CANTOPEN;
        std::memcpy(Out,"/lui.db",8);return SQLITE_OK;
    };
    V.xRandomness=[](sqlite3_vfs* V,int N,char* Out){auto* T=State::Owner(V).System;return T->xRandomness(T,N,Out);};
    V.xSleep=[](sqlite3_vfs* V,int N){auto* T=State::Owner(V).System;return T->xSleep(T,N);};
    V.xCurrentTime=[](sqlite3_vfs* V,double* Out){auto* T=State::Owner(V).System;return T->xCurrentTime(T,Out);};
    if(sqlite3_vfs_register(&V,0)!=SQLITE_OK) throw std::runtime_error("Unavailable"); S.Registered=true;
}
LiveStore::~LiveStore()=default;
uint32_t LiveStore::Open(unsigned WaitMs,bool Create,const std::atomic<bool>& Canceled,sqlite3** Db) {
    const auto Deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(WaitMs);
    for(;;) {
        if(Canceled) return LUI_NATIVE_CANCELED;
#if defined(_WIN32)
        OVERLAPPED Region{};
        if(LockFileEx(Data->Gate,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&Region)) break;
        if(GetLastError()!=ERROR_LOCK_VIOLATION) return LUI_NATIVE_PROVIDER_FAILURE;
#else
        if(!flock(Data->Gate,LOCK_EX|LOCK_NB)) break;
        if(errno!=EWOULDBLOCK && errno!=EAGAIN && errno!=EINTR) return LUI_NATIVE_PROVIDER_FAILURE;
#endif
        if(std::chrono::steady_clock::now()>=Deadline) return LUI_NATIVE_WOULD_BLOCK;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const int Status=sqlite3_open_v2("lui.db",Db,SQLITE_OPEN_READWRITE|(Create?SQLITE_OPEN_CREATE:0)|SQLITE_OPEN_PRIVATECACHE|SQLITE_OPEN_NOMUTEX,Data->Name.c_str());
    if(Status==SQLITE_OK) return LUI_NATIVE_OK;
    if(*Db) {sqlite3_close(*Db);*Db=nullptr;} Unlock(); return LUI_NATIVE_PROVIDER_FAILURE;
}
void LiveStore::Unlock() noexcept {
#if defined(_WIN32)
    OVERLAPPED Region{};UnlockFileEx(Data->Gate,0,1,0,&Region);
#else
    flock(Data->Gate,LOCK_UN);
#endif
}
}
