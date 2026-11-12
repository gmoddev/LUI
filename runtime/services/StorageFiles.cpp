#include "StorageFiles.h"
#include "../project/Files.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cerrno>
#include <map>
#include <vector>
#include <utility>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Lui::Native {
using Lui::Project::Internal::Fail;
constexpr size_t FileMaximum = 4 * 1024 * 1024;
bool StorageKey(std::string_view Key) {
    return !Key.empty() && Key.size() <= 64 && std::all_of(Key.begin(), Key.end(), [](char Byte) {
        return (Byte >= 'a' && Byte <= 'z') || (Byte >= '0' && Byte <= '9') || Byte == '_' || Byte == '-';
    });
}
namespace {
struct NonModal {
#if defined(_WIN32)
    DWORD Previous = 0;
    NonModal() { if (!SetThreadErrorMode(GetThreadErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX,&Previous)) Fail("ProviderFailure"); }
    ~NonModal() { SetThreadErrorMode(Previous,nullptr); }
#endif
};
std::string FileName(const std::string& Key) {
    if (!StorageKey(Key)) Fail("InvalidArgument");
    return "K-" + Key + ".bin";
}
bool DataName(const std::string& Name) {
    return Name.size() > 6 && Name.substr(0,2) == "K-" && Name.substr(Name.size()-4) == ".bin" &&
        StorageKey(std::string_view(Name).substr(2,Name.size()-6));
}
#if defined(_WIN32)
struct File {
    HANDLE Value = INVALID_HANDLE_VALUE;
    explicit File(HANDLE Value) : Value(Value) { if (Value == INVALID_HANDLE_VALUE) Fail("ProviderFailure"); }
    File(const File&) = delete;
    File(File&& Other) noexcept : Value(std::exchange(Other.Value,INVALID_HANDLE_VALUE)) {}
    File& operator=(File&& Other) noexcept {
        if (this != &Other) { Close(); Value = std::exchange(Other.Value,INVALID_HANDLE_VALUE); }
        return *this;
    }
    ~File() { if (Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }
    void Close() { if (Value != INVALID_HANDLE_VALUE) { CloseHandle(Value); Value = INVALID_HANDLE_VALUE; } }
};
BY_HANDLE_FILE_INFORMATION Info(HANDLE Value, bool Directory = false) {
    BY_HANDLE_FILE_INFORMATION Result{};
    if (!GetFileInformationByHandle(Value,&Result) || GetFileType(Value) != FILE_TYPE_DISK ||
        (Result.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(Result.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != Directory || (!Directory && Result.nNumberOfLinks != 1)) Fail("ProviderFailure");
    return Result;
}
std::wstring Final(HANDLE Value) {
    DWORD Length = GetFinalPathNameByHandleW(Value,nullptr,0,FILE_NAME_NORMALIZED);
    if (!Length || Length > 32768) Fail("ProviderFailure");
    std::wstring Result(Length,L'\0'); const auto Count = GetFinalPathNameByHandleW(Value,Result.data(),Length,FILE_NAME_NORMALIZED);
    if (!Count || Count >= Length) Fail("ProviderFailure"); Result.resize(Count); return Result;
}
uint64_t Size(const BY_HANDLE_FILE_INFORMATION& Value) { return (uint64_t(Value.nFileSizeHigh)<<32) | Value.nFileSizeLow; }
std::vector<File> OpenScope(const std::filesystem::path& Base, const std::string& Scope) {
    std::vector<File> Held; Held.reserve(4);
    Held.emplace_back(CreateFileW(Base.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Info(Held.back().Value, true);
    for (const auto& Part : {std::string("LUI"), std::string("Applications"), Scope}) {
        const auto Path = Final(Held.back().Value) + L"\\" + std::wstring(Part.begin(), Part.end());
        if (!CreateDirectoryW(Path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) Fail("ProviderFailure");
        Held.emplace_back(CreateFileW(Path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        Info(Held.back().Value, true);
    }
    return Held;
}
#else
struct File {
    int Value = -1;
    explicit File(int Value) : Value(Value) { if (Value < 0) Fail("ProviderFailure"); }
    File(const File&) = delete;
    File(File&& Other) noexcept : Value(std::exchange(Other.Value,-1)) {}
    ~File() { if (Value >= 0) close(Value); }
};
struct stat Info(int Value, bool Directory = false) {
    struct stat Result{};
    if (fstat(Value,&Result) || (Directory ? !S_ISDIR(Result.st_mode) : (!S_ISREG(Result.st_mode) || Result.st_nlink != 1))) Fail("ProviderFailure");
    return Result;
}
std::vector<File> OpenScope(const std::filesystem::path& Base, const std::string& Scope) {
    std::vector<File> Held; Held.reserve(4);
    Held.emplace_back(open(Base.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    Info(Held.back().Value, true);
    for (const auto& Part : {std::string(".lui"), std::string("Applications"), Scope}) {
        if (mkdirat(Held.back().Value, Part.c_str(), 0700) && errno != EEXIST) Fail("ProviderFailure");
        Held.emplace_back(openat(Held.back().Value, Part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        const auto Value = Info(Held.back().Value, true);
        if (Value.st_uid != geteuid() || (Value.st_mode & 077)) Fail("ProviderFailure");
    }
    return Held;
}
#endif
}
struct StorageFiles::State {
    File Root;
    std::unique_ptr<File> Lock;
    // Keep the complete provisioning walk pinned until lock acquisition and
    // the Windows root-sharing transition have both completed.
    explicit State(std::vector<File> Scope) : State(std::move(Scope.back())) {}
#if defined(_WIN32)
    explicit State(const std::filesystem::path& Path) : State(File(CreateFileW(Path.c_str(),FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr))) {}
    explicit State(File Handle) : Root(std::move(Handle)) {
        Info(Root.Value,true);
        Lock = std::make_unique<File>(File(CreateFileW((Final(Root.Value)+L"\\.lui-lock").c_str(),GENERIC_READ | GENERIC_WRITE,
            0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr))); Info(Lock->Value);
        // MoveFileEx's durable replacement requires write sharing on the root.
        // The exclusive nondelete-shared lock now keeps this directory nonempty,
        // preventing conversion to a directory reparse point. No unheld gap:
        // open and compare the replacement while the restrictive root is held.
        const auto Before = Info(Root.Value,true);
        File Writable(CreateFileW(Final(Root.Value).c_str(),FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        const auto After = Info(Writable.Value,true);
        if (Before.dwVolumeSerialNumber != After.dwVolumeSerialNumber || Before.nFileIndexHigh != After.nFileIndexHigh ||
            Before.nFileIndexLow != After.nFileIndexLow) Fail("ProviderFailure");
        Root = std::move(Writable);
    }
    std::wstring Path(const std::string& Name) { return Final(Root.Value)+L"\\"+std::wstring(Name.begin(),Name.end()); }
#else
    explicit State(const std::filesystem::path& Path) : State(File(open(Path.c_str(),O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC))) {}
    explicit State(File Handle) : Root(std::move(Handle)) {
        Info(Root.Value,true);
        Lock = std::make_unique<File>(File(openat(Root.Value,".lui-lock",O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC,0600))); Info(Lock->Value);
        if (flock(Lock->Value,LOCK_EX | LOCK_NB)) Fail("ProviderFailure");
    }
#endif
    std::map<std::string,uint64_t> Scan() {
        std::map<std::string,uint64_t> Result;
#if defined(_WIN32)
        WIN32_FIND_DATAW Entry{}; HANDLE Search = FindFirstFileW((Final(Root.Value)+L"\\*").c_str(),&Entry);
        if (Search == INVALID_HANDLE_VALUE) Fail("ProviderFailure");
        struct Guard { HANDLE Value; ~Guard() { FindClose(Value); } } Cleanup{Search};
        do {
            const std::wstring Wide = Entry.cFileName; if (Wide == L"." || Wide == L".." || Wide == L".lui-lock") continue;
            std::string Name; Name.reserve(Wide.size());
            for (const auto Byte : Wide) { if (Byte > 127) Fail("ProviderFailure"); Name += static_cast<char>(Byte); }
            if (!DataName(Name)) Fail("ProviderFailure");
            File Item(CreateFileW(Path(Name).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            const auto Length = Size(Info(Item.Value)); if (Length > FileMaximum) Fail("LimitExceeded"); Result.emplace(Name,Length);
            if (Result.size() > 128) Fail("LimitExceeded");
        } while (FindNextFileW(Search,&Entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) Fail("ProviderFailure");
#else
        const int Duplicate = openat(Root.Value,".",O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (Duplicate < 0) Fail("ProviderFailure");
        DIR* Directory = fdopendir(Duplicate);
        if (!Directory) { close(Duplicate); Fail("ProviderFailure"); }
        struct Guard { DIR* Value; ~Guard() { closedir(Value); } } Cleanup{Directory};
        errno = 0;
        while (const auto* Entry = readdir(Directory)) {
            const std::string Name = Entry->d_name;
            if (Name == "." || Name == ".." || Name == ".lui-lock") continue;
            if (!DataName(Name)) Fail("ProviderFailure");
            File Item(openat(Root.Value,Name.c_str(),O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC)); const auto Value = Info(Item.Value);
            if (Value.st_size < 0 || uint64_t(Value.st_size) > FileMaximum) Fail("LimitExceeded"); Result.emplace(Name,Value.st_size);
            if (Result.size() > 128) Fail("LimitExceeded");
            errno = 0;
        }
        if (errno) Fail("ProviderFailure");
#endif
        uint64_t Total = 0; for (const auto& Pair : Result) Total += Pair.second;
        if (Result.size() > 128 || Total > 64*1024*1024) Fail("LimitExceeded"); return Result;
    }
};
StorageFiles::StorageFiles(const std::filesystem::path& Root) {
    NonModal Guard;
    if (!Root.is_absolute()) Fail("InvalidArgument");
    Data = std::make_unique<State>(Root); Data->Scan();
}
StorageFiles::~StorageFiles() = default;
StorageFiles::StorageFiles(const std::filesystem::path& DataRoot, const std::string& Scope) {
    NonModal Guard;
    if (!DataRoot.is_absolute() || Scope.size() != 64 || !std::all_of(Scope.begin(), Scope.end(), [](char Byte) {
        return (Byte >= 'a' && Byte <= 'f') || (Byte >= '0' && Byte <= '9');
    })) Fail("InvalidArgument");
    Data = std::make_unique<State>(OpenScope(DataRoot, Scope)); Data->Scan();
}
bool StorageFiles::Read(const std::string& Key, std::string& Result) {
    NonModal Guard;
    const auto Name = FileName(Key); const auto Entries = Data->Scan();
    if (!Entries.count(Name)) return false;
    Result.resize(static_cast<size_t>(Entries.at(Name))); size_t Offset = 0;
#if defined(_WIN32)
    File Input(CreateFileW(Data->Path(Name).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    const auto Before = Info(Input.Value); if (Size(Before) != Result.size()) Fail("ProviderFailure");
    while (Offset < Result.size()) {
        DWORD Count = 0;
        if (!ReadFile(Input.Value,Result.data()+Offset,static_cast<DWORD>(std::min<size_t>(65536,Result.size()-Offset)),&Count,nullptr) || !Count) Fail("ProviderFailure");
        Offset += Count;
    }
    const auto After = Info(Input.Value);
    if (Size(After) != Size(Before) || std::memcmp(&Before.ftLastWriteTime,&After.ftLastWriteTime,sizeof(FILETIME))) Fail("ProviderFailure");
#else
    File Input(openat(Data->Root.Value,Name.c_str(),O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    const auto Before = Info(Input.Value); if (uint64_t(Before.st_size) != Result.size()) Fail("ProviderFailure");
    while (Offset < Result.size()) {
        const auto Count = read(Input.Value,Result.data()+Offset,Result.size()-Offset);
        if (Count < 0 && errno == EINTR) continue;
        if (Count <= 0) Fail("ProviderFailure"); Offset += static_cast<size_t>(Count);
    }
    const auto After = Info(Input.Value);
    if (Before.st_size != After.st_size || Before.st_mtim.tv_sec != After.st_mtim.tv_sec || Before.st_mtim.tv_nsec != After.st_mtim.tv_nsec ||
        Before.st_ctim.tv_sec != After.st_ctim.tv_sec || Before.st_ctim.tv_nsec != After.st_ctim.tv_nsec) Fail("ProviderFailure");
#endif
    return true;
}
void StorageFiles::Write(const std::string& Key, std::string_view Value, const std::function<bool()>& Canceled) {
    NonModal NonModalGuard;
    const auto Name = FileName(Key); const auto Entries = Data->Scan();
    uint64_t Total = 0; for (const auto& Pair : Entries) if (Pair.first != Name) Total += Pair.second;
    if (Value.size() > FileMaximum || Total + Value.size() > 64*1024*1024 || (!Entries.count(Name) && Entries.size() == 128)) Fail("LimitExceeded");
    if (Canceled()) Fail("Canceled");
    // The exclusive scope lock permits one fixed, exclusively created temporary.
    // Any stale/unexpected file fails closed during Scan, before a write begins.
    const std::string Temporary = ".lui-write";
    size_t Offset = 0;
#if defined(_WIN32)
    const auto TemporaryPath = Data->Path(Temporary), DestinationPath = Data->Path(Name);
    struct Guard { const std::wstring* Path; bool Active = false; ~Guard() { if (Active) DeleteFileW(Path->c_str()); } } Cleanup{&TemporaryPath};
    File Output(CreateFileW(TemporaryPath.c_str(),GENERIC_WRITE | FILE_READ_ATTRIBUTES,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,nullptr)); Cleanup.Active = true; Info(Output.Value);
    try {
        while (Offset < Value.size()) {
            if (Canceled()) Fail("Canceled"); DWORD Count = 0;
            if (!WriteFile(Output.Value,Value.data()+Offset,static_cast<DWORD>(std::min<size_t>(65536,Value.size()-Offset)),&Count,nullptr) || !Count) Fail("ProviderFailure");
            Offset += Count;
        }
        if (!FlushFileBuffers(Output.Value)) Fail("ProviderFailure"); Output.Close();
        if (Canceled()) Fail("Canceled");
        if (!MoveFileExW(TemporaryPath.c_str(),DestinationPath.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) Fail("ProviderFailure");
        Cleanup.Active = false;
    } catch (...) { Output.Close(); throw; }
#else
    struct Guard { int Root; bool Active = false; ~Guard() { if (Active) unlinkat(Root,".lui-write",0); } } Cleanup{Data->Root.Value};
    File Output(openat(Data->Root.Value,Temporary.c_str(),O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,0600)); Cleanup.Active = true; Info(Output.Value);
    while (Offset < Value.size()) {
        if (Canceled()) Fail("Canceled"); const auto Count = write(Output.Value,Value.data()+Offset,Value.size()-Offset);
        if (Count < 0 && errno == EINTR) continue;
        if (Count <= 0) Fail("ProviderFailure"); Offset += static_cast<size_t>(Count);
    }
    if (fsync(Output.Value)) Fail("ProviderFailure");
    if (Canceled()) Fail("Canceled");
    if (renameat(Data->Root.Value,Temporary.c_str(),Data->Root.Value,Name.c_str())) Fail("ProviderFailure");
    Cleanup.Active = false;
    if (fsync(Data->Root.Value)) Fail("ProviderFailure");
#endif
}
void StorageFiles::Remove(const std::string& Key) {
    NonModal Guard;
    const auto Name = FileName(Key); if (!Data->Scan().count(Name)) return;
#if defined(_WIN32)
    if (!DeleteFileW(Data->Path(Name).c_str())) Fail("ProviderFailure");
#else
    if (unlinkat(Data->Root.Value,Name.c_str(),0) || fsync(Data->Root.Value)) Fail("ProviderFailure");
#endif
}
}
