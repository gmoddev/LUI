#pragma once
#include "Declarations.h"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Lui::Project {
// Internal shared project data. No VM entry or native extension loading.
struct Limits {
    size_t ManifestBytes = 256 * 1024, LockBytes = 512 * 1024;
    size_t Owners = 33, Files = 256, FileBytes = 8 * 1024 * 1024, SourceBytes = 32 * 1024 * 1024;
    size_t ArtifactBytes = 16 * 1024 * 1024, BytecodeBytes = 64 * 1024 * 1024, RetainedBytes = 128 * 1024 * 1024;
    size_t BinaryBytes = 16 * 1024 * 1024, NativeBytes = 64 * 1024 * 1024;
};
struct Options {
    Limits Bounds;
    // Trusted cancellation query, called between files and after opening before reading.
    // It must not enter a VM. An already running read is not preempted.
    std::function<bool()> Canceled;
};
struct Source {
    std::string Id, Owner, Path, StagedPath, Bytes, Hash;
};
struct Owner {
    std::string Id, Root;
    std::vector<std::string> Requires;
};
struct NativeBinary { std::string Provider, Platform, Path, Hash, Bytes; };
class Snapshot;
namespace Internal {
class SourceReader;
std::shared_ptr<const Snapshot> PrepareSources(const SourceReader&, const std::string&, const Options&, std::string&,
    std::map<std::string, std::string>* Metadata = nullptr);
}
class Snapshot {
public:
    const std::map<std::string, Source>& GetSources() const { return Sources; }
    const std::map<std::string, Owner>& GetOwners() const { return Owners; }
    const std::string& GetEntry() const { return Entry; }
    const std::string& GetDigest() const { return Digest; }
    const std::string& GetDescriptor() const { return Descriptor; }
    const DeclarationSet& GetDeclarations() const { return Declarations; }
    const std::map<std::string, NativeBinary>& GetNativeBinaries() const { return NativeBinaries; }
    const Source* GetSource(std::string_view Id) const;
    // Resolve against captured declared data only. Never reads from the filesystem.
    const Source* Resolve(std::string_view Importer, std::string_view Request, std::string& Error) const;
private:
    std::map<std::string, Source> Sources;
    std::map<std::string, Owner> Owners;
    std::string Entry, Digest, Descriptor;
    DeclarationSet Declarations;
    std::map<std::string, NativeBinary> NativeBinaries;
    friend std::shared_ptr<const Snapshot> Prepare(const std::filesystem::path&, const Options&, std::string&);
    friend std::shared_ptr<const Snapshot> Internal::PrepareSources(const Internal::SourceReader&, const std::string&, const Options&, std::string&,
        std::map<std::string, std::string>*);
};
std::shared_ptr<const Snapshot> Prepare(const std::filesystem::path& Manifest, const Options& Settings, std::string& Error);
// Trusted captured UTF-8 inputs; same validators/resolver as native preparation.
// Exact declared set, max 256 files/8 MiB per file/32 MiB total. No IO or execution.
std::shared_ptr<const Snapshot> PrepareCaptured(const std::map<std::string, std::string>& Files,
    const std::string& Manifest, const Options& Settings, std::string& Error);
// Data-only literal builtin import validation, shared with future analyzer adapters.
bool ValidateImports(const Snapshot& Sources, const Options& Settings, std::string& Error);
struct Artifact {
    std::string Bytes, Hash;
};
class CompiledSnapshot {
public:
    const Snapshot& GetSources() const { return *Sources; }
    const std::map<std::string, Artifact>& GetArtifacts() const { return Artifacts; }
    const std::string& GetDigest() const { return Digest; }
    const std::string& GetDescriptor() const { return Descriptor; }
    size_t GetBytecodeBytes() const { return BytecodeBytes; }
    size_t GetRetainedBytes() const { return RetainedBytes; }
private:
    std::shared_ptr<const Snapshot> Sources;
    std::map<std::string, Artifact> Artifacts;
    std::string Digest, Descriptor;
    size_t BytecodeBytes = 0, RetainedBytes = 0;
    friend std::shared_ptr<const CompiledSnapshot> Compile(std::shared_ptr<const Snapshot>, const Options&, std::string&);
};
// Compiles ALL captured sources, even unused ones. Cancellation is observed between
// compiler calls; compiler/parser temporary allocations are not preempted or capped.
std::shared_ptr<const CompiledSnapshot> Compile(std::shared_ptr<const Snapshot> Sources, const Options& Settings, std::string& Error);
std::string Hash(std::string_view Bytes);
}
