#include "Storage.h"
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace Lui::Mobile {
bool RecoverStorage(const std::string& Root) {
#if defined(_WIN32)
    // Android/POSIX recovery only. Desktop storage behavior stays unchanged.
    return false;
#else
    if (Root.empty() || Root.front() != '/' || Root.size() > 4096 || Root.find('\0') != std::string::npos) return false;
    struct File { int Value; ~File() { if (Value >= 0) close(Value); } };
    File Directory{open(Root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (Directory.Value < 0) return false;
    File Lock{openat(Directory.Value, ".lui-lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600)};
    struct stat Info{};
    if (Lock.Value < 0 || fstat(Lock.Value, &Info) || !S_ISREG(Info.st_mode) || Info.st_nlink != 1 ||
        flock(Lock.Value, LOCK_EX | LOCK_NB)) return false;
    if (fstatat(Directory.Value, ".lui-write", &Info, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
    if (!S_ISREG(Info.st_mode) || Info.st_nlink != 1 || Info.st_size < 0 || Info.st_size > 4 * 1024 * 1024) return false;
    return !unlinkat(Directory.Value, ".lui-write", 0) && !fsync(Directory.Value);
#endif
}
}
