#include "Project.h"
#include "Files.h"
#include "Luau/Compiler.h"
#include <nlohmann/json.hpp>

namespace Lui::Project {
using namespace Internal;
namespace {
void CheckCancellation(const Options& Settings) {
    if (Settings.Canceled && Settings.Canceled()) Fail("Canceled");
}
void Charge(size_t& Total, size_t Bytes, size_t Limit) {
    if (Total > Limit || Bytes > Limit - Total) Fail("LimitExceeded");
    Total += Bytes;
}
// Capacity (including a terminator) plus conservative container bookkeeping.
// This is a retained-data admission budget, not an allocator/process RSS ceiling.
void ChargeText(size_t& Total, const std::string& Text, size_t Limit) {
    Charge(Total, Text.capacity() + 1, Limit);
}
size_t SourceStorage(const Snapshot& Sources, size_t Limit) {
    size_t Total = sizeof(Snapshot);
    ChargeText(Total, Sources.GetEntry(), Limit);
    ChargeText(Total, Sources.GetDigest(), Limit);
    ChargeText(Total, Sources.GetDescriptor(), Limit);
    const auto& Declarations = Sources.GetDeclarations();
    ChargeText(Total,Declarations.Descriptor,Limit); ChargeText(Total,Declarations.Digest,Limit);
    for (const auto& Pair : Declarations.Files) {
        Charge(Total,sizeof(DeclarationFile) + 512,Limit); ChargeText(Total,Pair.first,Limit);
        ChargeText(Total,Pair.second.Bytes,Limit); ChargeText(Total,Pair.second.Hash,Limit);
    }
    for (const auto& Pair : Declarations.Services) {
        Charge(Total,sizeof(ServiceDeclaration) + 512,Limit); ChargeText(Total,Pair.first,Limit);
        for (const auto& Method : Pair.second.Methods) {
            Charge(Total,sizeof(MethodDeclaration) + 512,Limit); ChargeText(Total,Method.first,Limit); ChargeText(Total,Method.second.Returns,Limit);
            Charge(Total,Method.second.Arguments.capacity() * sizeof(std::string),Limit);
            for (const auto& Type : Method.second.Arguments) ChargeText(Total,Type,Limit);
        }
        for (const auto& Signal : Pair.second.Signals) {
            Charge(Total,512 + Signal.second.capacity() * sizeof(std::string),Limit); ChargeText(Total,Signal.first,Limit);
            for (const auto& Type : Signal.second) ChargeText(Total,Type,Limit);
        }
    }
    for(const auto& Pair:Declarations.ServiceProviders) {
        Charge(Total,512,Limit); ChargeText(Total,Pair.first,Limit); ChargeText(Total,Pair.second,Limit);
    }
    for(const auto& Pair:Declarations.NativeProviders) {
        Charge(Total,sizeof(NativeDeclaration)+512,Limit); ChargeText(Total,Pair.first,Limit);
        ChargeText(Total,Pair.second.ProvenanceKind,Limit); ChargeText(Total,Pair.second.ProvenanceId,Limit);
        for(const auto& Binding:Pair.second.Bindings) {
            Charge(Total,sizeof(BinaryBinding)+512,Limit); ChargeText(Total,Binding.first,Limit);
            ChargeText(Total,Binding.second.Path,Limit); ChargeText(Total,Binding.second.Hash,Limit);
        }
    }
    for(const auto& Pair:Sources.GetNativeBinaries()) {
        Charge(Total,sizeof(NativeBinary)+512,Limit); ChargeText(Total,Pair.first,Limit);
        const auto& Value=Pair.second;
        for(const auto* Text:{&Value.Provider,&Value.Platform,&Value.Path,&Value.Hash,&Value.Bytes}) ChargeText(Total,*Text,Limit);
    }
    for (const auto& Pair : Sources.GetSources()) {
        Charge(Total, sizeof(Source) + 512, Limit);
        ChargeText(Total, Pair.first, Limit);
        const auto& Value = Pair.second;
        for (const auto* Text : {&Value.Id, &Value.Owner, &Value.Path, &Value.StagedPath, &Value.Bytes, &Value.Hash})
            ChargeText(Total, *Text, Limit);
    }
    for (const auto& Pair : Sources.GetOwners()) {
        Charge(Total, sizeof(Owner) + 512, Limit);
        ChargeText(Total, Pair.first, Limit);
        ChargeText(Total, Pair.second.Id, Limit);
        ChargeText(Total, Pair.second.Root, Limit);
        Charge(Total, Pair.second.Requires.capacity() * sizeof(std::string), Limit);
        for (const auto& Name : Pair.second.Requires) ChargeText(Total, Name, Limit);
    }
    return Total;
}
}
std::shared_ptr<const CompiledSnapshot> Compile(std::shared_ptr<const Snapshot> Sources,
    const Options& Settings, std::string& Error) {
    Error.clear();
    try {
        const auto& Bounds = Settings.Bounds;
        const Limits Defaults;
        if (!Sources || !Sources->GetSource(Sources->GetEntry())) Fail("InvalidInput");
        if (!Bounds.ArtifactBytes || !Bounds.BytecodeBytes || !Bounds.RetainedBytes ||
            Bounds.ArtifactBytes > Defaults.ArtifactBytes || Bounds.BytecodeBytes > Defaults.BytecodeBytes ||
            Bounds.RetainedBytes > Defaults.RetainedBytes) Fail("InvalidOptions");
        CheckCancellation(Settings);
        if (!ValidateImports(*Sources, Settings, Error)) return {};
        auto Result = std::make_shared<CompiledSnapshot>();
        Result->Sources = std::move(Sources);
        size_t Retained = SourceStorage(*Result->Sources, Bounds.RetainedBytes);
        Charge(Retained, sizeof(CompiledSnapshot), Bounds.RetainedBytes);
        // Reserve the host handle and its sorted pointer index too, even when a
        // C++ consumer does not create that adapter.
        Charge(Retained, 512 + Result->Sources->GetSources().size() * sizeof(void*), Bounds.RetainedBytes);
        Luau::CompileOptions Profile;
        Profile.optimizationLevel = 1;
        Profile.debugLevel = 2;
        Profile.typeInfoLevel = 0;
        Profile.coverageLevel = 0;
        // Do not assume globals (including require) are constant. Ordinary lexical
        // shadowing/private global writes must remain ordinary Luau semantics.
        nlohmann::json ArtifactHashes = nlohmann::json::object();
        for (const auto& Pair : Result->Sources->GetSources()) {
            CheckCancellation(Settings);
            Artifact Value;
            Value.Bytes = Luau::compile(Pair.second.Bytes, Profile);
            CheckCancellation(Settings);
            // Luau encodes compiler diagnostics as a version-zero artifact. Never
            // retain or forward its payload (it can contain source text).
            if (Value.Bytes.empty() || Value.Bytes.front() == '\0') Fail("CompileFailed");
            if (Value.Bytes.size() > Bounds.ArtifactBytes) Fail("LimitExceeded");
            Charge(Result->BytecodeBytes, Value.Bytes.size(), Bounds.BytecodeBytes);
            Value.Hash = Hash(Value.Bytes);
            Charge(Retained, sizeof(Artifact) + 512, Bounds.RetainedBytes);
            ChargeText(Retained, Pair.first, Bounds.RetainedBytes);
            ChargeText(Retained, Value.Bytes, Bounds.RetainedBytes);
            ChargeText(Retained, Value.Hash, Bounds.RetainedBytes);
            ArtifactHashes[Pair.first] = Value.Hash;
            Result->Artifacts.emplace(Pair.first, std::move(Value));
        }
        auto Descriptor = nlohmann::json::parse(Result->Sources->GetDescriptor());
        Descriptor["PreparationVersion"] = 2;
        Descriptor["CompileProfile"] = {{"Name", "LuauBytecode"}, {"Optimization", 1},
            {"Debug", 2}, {"TypeInfo", 0}, {"Coverage", 0}, {"Imports", "LiteralBuiltinV1"}};
        Descriptor["ArtifactHashes"] = std::move(ArtifactHashes);
        Result->Descriptor = Descriptor.dump();
        Result->Digest = Hash(Result->Descriptor);
        ChargeText(Retained, Result->Descriptor, Bounds.RetainedBytes);
        ChargeText(Retained, Result->Digest, Bounds.RetainedBytes);
        Result->RetainedBytes = Retained;
        CheckCancellation(Settings);
        return Result;
    } catch (const Failure& Reason) { Error = "[LUI:Project] " + std::string(Reason.what()); }
    catch (const std::bad_alloc&) { Error = "[LUI:Project] LimitExceeded"; }
    catch (...) { Error = "[LUI:Project] CompileFailed"; }
    return {};
}
}
