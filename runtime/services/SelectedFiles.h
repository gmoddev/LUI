#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Lui::Native {
bool SelectedName(std::string_view Name);
enum class DirectoryChange {CreateFolder, RemoveFolder, RemoveFile, Rename};
struct SelectedEntry { std::string Name; bool Directory; uint64_t Size; };
class SelectionScope {
public:
    SelectionScope();
    ~SelectionScope();
private:
    friend class SelectedFile;
    struct State;
    std::unique_ptr<State> Data;
};
// Private provider authority. Paths enter only from a trusted native picker.
class SelectedWriter;
class SelectedFile {
public:
    SelectedFile(const std::filesystem::path& Path, bool Directory, bool Writable, const std::shared_ptr<SelectionScope>& Scope = {});
    ~SelectedFile();
    std::string Name() const;
    void RestrictToWriting();
    std::string Read(const std::function<bool()>& Canceled);
    std::string ReadRange(uint64_t Position, uint32_t Count, const std::function<bool()>& Canceled);
    uint64_t ByteLength() const;
    void Write(std::string_view Bytes, const std::function<bool()>& Canceled);
    std::shared_ptr<SelectedWriter> BeginWriter();
    std::vector<SelectedEntry> List(const std::function<bool()>& Canceled);
    std::shared_ptr<SelectedFile> Open(const std::string& Name);
    std::shared_ptr<SelectedFile> OpenOutput(const std::string& Name);
    void Change(DirectoryChange Action,const std::string& Name,const std::string& Destination,const std::function<bool()>& Canceled);
private:
    friend class SelectedWriter;
    struct State;
    std::shared_ptr<State> Data;
    explicit SelectedFile(std::unique_ptr<State> Data);
    std::string ReadSpan(uint64_t Position, uint32_t Count, bool Whole, const std::function<bool()>& Canceled);
};
class SelectedWriter {
public:
    static constexpr uint64_t Maximum = 128ULL * 1024 * 1024;
    ~SelectedWriter();
    void WriteAt(uint64_t Position, std::string_view Bytes, const std::function<bool()>& Canceled);
    void Commit(const std::function<bool()>& Canceled);
private:
    friend class SelectedFile;
    struct State;
    std::unique_ptr<State> Data;
    explicit SelectedWriter(std::unique_ptr<State> Data);
};
}
