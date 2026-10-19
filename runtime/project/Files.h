#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Lui::Project::Internal {
struct Failure : std::runtime_error { explicit Failure(const char* Code) : std::runtime_error(Code) {} };
[[noreturn]] inline void Fail(const char* Code) { throw Failure(Code); }
struct FileData { std::string Bytes, Identity; };
class Anchor {
public:
    explicit Anchor(const std::filesystem::path& Root);
    ~Anchor();
    Anchor(const Anchor&) = delete;
    Anchor& operator=(const Anchor&) = delete;
    FileData Read(const std::string& Relative, size_t Maximum, const std::function<bool()>& Canceled = {}) const;
    void Validate(const std::string& Relative) const;
    // Exclusive, anchored creation. Existing files/links are never overwritten.
    void Write(const std::string& Relative, std::string_view Bytes) const;
private:
    struct State;
    std::unique_ptr<State> Data;
};
class PathIndex {
public:
    void Add(const std::string& Path);
private:
    std::map<std::string, std::pair<std::string, bool>> Entries;
};
bool PathValid(std::string_view Path, bool Root = false);
std::string Fold(std::string_view Text);
bool Utf8(std::string_view Text);
}
