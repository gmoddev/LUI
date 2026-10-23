#include "LuiProject.h"
#include "Project.h"
#include "Host.h"
#include "Files.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

struct LuiProject {
    std::atomic<uint32_t> References{1};
    std::shared_ptr<const Lui::Project::CompiledSnapshot> Snapshot;
    std::vector<const Lui::Project::Source*> Sources;
};
std::shared_ptr<const Lui::Project::CompiledSnapshot> GetProjectSnapshot(const LuiProject* Project) {
    return Project ? Project->Snapshot : nullptr;
}
LuiProject* WrapProject(std::shared_ptr<const Lui::Project::CompiledSnapshot> Snapshot) {
    if (!Snapshot) return nullptr;
    auto Result = std::make_unique<LuiProject>();
    Result->Snapshot = std::move(Snapshot);
    for (const auto& Pair : Result->Snapshot->GetSources().GetSources()) Result->Sources.push_back(&Pair.second);
    return Result.release();
}
namespace {
void WriteError(char* Output, uint64_t Capacity, const char* Message) noexcept {
    if (!Output || !Capacity) return;
    const auto Count = static_cast<size_t>(std::min<uint64_t>(Capacity - 1, std::strlen(Message)));
    std::memcpy(Output, Message, Count); Output[Count] = '\0';
}
template<class Value> bool Compatible(const Value* Input) {
    return Input && Input->StructSize == sizeof(Value) && Input->AbiVersion == LUI_PROJECT_ABI_VERSION;
}
bool ErrorBuffer(char* Error, uint64_t Capacity) {
    return (!Capacity || Error) && Capacity <= std::numeric_limits<size_t>::max();
}
LuiProjectSourceV1 View(const LuiProject* Project, const Lui::Project::Source& Source) {
    const auto& Artifact = Project->Snapshot->GetArtifacts().at(Source.Id);
    return {sizeof(LuiProjectSourceV1), LUI_PROJECT_ABI_VERSION, Source.Id.c_str(), Source.Owner.c_str(),
        Source.Path.c_str(), Source.StagedPath.c_str(), Source.Hash.c_str(), Artifact.Hash.c_str(),
        Source.Bytes.data(), static_cast<uint64_t>(Source.Bytes.size()), Artifact.Bytes.data(),
        static_cast<uint64_t>(Artifact.Bytes.size())};
}
}
#ifndef LUI_CAPTURED_ONLY
extern "C" LUI_API int LUI_CALL Lui_ProjectPrepare(const LuiProjectOptionsV1* Input,
    const char* ManifestPath, uint64_t PathBytes, LuiProject** Output, char* Error, uint64_t ErrorCapacity) {
    if (Output) *Output = nullptr;
    if (!ErrorBuffer(Error, ErrorCapacity)) return 0;
    WriteError(Error, ErrorCapacity, "");
    try {
        if (!Output || !Compatible(Input) || Input->Reserved || !ManifestPath || !PathBytes || PathBytes > 4096 ||
            !Lui::Project::Internal::Utf8(std::string_view(ManifestPath, static_cast<size_t>(PathBytes)))) {
            WriteError(Error, ErrorCapacity, "[LUI:Project] InvalidInput"); return 0;
        }
        Lui::Project::Options Settings;
        auto Narrow = [](size_t& Bound, uint64_t Value) {
            if (Value > Bound) throw Lui::Project::Internal::Failure("InvalidOptions");
            if (Value) Bound = static_cast<size_t>(Value);
        };
        auto& Bounds = Settings.Bounds;
        Narrow(Bounds.ManifestBytes, Input->ManifestBytes); Narrow(Bounds.LockBytes, Input->LockBytes);
        Narrow(Bounds.Owners, Input->Owners); Narrow(Bounds.Files, Input->Files);
        Narrow(Bounds.FileBytes, Input->FileBytes); Narrow(Bounds.SourceBytes, Input->SourceBytes);
        Narrow(Bounds.ArtifactBytes, Input->ArtifactBytes); Narrow(Bounds.BytecodeBytes, Input->BytecodeBytes);
        Narrow(Bounds.RetainedBytes, Input->RetainedBytes);
        // Copy callback/context so no caller-owned options are retained/read after
        // startup. Callback/context remain valid for this synchronous call only.
        if (Input->Canceled) {
            const auto Callback = Input->Canceled; void* Context = Input->CancellationContext;
            Settings.Canceled = [Callback, Context] { return Callback(Context) != 0; };
        }
        std::string Reason;
        auto Sources = Lui::Project::Prepare(std::filesystem::u8path(ManifestPath, ManifestPath + PathBytes), Settings, Reason);
        if (!Sources) { WriteError(Error, ErrorCapacity, Reason.c_str()); return 0; }
        auto Compiled = Lui::Project::Compile(std::move(Sources), Settings, Reason);
        if (!Compiled) { WriteError(Error, ErrorCapacity, Reason.c_str()); return 0; }
        auto Result = std::make_unique<LuiProject>();
        Result->Snapshot = std::move(Compiled);
        for (const auto& Pair : Result->Snapshot->GetSources().GetSources()) Result->Sources.push_back(&Pair.second);
        *Output = Result.release(); return 1;
    } catch (const Lui::Project::Internal::Failure&) {
        WriteError(Error, ErrorCapacity, "[LUI:Project] InvalidOptions");
    } catch (const std::bad_alloc&) { WriteError(Error, ErrorCapacity, "[LUI:Project] LimitExceeded"); }
    catch (...) { WriteError(Error, ErrorCapacity, "[LUI:Project] InvalidInput"); }
    return 0;
}
#endif
extern "C" LUI_API void LUI_CALL Lui_ProjectRetain(LuiProject* Project) {
    if (Project) Project->References.fetch_add(1, std::memory_order_relaxed);
}
extern "C" LUI_API void LUI_CALL Lui_ProjectRelease(LuiProject* Project) {
    if (Project && Project->References.fetch_sub(1, std::memory_order_acq_rel) == 1) delete Project;
}
extern "C" LUI_API int LUI_CALL Lui_ProjectGetSummary(const LuiProject* Project, LuiProjectSummaryV1* Output) {
    if (!Project || !Compatible(Output)) return 0;
    const auto& Snapshot = *Project->Snapshot;
    *Output = {sizeof(LuiProjectSummaryV1), LUI_PROJECT_ABI_VERSION, 2, static_cast<uint32_t>(Project->Sources.size()),
        Snapshot.GetSources().GetEntry().c_str(), Snapshot.GetSources().GetDigest().c_str(), Snapshot.GetDigest().c_str(),
        Snapshot.GetDescriptor().c_str(), static_cast<uint64_t>(Snapshot.GetDescriptor().size()),
        static_cast<uint64_t>(Snapshot.GetBytecodeBytes()), static_cast<uint64_t>(Snapshot.GetRetainedBytes())};
    return 1;
}
extern "C" LUI_API int LUI_CALL Lui_ProjectGetSource(const LuiProject* Project, uint32_t Index, LuiProjectSourceV1* Output) {
    if (!Project || !Compatible(Output) || Index >= Project->Sources.size()) return 0;
    *Output = View(Project, *Project->Sources[Index]); return 1;
}
extern "C" LUI_API int LUI_CALL Lui_ProjectResolve(const LuiProject* Project,
    const char* Importer, uint64_t ImporterBytes, const char* Request, uint64_t RequestBytes,
    LuiProjectSourceV1* Output, char* Error, uint64_t ErrorCapacity) {
    if (!ErrorBuffer(Error, ErrorCapacity)) return 0;
    WriteError(Error, ErrorCapacity, "");
    try {
        if (!Project || !Compatible(Output) || !Importer || !Request || ImporterBytes > 640 || RequestBytes > 640) {
            WriteError(Error, ErrorCapacity, "[LUI:Project] InvalidInput"); return 0;
        }
        std::string Reason;
        auto* Source = Project->Snapshot->GetSources().Resolve(std::string_view(Importer, static_cast<size_t>(ImporterBytes)),
            std::string_view(Request, static_cast<size_t>(RequestBytes)), Reason);
        if (!Source) { WriteError(Error, ErrorCapacity, Reason.c_str()); return 0; }
        *Output = View(Project, *Source); return 1;
    } catch (...) { WriteError(Error, ErrorCapacity, "[LUI:Project] InvalidInput"); return 0; }
}
