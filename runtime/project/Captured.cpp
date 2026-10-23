#include "Project.h"
#include "Files.h"
#include <set>

namespace Lui::Project {
namespace {
class CapturedReader final : public Internal::SourceReader {
    const std::map<std::string, std::string>& Files;
    mutable std::set<std::string> Used;
public:
    explicit CapturedReader(const std::map<std::string, std::string>& Input) : Files(Input) {
        if (Files.empty() || Files.size() > 256) Internal::Fail("LimitExceeded");
        size_t Total = 0;
        Internal::PathIndex Paths;
        for (const auto& Pair : Files) {
            Paths.Add(Pair.first);
            if (Pair.second.size() > 8 * 1024 * 1024 || Pair.second.size() > 32 * 1024 * 1024 - Total)
                Internal::Fail("LimitExceeded");
            Total += Pair.second.size();
        }
    }
    Internal::FileData Read(const std::string& Relative, size_t Maximum,
        const std::function<bool()>& Canceled = {}) const override {
        if (Canceled && Canceled()) Internal::Fail("Canceled");
        Validate(Relative);
        const auto& Bytes = Files.at(Relative);
        if (Bytes.size() > Maximum) Internal::Fail("LimitExceeded");
        return {Bytes, "Captured:" + Relative};
    }
    void Validate(const std::string& Relative) const override {
        if (!Internal::PathValid(Relative)) Internal::Fail("InvalidPath");
        if (!Files.count(Relative)) Internal::Fail("MissingFile");
        Used.insert(Relative);
    }
    bool Complete() const { return Used.size() == Files.size(); }
};
}
std::shared_ptr<const Snapshot> PrepareCaptured(const std::map<std::string, std::string>& Files,
    const std::string& Manifest, const Options& Settings, std::string& Error) {
    Error.clear();
    try {
        CapturedReader Reader(Files);
        auto Result = Internal::PrepareSources(Reader, Manifest, Settings, Error);
        if (Result && !Reader.Complete()) Internal::Fail("UnexpectedFile");
        return Result;
    } catch (const Internal::Failure& Reason) { Error = std::string("[LUI:Project] ") + Reason.what(); }
    catch (const std::bad_alloc&) { Error = "[LUI:Project] LimitExceeded"; }
    catch (...) { Error = "[LUI:Project] InvalidInput"; }
    return {};
}
}
