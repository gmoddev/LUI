#pragma once
#include "Project.h"

namespace Lui::Project {
struct PackageOptions {
    Options Preparation;
    size_t AssetBytes = 16 * 1024 * 1024, TotalAssetBytes = 64 * 1024 * 1024;
    size_t IndexBytes = 1024 * 1024;
};
struct PackageFile { std::string Bytes, Hash; };
class PackageSnapshot {
public:
    const CompiledSnapshot& GetPrepared() const { return *Prepared; }
    std::shared_ptr<const CompiledSnapshot> GetSnapshot() const { return Prepared; }
    const std::map<std::string, PackageFile>& GetFiles() const { return Files; }
    const std::string& GetIndex() const { return Index; }
    const std::string& GetDigest() const { return Digest; }
    size_t GetRetainedBytes() const { return RetainedBytes; }
private:
    PackageSnapshot() = default;
    std::shared_ptr<const CompiledSnapshot> Prepared;
    // Canonical manifest/lock and captured binary assets. Source bytes remain in Prepared.
    std::map<std::string, PackageFile> Files;
    std::string Index, Digest;
    size_t RetainedBytes = 0;
    friend struct PackageCapture;
};
// Data-only content profile: all declared sources/dependencies and assets,
// canonical manifest/lock. No application execution, DLL or platform host copy.
std::shared_ptr<const PackageSnapshot> CapturePackage(const std::filesystem::path& Manifest,
    const PackageOptions& Settings, std::string& Error);
// Output must NOT exist; its parent must exist. Files use exclusive anchored
// creation and the index is written last. Failure may leave an incomplete folder;
// this function never overwrites or recursively deletes an existing path.
bool StagePackage(const PackageSnapshot& Package, const std::filesystem::path& Output,
    const std::function<bool()>& Canceled, std::string& Error);
// Verify the deterministic index against newly captured, bounded opened-file
// inputs. Consumers must use the returned snapshot, never reread verified paths.
std::shared_ptr<const PackageSnapshot> VerifyPackage(const std::filesystem::path& Root,
    const PackageOptions& Settings, std::string& Error);
}
