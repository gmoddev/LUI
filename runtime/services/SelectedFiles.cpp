#include "SelectedFiles.h"
#include "../project/Files.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <optional>
#include <utility>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
#include <unistd.h>
#endif

namespace Lui::Native {
using Lui::Project::Internal::Fail;
constexpr size_t Maximum = 4*1024*1024;
bool SelectedName(std::string_view Name) {
    if (Name.empty() || Name.size()>255 || Name=="." || Name==".." || Name.back()=='.' || Name.back()==' ' ||
        !Lui::Project::Internal::Utf8(Name)) return false;
    for (unsigned char Byte:Name) if (Byte<32 || Byte==127 || std::string_view("/\\:*?\"<>|").find(char(Byte))!=std::string_view::npos) return false;
    auto Stem=std::string(Name.substr(0,Name.find('.')));
    std::transform(Stem.begin(),Stem.end(),Stem.begin(),[](unsigned char Byte){return Byte>='a' && Byte<='z'?char(Byte-32):char(Byte);});
    if (Stem=="CON" || Stem=="PRN" || Stem=="AUX" || Stem=="NUL" ||
        (Stem.size()==4 && (Stem.substr(0,3)=="COM" || Stem.substr(0,3)=="LPT") && Stem[3]>='0' && Stem[3]<='9')) return false;
    constexpr std::string_view Prefix=".lui-selected-";
    return Name.size()<Prefix.size() || !std::equal(Prefix.begin(),Prefix.end(),Name.begin(),[](char A,char B){return A==(B>='A' && B<='Z'?char(B+32):B);});
}
namespace {
struct NonModal {
#if defined(_WIN32)
    DWORD Previous=0;
    NonModal() {if(!SetThreadErrorMode(GetThreadErrorMode()|0x8003,&Previous)) Fail("ProviderFailure");}
    ~NonModal() {SetThreadErrorMode(Previous,nullptr);}
#endif
};
void CheckCancel(const std::function<bool()>& Canceled) {if(Canceled && Canceled()) Fail("Canceled");}
std::string Temporary() {
    static std::atomic<uint64_t> Next{0};
    return ".lui-selected-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++Next);
}
#if defined(_WIN32)
struct File {
    HANDLE Value;
    explicit File(HANDLE Value):Value(Value) {if(Value==INVALID_HANDLE_VALUE) Fail("ProviderFailure");}
    ~File() {Close();}
    void Close() {if(Value!=INVALID_HANDLE_VALUE){CloseHandle(Value); Value=INVALID_HANDLE_VALUE;}}
};
BY_HANDLE_FILE_INFORMATION Info(HANDLE Value,bool Directory) {
    BY_HANDLE_FILE_INFORMATION Result{};
    if(!GetFileInformationByHandle(Value,&Result) || GetFileType(Value)!=FILE_TYPE_DISK ||
        (Result.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || bool(Result.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=Directory ||
        (!Directory && Result.nNumberOfLinks!=1)) Fail("ProviderFailure");
    return Result;
}
uint64_t Size(const BY_HANDLE_FILE_INFORMATION& Info) {return (uint64_t(Info.nFileSizeHigh)<<32)|Info.nFileSizeLow;}
std::wstring Final(HANDLE Value) {
    DWORD Count=GetFinalPathNameByHandleW(Value,nullptr,0,FILE_NAME_NORMALIZED);
    if(!Count || Count>32768) Fail("ProviderFailure");
    std::wstring Result(Count,L'\0'); const auto Length=GetFinalPathNameByHandleW(Value,Result.data(),Count,FILE_NAME_NORMALIZED);
    if(!Length || Length>=Count) Fail("ProviderFailure"); Result.resize(Length); return Result;
}
std::wstring Wide(const std::string& Name) {return std::filesystem::u8path(Name).wstring();}
std::string Revision(const BY_HANDLE_FILE_INFORMATION& Value) {
    return std::to_string(Value.dwVolumeSerialNumber)+":"+std::to_string(Value.nFileIndexHigh)+":"+std::to_string(Value.nFileIndexLow)+":"+
        std::to_string(Size(Value))+":"+std::to_string(Value.ftLastWriteTime.dwHighDateTime)+":"+std::to_string(Value.ftLastWriteTime.dwLowDateTime);
}
struct Directory {
    std::vector<std::shared_ptr<File>> Held;
    std::shared_ptr<File> Guard;
    std::wstring GuardPath;
    static std::shared_ptr<File> Anchor(const std::wstring& Name,DWORD Sharing=FILE_SHARE_READ) {
            auto Result=std::make_shared<File>(CreateFileW(Name.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,Sharing,nullptr,
                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)); Info(Result->Value,true); return Result;
    }
    static std::shared_ptr<File> WritableAnchor(const std::shared_ptr<File>& Previous) {
        const auto Before=Info(Previous->Value,true);
        auto Result=Anchor(Final(Previous->Value),FILE_SHARE_READ|FILE_SHARE_WRITE);
        const auto After=Info(Result->Value,true);
        if(Before.dwVolumeSerialNumber!=After.dwVolumeSerialNumber || Before.nFileIndexHigh!=After.nFileIndexHigh || Before.nFileIndexLow!=After.nFileIndexLow) Fail("ProviderFailure");
        return Result;
    }
    explicit Directory(const std::filesystem::path& Path,bool Writable) {
        if(!Path.is_absolute() || Path.root_name().wstring().size()!=2 || Path.root_name().wstring()[1]!=L':') Fail("InvalidArgument");
        Held.push_back(Anchor(Path.root_path().wstring()));
        for(const auto& Part:Path.relative_path()) {
            const auto Name=Part.u8string(); if(!SelectedName(Name)) Fail("InvalidArgument");
            Held.push_back(Anchor(Final(Held.back()->Value)+L"\\"+Part.wstring()));
            if(Held.size()>128) Fail("LimitExceeded");
        }
        // Every ancestor is nonempty because its next child is held without
        // delete sharing. Permit writes there without permitting reparse/rename
        // substitution; a descendant Folder must not block an ancestor Save as.
        for(size_t Index=0;Index+1<Held.size();++Index) Held[Index]=WritableAnchor(Held[Index]);
        if(Writable) EnableWrite();
    }
    void EnableWrite() {
        if(!Guard) {
            // A held, nondelete-shared entry prevents reparse conversion while
            // write sharing is enabled for atomic rename in the selected parent.
            GuardPath=Final(Held.back()->Value)+L"\\"+Wide(Temporary());
            auto NewGuard=std::make_shared<File>(CreateFileW(GuardPath.c_str(),GENERIC_WRITE|DELETE|FILE_READ_ATTRIBUTES,0,nullptr,CREATE_NEW,
                FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)); Info(NewGuard->Value,false);
            auto WritableRoot=WritableAnchor(Held.back());
            Held.back()=std::move(WritableRoot);
            Guard=std::move(NewGuard);
        }
    }
    std::string Identity() const {
        const auto Value=Info(Held.back()->Value,true);
        return std::to_string(Value.dwVolumeSerialNumber)+":"+std::to_string(Value.nFileIndexHigh)+":"+std::to_string(Value.nFileIndexLow);
    }
    std::wstring Path(const std::string& Name) const {Info(Held.back()->Value,true); return Final(Held.back()->Value)+L"\\"+Wide(Name);}
    std::shared_ptr<File> Open(const std::string& Name) const {
        auto Result=std::make_shared<File>(CreateFileW(Path(Name).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr)); Info(Result->Value,false); return Result;
    }
    std::string Current(const std::string& Name) const {
        const auto Attributes=GetFileAttributesW(Path(Name).c_str());
        if(Attributes==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_FILE_NOT_FOUND) return {};
        return Revision(Info(Open(Name)->Value,false));
    }
};
#else
struct File {
    int Value;
    explicit File(int Value):Value(Value) {if(Value<0) Fail("ProviderFailure");}
    ~File() {close(Value);}
};
struct stat Info(int Value,bool Directory) {
    struct stat Result{};
    if(fstat(Value,&Result) || (Directory ? !S_ISDIR(Result.st_mode):(!S_ISREG(Result.st_mode) || Result.st_nlink!=1))) Fail("ProviderFailure");
    return Result;
}
uint64_t Size(const struct stat& Value) {if(Value.st_size<0) Fail("ProviderFailure"); return uint64_t(Value.st_size);}
std::string Revision(const struct stat& Value) {
    return std::to_string(Value.st_dev)+":"+std::to_string(Value.st_ino)+":"+std::to_string(Size(Value))+":"+
        std::to_string(Value.st_mtim.tv_sec)+":"+std::to_string(Value.st_mtim.tv_nsec)+":"+
        std::to_string(Value.st_ctim.tv_sec)+":"+std::to_string(Value.st_ctim.tv_nsec);
}
struct Directory {
    std::shared_ptr<File> Root;
    explicit Directory(const std::filesystem::path& Path,bool) {
        if(!Path.is_absolute()) Fail("InvalidArgument");
        Root=std::make_shared<File>(open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC)); size_t Count=0;
        for(const auto& Part:Path.relative_path()) {
            const auto Name=Part.u8string(); if(!SelectedName(Name)) Fail("InvalidArgument");
            Root=std::make_shared<File>(openat(Root->Value,Name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)); Info(Root->Value,true);
            if(++Count>128) Fail("LimitExceeded");
        }
    }
    void EnableWrite() {}
    std::string Identity() const {const auto Value=Info(Root->Value,true); return std::to_string(Value.st_dev)+":"+std::to_string(Value.st_ino);}
    std::shared_ptr<File> Open(const std::string& Name) const {
        auto Result=std::make_shared<File>(openat(Root->Value,Name.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC)); Info(Result->Value,false); return Result;
    }
    std::string Current(const std::string& Name) const {
        struct stat Value{};
        if(fstatat(Root->Value,Name.c_str(),&Value,AT_SYMLINK_NOFOLLOW) && errno==ENOENT) return {};
        return Revision(Info(Open(Name)->Value,false));
    }
};
#endif
}
struct SelectionScope::State {std::map<std::string,std::weak_ptr<Directory>> Roots;};
SelectionScope::SelectionScope():Data(std::make_unique<State>()) {}
SelectionScope::~SelectionScope()=default;
struct SelectedFile::State {
    std::shared_ptr<Directory> Parent;
    std::shared_ptr<File> Input;
    std::string Name,Expected;
    bool Folder=false,Writable=false,Readable=true;
};
SelectedFile::SelectedFile(const std::filesystem::path& Path,bool Folder,bool Writable,const std::shared_ptr<SelectionScope>& Scope):Data(std::make_unique<State>()) {
    NonModal Guard;
    if(!Path.is_absolute() || Path.u8string().size()>32768) Fail("InvalidArgument");
    Data->Folder=Folder; Data->Writable=Writable;
    Data->Name=Path.filename().u8string(); if(!SelectedName(Data->Name)) Fail("InvalidArgument");
    Data->Parent=std::make_shared<Directory>(Folder?Path:Path.parent_path(),false);
    if(Scope) {
        auto& Roots=Scope->Data->Roots;
        for(auto It=Roots.begin();It!=Roots.end();) {if(It->second.expired()) It=Roots.erase(It); else ++It;}
        const auto Key=Data->Parent->Identity(); const auto Found=Roots.find(Key);
        if(Found!=Roots.end()) {
            // Drop the duplicate restrictive anchor before upgrading the shared
            // root. A live read-only Folder must not block this session's Save as.
            if(auto Existing=Found->second.lock()) Data->Parent=std::move(Existing);
        } else if(Roots.size()>=32) Fail("LimitExceeded");
        Roots[Key]=Data->Parent;
    }
    if(Writable) Data->Parent->EnableWrite();
    if(!Folder) {
        if(Writable) Data->Expected=Data->Parent->Current(Data->Name);
        else Data->Input=Data->Parent->Open(Data->Name);
    }
}
SelectedFile::SelectedFile(std::unique_ptr<State> Data):Data(std::move(Data)) {}
SelectedFile::~SelectedFile()=default;
std::string SelectedFile::Name() const {return Data->Name;}
void SelectedFile::RestrictToWriting() {Data->Readable=false;}
std::string SelectedFile::Read(const std::function<bool()>& Canceled) {
    return ReadSpan(0,Maximum,true,Canceled);
}
std::string SelectedFile::ReadRange(uint64_t Position,uint32_t Count,const std::function<bool()>& Canceled) {
    if(Position>9007199254740991ULL || !Count || Count>65536) Fail("InvalidArgument");
    return ReadSpan(Position,Count,false,Canceled);
}
uint64_t SelectedFile::ByteLength() const {
    NonModal Guard;
    if(!Data->Readable) Fail("CapabilityDenied");
    if(Data->Folder) Fail("InvalidArgument");
    auto Input=Data->Input?Data->Input:Data->Parent->Open(Data->Name);
    return Size(Info(Input->Value,false));
}
std::string SelectedFile::ReadSpan(uint64_t Position,uint32_t Count,bool Whole,const std::function<bool()>& Canceled) {
    if(!Data->Readable) Fail("CapabilityDenied");
    NonModal Guard; if(Data->Folder) Fail("InvalidArgument"); CheckCancel(Canceled);
    auto Input=Data->Input?Data->Input:Data->Parent->Open(Data->Name);
    const auto Before=Info(Input->Value,false); const auto Length=Size(Before);
    if(Whole && Length>Maximum) Fail("LimitExceeded");
    const auto Available=Length-std::min(Length,Position);
    std::string Result(static_cast<size_t>(std::min<uint64_t>(Available,Count)),'\0'); size_t Offset=0;
#if defined(_WIN32)
    LARGE_INTEGER Start{}; Start.QuadPart=static_cast<LONGLONG>(Position);
    if(!Result.empty() && !SetFilePointerEx(Input->Value,Start,nullptr,FILE_BEGIN)) Fail("ProviderFailure");
#endif
    while(Offset<Result.size()) {
        CheckCancel(Canceled);
#if defined(_WIN32)
        DWORD Count=0; if(!ReadFile(Input->Value,Result.data()+Offset,static_cast<DWORD>(std::min<size_t>(65536,Result.size()-Offset)),&Count,nullptr) || !Count) Fail("ProviderFailure");
#else
        const auto Count=pread(Input->Value,Result.data()+Offset,std::min<size_t>(65536,Result.size()-Offset),static_cast<off_t>(Position+Offset));
        if(Count<0 && errno==EINTR) continue;
        if(Count<=0) Fail("ProviderFailure");
#endif
        Offset+=static_cast<size_t>(Count);
    }
    CheckCancel(Canceled);
    if(Revision(Before)!=Revision(Info(Input->Value,false))) Fail("ProviderFailure");
    if(Data->Writable && Whole) Data->Expected=Revision(Before);
    return Result;
}
struct SelectedWriter::State {
    std::shared_ptr<SelectedFile::State> Destination;
    std::optional<File> Output;
    std::string Expected, TemporaryName;
    bool Finished=false, Named=false;
#if !defined(_WIN32)
    bool SameEntry() const noexcept {
        struct stat Entry{}, Held{};
        return Output && !fstat(Output->Value,&Held) &&
            !fstatat(Destination->Parent->Root->Value,TemporaryName.c_str(),&Entry,AT_SYMLINK_NOFOLLOW) &&
            Entry.st_dev==Held.st_dev && Entry.st_ino==Held.st_ino;
    }
#endif
    ~State() {
        if (!Named || !Output) return;
#if defined(_WIN32)
        FILE_DISPOSITION_INFO Remove{TRUE};
        SetFileInformationByHandle(Output->Value,FileDispositionInfo,&Remove,sizeof(Remove));
#else
        if (SameEntry()) unlinkat(Destination->Parent->Root->Value,TemporaryName.c_str(),0);
#endif
    }
};
SelectedWriter::SelectedWriter(std::unique_ptr<State> Value):Data(std::move(Value)) {}
SelectedWriter::~SelectedWriter()=default;
std::shared_ptr<SelectedWriter> SelectedFile::BeginWriter() {
    NonModal Guard;
    if (!Data->Writable || Data->Folder) Fail("CapabilityDenied");
    auto Draft=std::make_unique<SelectedWriter::State>();
    Draft->Destination=Data;
    Draft->Expected=Data->Expected;
    if (Data->Parent->Current(Data->Name)!=Draft->Expected) Fail("ProviderFailure");
    Draft->TemporaryName=Temporary();
#if defined(_WIN32)
    Draft->Output.emplace(CreateFileW(Data->Parent->Path(Draft->TemporaryName).c_str(),
        GENERIC_WRITE|DELETE|FILE_READ_ATTRIBUTES,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    Draft->Named=true;
    Info(Draft->Output->Value,false);
    FILE_DISPOSITION_INFO Remove{TRUE};
    if (!SetFileInformationByHandle(Draft->Output->Value,FileDispositionInfo,&Remove,sizeof(Remove))) Fail("ProviderFailure");
#else
    const int Root=Data->Parent->Root->Value;
#if defined(O_TMPFILE)
    if (access("/proc/self/fd",X_OK)==0) {
        const int Anonymous=openat(Root,".",O_TMPFILE|O_RDWR|O_CLOEXEC,0600);
        if (Anonymous>=0) Draft->Output.emplace(Anonymous);
        else if (errno!=EOPNOTSUPP && errno!=EINVAL && errno!=EISDIR && errno!=ENOENT) Fail("ProviderFailure");
    }
#endif
    if (!Draft->Output) {
        Draft->Output.emplace(openat(Root,Draft->TemporaryName.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
        Draft->Named=true;
        Info(Draft->Output->Value,false);
    }
#endif
    return std::shared_ptr<SelectedWriter>(new SelectedWriter(std::move(Draft)));
}
void SelectedWriter::WriteAt(uint64_t Position,std::string_view Bytes,const std::function<bool()>& Canceled) {
    NonModal Guard;
    if (!Data || Data->Finished) Fail("Closed");
    if (Bytes.size()>65536 || Position>Maximum || Bytes.size()>Maximum-Position) Fail("LimitExceeded");
    Data->Finished=true; // Any partially failed operation forbids publication.
    CheckCancel(Canceled);
#if defined(_WIN32)
    LARGE_INTEGER At{}; At.QuadPart=static_cast<LONGLONG>(Position);
    if (!SetFilePointerEx(Data->Output->Value,At,nullptr,FILE_BEGIN)) Fail("ProviderFailure");
#endif
    size_t Done=0;
#if !defined(_WIN32)
    if (Data->Named) Info(Data->Output->Value,false);
#endif
    while (Done<Bytes.size()) {
        CheckCancel(Canceled);
#if defined(_WIN32)
        DWORD Count=0;
        if (!WriteFile(Data->Output->Value,Bytes.data()+Done,static_cast<DWORD>(Bytes.size()-Done),&Count,nullptr) || !Count) Fail("ProviderFailure");
#else
        const auto Count=pwrite(Data->Output->Value,Bytes.data()+Done,Bytes.size()-Done,static_cast<off_t>(Position+Done));
        if (Count<0 && errno==EINTR) continue;
        if (Count<=0) Fail("ProviderFailure");
#endif
        Done+=static_cast<size_t>(Count);
    }
    CheckCancel(Canceled);
    Data->Finished=false;
}
void SelectedWriter::Commit(const std::function<bool()>& Canceled) {
    NonModal Guard;
    auto Draft=std::move(Data);
    if (!Draft || Draft->Finished) Fail("Closed");
    Draft->Finished=true;
    auto& FileState=*Draft->Destination;
    CheckCancel(Canceled);
#if defined(_WIN32)
    const auto Output=Draft->Output->Value;
    if (!FlushFileBuffers(Output)) Fail("ProviderFailure");
    if (FileState.Parent->Current(FileState.Name)!=Draft->Expected) Fail("ProviderFailure");
    const auto Destination=FileState.Parent->Path(FileState.Name);
    std::vector<char> Buffer(sizeof(FILE_RENAME_INFO)+Destination.size()*sizeof(wchar_t));
    auto* Rename=reinterpret_cast<FILE_RENAME_INFO*>(Buffer.data()); Rename->ReplaceIfExists=TRUE; Rename->RootDirectory=nullptr;
    Rename->FileNameLength=static_cast<DWORD>(Destination.size()*sizeof(wchar_t));
    std::memcpy(Rename->FileName,Destination.data(),Rename->FileNameLength);
    FILE_DISPOSITION_INFO Keep{FALSE};
    if (!SetFileInformationByHandle(Output,FileDispositionInfo,&Keep,sizeof(Keep))) Fail("ProviderFailure");
    CheckCancel(Canceled);
    if (!SetFileInformationByHandle(Output,FileRenameInfo,Rename,static_cast<DWORD>(Buffer.size()))) Fail("ProviderFailure");
    Draft->Named=false;
    Draft->Output.reset();
    FileState.Expected=FileState.Parent->Current(FileState.Name);
#else
    const int Root=FileState.Parent->Root->Value, Output=Draft->Output->Value;
    if (fsync(Output)) Fail("ProviderFailure");
    if (FileState.Parent->Current(FileState.Name)!=Draft->Expected) Fail("ProviderFailure");
    if (!Draft->Named) {
        const std::string Fd="/proc/self/fd/"+std::to_string(Output);
        if (linkat(AT_FDCWD,Fd.c_str(),Root,Draft->TemporaryName.c_str(),AT_SYMLINK_FOLLOW)) Fail("ProviderFailure");
        Draft->Named=true;
    }
    Info(Output,false);
    if (!Draft->SameEntry()) Fail("ProviderFailure");
    CheckCancel(Canceled);
    if (renameat(Root,Draft->TemporaryName.c_str(),Root,FileState.Name.c_str())) Fail("ProviderFailure");
    Draft->Named=false;
    FileState.Expected=Revision(Info(Output,false));
    Draft->Output.reset();
    if (fsync(Root)) Fail("ProviderFailure");
#endif
}
void SelectedFile::Write(std::string_view Bytes,const std::function<bool()>& Canceled) {
    if (!Data->Writable || Data->Folder) Fail("CapabilityDenied");
    if (Bytes.size()>Maximum) Fail("LimitExceeded");
    CheckCancel(Canceled);
    auto Draft=BeginWriter();
    for (size_t Offset=0;Offset<Bytes.size();Offset+=65536) Draft->WriteAt(Offset,Bytes.substr(Offset,65536),Canceled);
    Draft->Commit(Canceled);
}
std::vector<SelectedEntry> SelectedFile::List(const std::function<bool()>& Canceled) {
    NonModal Guard; if(!Data->Folder) Fail("InvalidArgument");
    std::vector<SelectedEntry> Result; size_t Scanned=0;
    auto Add=[&](const std::string& Name,bool Directory,uint64_t Length) {
        CheckCancel(Canceled); if(++Scanned>1024) Fail("LimitExceeded");
        if(SelectedName(Name)) {Result.push_back({Name,Directory,Length}); if(Result.size()>256) Fail("LimitExceeded");}
    };
#if defined(_WIN32)
    WIN32_FIND_DATAW Entry{}; HANDLE Search=FindFirstFileW(Data->Parent->Path("*").c_str(),&Entry);
    if(Search==INVALID_HANDLE_VALUE) Fail("ProviderFailure");
    struct Cleanup {HANDLE Search; ~Cleanup(){FindClose(Search);}} CleanupGuard{Search};
    do {
        const std::string Name=std::filesystem::path(Entry.cFileName).u8string();
        if(Entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) {CheckCancel(Canceled); if(++Scanned>1024) Fail("LimitExceeded"); continue;}
        Add(Name,bool(Entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),(uint64_t(Entry.nFileSizeHigh)<<32)|Entry.nFileSizeLow);
    } while(FindNextFileW(Search,&Entry));
    if(GetLastError()!=ERROR_NO_MORE_FILES) Fail("ProviderFailure");
#else
    const int Copy=openat(Data->Parent->Root->Value,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(Copy<0) Fail("ProviderFailure"); DIR* Folder=fdopendir(Copy); if(!Folder){close(Copy); Fail("ProviderFailure");}
    struct Cleanup {DIR* Folder; ~Cleanup(){closedir(Folder);}} CleanupGuard{Folder};
    errno=0;
    while(const auto* Entry=readdir(Folder)) {
        const std::string Name=Entry->d_name; CheckCancel(Canceled);
        if(++Scanned>1024) Fail("LimitExceeded");
        struct stat Info{};
        if(SelectedName(Name) && !fstatat(Data->Parent->Root->Value,Name.c_str(),&Info,AT_SYMLINK_NOFOLLOW) &&
            (S_ISDIR(Info.st_mode) || (S_ISREG(Info.st_mode) && Info.st_nlink==1))) {
            Result.push_back({Name,S_ISDIR(Info.st_mode),S_ISDIR(Info.st_mode)?0:Size(Info)});
            if(Result.size()>256) Fail("LimitExceeded");
        }
        errno=0;
    }
    if(errno) Fail("ProviderFailure");
#endif
    std::sort(Result.begin(),Result.end(),[](const auto& A,const auto& B){return A.Name<B.Name;}); return Result;
}
std::shared_ptr<SelectedFile> SelectedFile::Open(const std::string& Name) {
    NonModal Guard; if(!Data->Folder || !SelectedName(Name)) Fail("InvalidArgument");
    auto Child=std::make_unique<State>(); Child->Parent=Data->Parent; Child->Name=Name;
    Child->Input=Data->Parent->Open(Name);
    return std::shared_ptr<SelectedFile>(new SelectedFile(std::move(Child)));
}
std::shared_ptr<SelectedFile> SelectedFile::OpenOutput(const std::string& Name) {
    NonModal Guard;
    if (!Data->Folder || !Data->Writable) Fail("CapabilityDenied");
    if (!SelectedName(Name)) Fail("InvalidArgument");
    auto Child=std::make_unique<State>(); Child->Parent=Data->Parent; Child->Name=Name;
    Child->Writable=true; Child->Expected=Data->Parent->Current(Name);
    return std::shared_ptr<SelectedFile>(new SelectedFile(std::move(Child)));
}
void SelectedFile::Change(DirectoryChange Action,const std::string& Name,const std::string& Destination,const std::function<bool()>& Canceled) {
    NonModal Guard;
    if (!Data->Folder || !Data->Writable) Fail("CapabilityDenied");
    const bool Rename=Action==DirectoryChange::Rename;
    if (!SelectedName(Name) || (Rename && !SelectedName(Destination))) Fail("InvalidArgument");
    CheckCancel(Canceled);
#if defined(_WIN32)
    const auto Path=Data->Parent->Path(Name);
    if (Action==DirectoryChange::CreateFolder) {
        if (!CreateDirectoryW(Path.c_str(),nullptr)) Fail("ProviderFailure");
        return;
    }
    const bool Folder=Action==DirectoryChange::RemoveFolder;
    File Entry(CreateFileW(Path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|((Folder || Rename)?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr));
    BY_HANDLE_FILE_INFORMATION Found{};
    if (!GetFileInformationByHandle(Entry.Value,&Found)) Fail("ProviderFailure");
    Info(Entry.Value,Folder || (Rename && (Found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))); CheckCancel(Canceled);
    if (Rename) {
        const auto Target=Data->Parent->Path(Destination);
        const auto Size=static_cast<DWORD>(Target.size()*sizeof(wchar_t));
        std::vector<unsigned char> Buffer(sizeof(FILE_RENAME_INFO)+Size,0);
        auto* Record=reinterpret_cast<FILE_RENAME_INFO*>(Buffer.data());
        Record->FileNameLength=Size;std::memcpy(Record->FileName,Target.data(),Size);
        if (!SetFileInformationByHandle(Entry.Value,FileRenameInfo,Record,static_cast<DWORD>(Buffer.size()))) Fail("ProviderFailure");
    } else {
        FILE_DISPOSITION_INFO Remove{TRUE};
        if (!SetFileInformationByHandle(Entry.Value,FileDispositionInfo,&Remove,sizeof(Remove))) Fail("ProviderFailure");
    }
#else
    const int Root=Data->Parent->Root->Value;
    if (Action==DirectoryChange::CreateFolder) {
        if (mkdirat(Root,Name.c_str(),0700)) Fail("ProviderFailure");
    } else {
        struct stat Entry{};
        if (fstatat(Root,Name.c_str(),&Entry,AT_SYMLINK_NOFOLLOW)) Fail("ProviderFailure");
        const bool Folder=Action==DirectoryChange::RemoveFolder;
        if (!(S_ISDIR(Entry.st_mode)?(Folder || Rename):(!Folder && S_ISREG(Entry.st_mode) && Entry.st_nlink==1))) Fail("CapabilityDenied");
        CheckCancel(Canceled);
        if (Rename) {
#if defined(__linux__)
            if (syscall(SYS_renameat2,Root,Name.c_str(),Root,Destination.c_str(),RENAME_NOREPLACE)) Fail("ProviderFailure");
#else
            Fail("Unavailable");
#endif
        } else if (unlinkat(Root,Name.c_str(),Folder?AT_REMOVEDIR:0)) Fail("ProviderFailure");
    }
    if (fsync(Root)) Fail("ProviderFailure");
#endif
}
}
