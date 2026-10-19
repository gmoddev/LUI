#include "Files.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Lui::Project::Internal {
std::string Fold(std::string_view Text) {
    std::string Result(Text);
    for (char& Character : Result) if (Character >= 'A' && Character <= 'Z') Character += 'a' - 'A';
    return Result;
}
bool PathValid(std::string_view Path, bool Root) {
    if (Root && Path == ".") return true;
    if (Path.empty() || Path.size() > 512) return false;
    size_t Start = 0;
    while (Start < Path.size()) {
        auto End = Path.find('/', Start);
        if (End == std::string_view::npos) End = Path.size();
        auto Part = Path.substr(Start, End - Start);
        if (Part.empty() || Part == "." || Part == ".." || Part.back() == '.') return false;
        for (char Character : Part) if (!((Character >= 'a' && Character <= 'z') ||
            (Character >= 'A' && Character <= 'Z') || (Character >= '0' && Character <= '9') ||
            Character == '_' || Character == '-' || Character == '.')) return false;
        auto Base = Fold(Part.substr(0, Part.find('.')));
        if (Base == "con" || Base == "prn" || Base == "aux" || Base == "nul" ||
            (Base.size() == 4 && (Base.substr(0,3) == "com" || Base.substr(0,3) == "lpt") &&
                Base[3] >= '1' && Base[3] <= '9')) return false;
        Start = End + 1;
    }
    return Path.back() != '/';
}
void PathIndex::Add(const std::string& Path) {
    if (!PathValid(Path)) Fail("InvalidPath");
    size_t End = 0;
    do {
        End = Path.find('/', End + 1);
        const bool Directory = End != std::string::npos;
        const auto Prefix = Path.substr(0, End);
        auto Inserted = Entries.emplace(Fold(Prefix), std::make_pair(Prefix, Directory));
        if (!Inserted.second && (Inserted.first->second.first != Prefix ||
            !Directory || !Inserted.first->second.second)) Fail("DuplicateIdentity");
    } while (End != std::string::npos);
}
bool Utf8(std::string_view Text) {
    for (size_t Index = 0; Index < Text.size();) {
        unsigned char First = static_cast<unsigned char>(Text[Index++]);
        if (!First) return false;
        if (First < 0x80) continue;
        unsigned Count = First >= 0xc2 && First <= 0xdf ? 1 : First >= 0xe0 && First <= 0xef ? 2 :
            First >= 0xf0 && First <= 0xf4 ? 3 : 0;
        if (!Count || Index + Count > Text.size()) return false;
        unsigned Value = First & (Count == 1 ? 0x1f : Count == 2 ? 0x0f : 0x07);
        for (unsigned Offset = 0; Offset < Count; ++Offset) {
            unsigned char Byte = static_cast<unsigned char>(Text[Index++]);
            if ((Byte & 0xc0) != 0x80) return false;
            Value = (Value << 6) | (Byte & 0x3f);
        }
        if (Value < (Count == 1 ? 0x80u : Count == 2 ? 0x800u : 0x10000u) ||
            Value > 0x10ffff || (Value >= 0xd800 && Value <= 0xdfff)) return false;
    }
    return Text.substr(0,3) != "\xef\xbb\xbf";
}

