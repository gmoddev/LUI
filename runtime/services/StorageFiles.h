#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Lui::Native {
bool StorageKey(std::string_view Key);
class StorageFiles {
public:
    explicit StorageFiles(const std::filesystem::path& Root);
    StorageFiles(const std::filesystem::path& DataRoot, const std::string& Scope);
    ~StorageFiles();
    bool Read(const std::string& Key, std::string& Result);
    void Write(const std::string& Key, std::string_view Value, const std::function<bool()>& Canceled);
    void Remove(const std::string& Key);
private:
    struct State;
    std::unique_ptr<State> Data;
};
}