#ifdef _WIN32
struct Handle {
    HANDLE Value = INVALID_HANDLE_VALUE;
    std::vector<std::shared_ptr<Handle>> Parents;
    explicit Handle(HANDLE Value) : Value(Value) { if (Value == INVALID_HANDLE_VALUE) Fail("IoError"); }
    ~Handle() { CloseHandle(Value); }
};
static std::wstring Final(HANDLE Value) {
    DWORD Size = GetFinalPathNameByHandleW(Value, nullptr, 0, FILE_NAME_NORMALIZED);
    if (!Size || Size > 32768) Fail("IoError");
    std::wstring Result(Size, L'\0');
    DWORD Count = GetFinalPathNameByHandleW(Value, Result.data(), Size, FILE_NAME_NORMALIZED);
    if (!Count || Count >= Size) Fail("IoError");
    Result.resize(Count); return Result;
}
static BY_HANDLE_FILE_INFORMATION Info(HANDLE Value, bool Directory) {
    BY_HANDLE_FILE_INFORMATION Result{};
    if (!GetFileInformationByHandle(Value, &Result)) Fail("IoError");
    if (Result.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) Fail("LinkedPath");
    if (bool(Result.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != Directory || GetFileType(Value) != FILE_TYPE_DISK)
        Fail("NotRegularFile");
    return Result;
}
struct Anchor::State { std::shared_ptr<Handle> Root; };
Anchor::Anchor(const std::filesystem::path& Root) : Data(std::make_unique<State>()) {
    Data->Root = std::make_shared<Handle>(CreateFileW(Root.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Info(Data->Root->Value, true);
}
static std::shared_ptr<Handle> Open(const std::shared_ptr<Handle>& Root, const std::string& Relative) {
    if (!PathValid(Relative)) Fail("InvalidPath");
    std::vector<std::shared_ptr<Handle>> Held{Root};
    size_t Start = 0;
    while (Start < Relative.size()) {
        size_t End = Relative.find('/', Start);
        bool Directory = End != std::string::npos;
        if (!Directory) End = Relative.size();
        std::string Part = Relative.substr(Start, End - Start);
        std::wstring Wide(Part.begin(), Part.end());
        auto Parent = Final(Held.back()->Value);
        auto Path = Parent + L"\\" + Wide;
        HANDLE Native = CreateFileW(Path.c_str(), Directory ? FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES : GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (Native == INVALID_HANDLE_VALUE) {
            DWORD Error = GetLastError();
            Fail(Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND ? "MissingFile" : "IoError");
        }
        auto File = std::make_shared<Handle>(Native);
        Info(File->Value, Directory);
        // Parent/file handles deny write/delete sharing while validating. Use stored spelling, not case-insensitive lookup.
        WIN32_FIND_DATAW Entry{};
        HANDLE Search = FindFirstFileW(Path.c_str(), &Entry);
        if (Search == INVALID_HANDLE_VALUE) Fail("FileChanged");
        FindClose(Search);
        if (Wide != Entry.cFileName) Fail("CaseMismatch");
        auto RootPath = Final(Root->Value) + L"\\";
        auto FilePath = Final(File->Value);
        if (FilePath.size() <= RootPath.size() ||
            CompareStringOrdinal(FilePath.data(), static_cast<int>(RootPath.size()), RootPath.data(),
                static_cast<int>(RootPath.size()), TRUE) != CSTR_EQUAL) Fail("LinkedPath");
        Held.push_back(std::move(File));
        Start = End + 1;
    }
    Held.back()->Parents.assign(Held.begin(), Held.end() - 1);
    return Held.back();
}
FileData Anchor::Read(const std::string& Relative, size_t Maximum, const std::function<bool()>& Canceled) const {
    auto File = Open(Data->Root, Relative);
    auto Before = Info(File->Value, false);
    uint64_t Size = (uint64_t(Before.nFileSizeHigh) << 32) | Before.nFileSizeLow;
    if (Size > Maximum) Fail("LimitExceeded");
    if (Canceled && Canceled()) Fail("Canceled");
    FileData Result;
    Result.Identity = std::to_string(Before.dwVolumeSerialNumber) + ":" +
        std::to_string(Before.nFileIndexHigh) + ":" + std::to_string(Before.nFileIndexLow);
    Result.Bytes.resize(static_cast<size_t>(Size));
    size_t Offset = 0;
    while (Offset < Size) {
        DWORD Count = 0;
        if (!ReadFile(File->Value, Result.Bytes.data() + Offset,
            static_cast<DWORD>(std::min<uint64_t>(Size - Offset, 65536)), &Count, nullptr)) Fail("IoError");
        if (!Count) Fail("FileChanged");
        Offset += Count;
    }
    auto After = Info(File->Value, false);
    if (Before.nFileSizeHigh != After.nFileSizeHigh || Before.nFileSizeLow != After.nFileSizeLow ||
        std::memcmp(&Before.ftLastWriteTime, &After.ftLastWriteTime, sizeof(FILETIME))) Fail("FileChanged");
    return Result;
}
void Anchor::Validate(const std::string& Relative) const { Info(Open(Data->Root, Relative)->Value, false); }
void Anchor::Write(const std::string& Relative, std::string_view Bytes) const {
    if (!PathValid(Relative)) Fail("InvalidPath");
    // Keep every parent handle pinned with write/delete sharing denied. Parent
    // creation is safe against replacement because its own parent is pinned.
    auto Parent = Data->Root;
    std::vector<std::shared_ptr<Handle>> Held{Parent};
    size_t Start = 0;
    while (true) {
        const auto End = Relative.find('/', Start);
        const auto Part = Relative.substr(Start, End == std::string::npos ? End : End - Start);
        const std::wstring Wide(Part.begin(), Part.end());
        const auto Path = Final(Parent->Value) + L"\\" + Wide;
        if (End == std::string::npos) {
            Handle File(CreateFileW(Path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            Info(File.Value, false);
            size_t Offset = 0;
            while (Offset < Bytes.size()) {
                DWORD Count = 0;
                if (!WriteFile(File.Value, Bytes.data() + Offset,
                    static_cast<DWORD>(std::min<size_t>(Bytes.size() - Offset, 65536)), &Count, nullptr) || !Count) Fail("IoError");
                Offset += Count;
            }
            if (!FlushFileBuffers(File.Value)) Fail("IoError");
            return;
        }
        if (!CreateDirectoryW(Path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) Fail("IoError");
        auto Directory = std::make_shared<Handle>(CreateFileW(Path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        Info(Directory->Value, true);
        WIN32_FIND_DATAW Entry{}; HANDLE Search = FindFirstFileW(Path.c_str(), &Entry);
        if (Search == INVALID_HANDLE_VALUE) Fail("IoError");
        FindClose(Search); if (Wide != Entry.cFileName) Fail("CaseMismatch");
        Held.push_back(Directory); Parent = std::move(Directory); Start = End + 1;
    }
}
#else
struct Handle {
    int Value;
    explicit Handle(int Value) : Value(Value) {
        if (Value < 0) Fail(errno == ELOOP ? "LinkedPath" : errno == ENOENT ? "MissingFile" : "IoError");
    }
    ~Handle() { close(Value); }
};
struct Anchor::State { std::shared_ptr<Handle> Root; };
Anchor::Anchor(const std::filesystem::path& Root) : Data(std::make_unique<State>()) {
    Data->Root = std::make_shared<Handle>(open(Root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
}
static bool WrongCase(int Parent, const std::string& Name) {
    int Copy = openat(Parent, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (Copy < 0) Fail("IoError");
    DIR* Directory = fdopendir(Copy);
    if (!Directory) { close(Copy); Fail("IoError"); }
    bool Found = false;
    while (auto* Entry = readdir(Directory)) if (Fold(Entry->d_name) == Fold(Name)) { Found = true; break; }
    closedir(Directory); return Found;
}
static std::shared_ptr<Handle> Open(const std::shared_ptr<Handle>& Root, const std::string& Relative) {
    if (!PathValid(Relative)) Fail("InvalidPath");
    auto Parent = Root;
    size_t Start = 0;
    while (Start < Relative.size()) {
        size_t End = Relative.find('/', Start);
        bool Directory = End != std::string::npos;
        if (!Directory) End = Relative.size();
        auto Part = Relative.substr(Start, End - Start);
        // Classify links before open, then O_NOFOLLOW independently enforces it during open.
        struct stat Entry{};
        if (fstatat(Parent->Value, Part.c_str(), &Entry, AT_SYMLINK_NOFOLLOW))
            Fail(errno == ENOENT && WrongCase(Parent->Value, Part) ? "CaseMismatch" : "MissingFile");
        if (S_ISLNK(Entry.st_mode)) Fail("LinkedPath");
        auto File = std::make_shared<Handle>(openat(Parent->Value, Part.c_str(), O_RDONLY | O_NOFOLLOW |
            O_CLOEXEC | O_NONBLOCK | (Directory ? O_DIRECTORY : 0)));
        struct stat Opened{};
        if (fstat(File->Value, &Opened)) Fail("IoError");
        if (Entry.st_dev != Opened.st_dev || Entry.st_ino != Opened.st_ino) Fail("FileChanged");
        if (!(Directory ? S_ISDIR(Opened.st_mode) : S_ISREG(Opened.st_mode))) Fail("NotRegularFile");
        Parent = std::move(File); Start = End + 1;
    }
    return Parent;
}
FileData Anchor::Read(const std::string& Relative, size_t Maximum, const std::function<bool()>& Canceled) const {
    auto File = Open(Data->Root, Relative);
    struct stat Before{};
    if (fstat(File->Value, &Before)) Fail("IoError");
    if (Before.st_size < 0 || static_cast<uint64_t>(Before.st_size) > Maximum) Fail("LimitExceeded");
    if (Canceled && Canceled()) Fail("Canceled");
    FileData Result;
    Result.Identity = std::to_string(Before.st_dev) + ":" + std::to_string(Before.st_ino);
    Result.Bytes.resize(static_cast<size_t>(Before.st_size));
    size_t Offset = 0;
    while (Offset < Result.Bytes.size()) {
        ssize_t Count = read(File->Value, Result.Bytes.data() + Offset, Result.Bytes.size() - Offset);
        if (Count < 0 && errno == EINTR) continue;
        if (Count <= 0) Fail(Count == 0 ? "FileChanged" : "IoError");
        Offset += static_cast<size_t>(Count);
    }
    struct stat After{};
    if (fstat(File->Value, &After)) Fail("IoError");
    if (Before.st_size != After.st_size || Before.st_mtim.tv_sec != After.st_mtim.tv_sec ||
        Before.st_mtim.tv_nsec != After.st_mtim.tv_nsec || Before.st_ctim.tv_sec != After.st_ctim.tv_sec ||
        Before.st_ctim.tv_nsec != After.st_ctim.tv_nsec) Fail("FileChanged");
    return Result;
}
void Anchor::Validate(const std::string& Relative) const { Open(Data->Root, Relative); }
void Anchor::Write(const std::string& Relative, std::string_view Bytes) const {
    if (!PathValid(Relative)) Fail("InvalidPath");
    auto Parent = Data->Root;
    size_t Start = 0;
    while (true) {
        const auto End = Relative.find('/', Start);
        const auto Part = Relative.substr(Start, End == std::string::npos ? End : End - Start);
        if (End == std::string::npos) {
            Handle File(openat(Parent->Value, Part.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
            size_t Offset = 0;
            while (Offset < Bytes.size()) {
                const auto Count = write(File.Value, Bytes.data() + Offset, Bytes.size() - Offset);
                if (Count < 0 && errno == EINTR) continue;
                if (Count <= 0) Fail("IoError");
                Offset += static_cast<size_t>(Count);
            }
            if (fsync(File.Value)) Fail("IoError");
            return;
        }
        if (mkdirat(Parent->Value, Part.c_str(), 0700) && errno != EEXIST) Fail("IoError");
        auto Directory = std::make_shared<Handle>(openat(Parent->Value, Part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        Parent = std::move(Directory); Start = End + 1;
    }
}
#endif
Anchor::~Anchor() = default;
}
