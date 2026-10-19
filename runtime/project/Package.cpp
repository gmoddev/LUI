#include "Package.h"
#include "Files.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>

namespace Lui::Project {
using Json = nlohmann::json;
using namespace Internal;
namespace {
constexpr const char* IndexName = "lui.project-index.json";
void Check(const std::function<bool()>& Canceled) { if (Canceled && Canceled()) Fail("Canceled"); }
void Charge(size_t& Total, size_t Bytes, size_t Maximum) {
    if (Total > Maximum || Bytes > Maximum - Total) Fail("LimitExceeded");
    Total += Bytes;
}
void Message(std::string& Error, const Failure& Reason) { Error = std::string("[LUI:Project] ") + Reason.what(); }
}
struct PackageCapture {
    static std::shared_ptr<const PackageSnapshot> Read(const Anchor& Input, const std::string& ManifestName,
        const PackageOptions& Settings, std::string& Error, bool Staged = false) {
        const PackageOptions Defaults;
        if (!Settings.AssetBytes || !Settings.TotalAssetBytes || !Settings.IndexBytes ||
            Settings.AssetBytes > Defaults.AssetBytes || Settings.TotalAssetBytes > Defaults.TotalAssetBytes ||
            Settings.IndexBytes > Defaults.IndexBytes) Fail("InvalidOptions");
        std::map<std::string, std::string> CapturedMetadata;
        auto Sources = Internal::PrepareSources(Input, ManifestName, Settings.Preparation, Error, &CapturedMetadata);
        if (!Sources) return {};
        auto Result = std::shared_ptr<PackageSnapshot>(new PackageSnapshot);
        const auto Descriptor = Json::parse(Sources->GetDescriptor());
        const auto& Manifest = Descriptor.at("Manifest");
        PathIndex Paths; Paths.Add("lui.json"); Paths.Add(IndexName);
        for (const auto& Pair : Sources->GetSources()) Paths.Add(Pair.second.StagedPath);
        for (const auto& Name : Manifest.at("Assets")) Paths.Add(Name.get<std::string>());
        for (const auto& Pair : Sources->GetDeclarations().Files) Paths.Add(Pair.first);
        for(const auto& Pair:Sources->GetNativeBinaries()) Paths.Add(Pair.first);
        if (Manifest.at("Modules").contains("Lock")) Paths.Add(Manifest.at("Modules").at("Lock").get<std::string>());
        Result->Prepared = Compile(Sources, Settings.Preparation, Error);
        if (!Result->Prepared) return {};
        Result->RetainedBytes = Result->Prepared->GetRetainedBytes();
        const auto Maximum = Settings.Preparation.Bounds.RetainedBytes;
        Charge(Result->RetainedBytes, sizeof(PackageSnapshot), Maximum);
        Json Metadata = Json::array(), Assets = Json::array(), SourceFiles = Json::array();
        auto Add = [&](const std::string& Path, std::string Bytes, Json& List) {
            PackageFile File{std::move(Bytes), {}}; File.Hash = Hash(File.Bytes);
            Charge(Result->RetainedBytes, sizeof(PackageFile) + 512 + Path.capacity() + 1 +
                File.Bytes.capacity() + 1 + File.Hash.capacity() + 1, Maximum);
            List.push_back({{"Path",Path},{"Sha256",File.Hash},{"Bytes",File.Bytes.size()}});
            Result->Files.emplace(Path, std::move(File));
        };
        Add("lui.json", Manifest.dump(), Metadata);
        if (Manifest.at("Modules").contains("Lock"))
            Add(Manifest.at("Modules").at("Lock").get<std::string>(), Descriptor.at("Lock").dump(), Metadata);
        if (Staged) for (const auto& Pair : Result->Files)
            if (CapturedMetadata.at(Pair.first) != Pair.second.Bytes) Fail("PackageMismatch");
        for (const auto& Pair : Sources->GetDeclarations().Files) Add(Pair.first,Pair.second.Bytes,Metadata);
        size_t TotalAssets = 0; std::set<std::string> Physical;
        for (const auto& Name : Manifest.at("Assets")) {
            Check(Settings.Preparation.Canceled);
            const auto Path = Name.get<std::string>();
            auto File = Input.Read(Path, std::min(Settings.AssetBytes, Settings.TotalAssetBytes - TotalAssets), Settings.Preparation.Canceled);
            if (!Physical.insert(File.Identity).second) Fail("DuplicateIdentity");
            TotalAssets += File.Bytes.size();
            Add(Path, std::move(File.Bytes), Assets);
        }
        for (const auto& Pair : Sources->GetSources())
            SourceFiles.push_back({{"Id",Pair.first},{"Path",Pair.second.StagedPath},{"Sha256",Pair.second.Hash},{"Bytes",Pair.second.Bytes.size()}});
        std::sort(Metadata.begin(), Metadata.end(), [](const Json& A, const Json& B) { return A.at("Path") < B.at("Path"); });
        // Provenance is retained without inventing license or download authority.
        Json Dependencies = Json::array();
        const auto& Lock = Descriptor.at("Lock");
        if (Lock.contains("Packages")) for (const auto& Item : Lock.at("Packages"))
            Dependencies.push_back({{"Alias",Item.at("Alias")},{"Provenance",Item.at("Provenance")}});
        Json Index{{"SchemaVersion",1},{"Profile","DeclaredContentV1"},{"Manifest","lui.json"},
            {"Entry",Sources->GetEntry()},{"SourceDigest",Sources->GetDigest()},
            {"CompiledDigest",Result->Prepared->GetDigest()},{"LuauRevision",Descriptor.at("LuauRevision")},
            {"Sources",SourceFiles},{"Assets",Assets},{"Metadata",Metadata},{"Dependencies",Dependencies}};
        if(!Sources->GetNativeBinaries().empty()) {
            Index["SchemaVersion"]=2; Index["Profile"]="DeclaredContentV2"; Index["NativeBindings"]=Descriptor.at("NativeBindings");
        }
        Result->Index=Index.dump();
        if (Result->Index.size() > Settings.IndexBytes) Fail("LimitExceeded");
        Result->Digest = Hash(Result->Index);
        Charge(Result->RetainedBytes, Result->Index.capacity() + Result->Digest.capacity() + 2, Maximum);
        Check(Settings.Preparation.Canceled);
        return Result;
    }
};
std::shared_ptr<const PackageSnapshot> CapturePackage(const std::filesystem::path& Manifest,
    const PackageOptions& Settings, std::string& Error) {
    Error.clear();
    try {
        Check(Settings.Preparation.Canceled);
        const auto Absolute = std::filesystem::absolute(Manifest);
        Anchor Input(Absolute.parent_path());
        return PackageCapture::Read(Input, Absolute.filename().u8string(), Settings, Error);
    } catch (const Failure& Reason) { Message(Error, Reason); }
    catch (const std::bad_alloc&) { Error = "[LUI:Project] LimitExceeded"; }
    catch (...) { Error = "[LUI:Project] InvalidInput"; }
    return {};
}
bool StagePackage(const PackageSnapshot& Package, const std::filesystem::path& Output,
    const std::function<bool()>& Canceled, std::string& Error) {
    Error.clear();
    try {
        Check(Canceled);
        // create_directory, not create_directories: no reuse of caller data or
        // surprise creation outside the explicitly supplied output directory.
        if (!std::filesystem::create_directory(Output)) Fail("OutputExists");
        Anchor Target(Output);
        for (const auto& Pair : Package.GetFiles()) { Check(Canceled); Target.Write(Pair.first, Pair.second.Bytes); }
        for (const auto& Pair : Package.GetPrepared().GetSources().GetSources()) {
            Check(Canceled); Target.Write(Pair.second.StagedPath, Pair.second.Bytes);
        }
        for(const auto& Pair:Package.GetPrepared().GetSources().GetNativeBinaries()) {
            Check(Canceled); Target.Write(Pair.first,Pair.second.Bytes);
        }
        // Recheck the anchored bytes before publishing the commit marker.
        for (const auto& Pair : Package.GetFiles()) {
            const auto File = Target.Read(Pair.first, Pair.second.Bytes.size(), Canceled);
            if (Hash(File.Bytes) != Pair.second.Hash) Fail("PackageMismatch");
        }
        for (const auto& Pair : Package.GetPrepared().GetSources().GetSources()) {
            const auto File = Target.Read(Pair.second.StagedPath, Pair.second.Bytes.size(), Canceled);
            if (Hash(File.Bytes) != Pair.second.Hash) Fail("PackageMismatch");
        }
        for(const auto& Pair:Package.GetPrepared().GetSources().GetNativeBinaries()) {
            const auto File=Target.Read(Pair.first,Pair.second.Bytes.size(),Canceled);
            if(Hash(File.Bytes)!=Pair.second.Hash) Fail("BinaryMismatch");
        }
        Check(Canceled); Target.Write(IndexName, Package.GetIndex());
        return true;
    } catch (const Failure& Reason) { Message(Error, Reason); }
    catch (const std::bad_alloc&) { Error = "[LUI:Project] LimitExceeded"; }
    catch (...) { Error = "[LUI:Project] IoError"; }
    return false;
}
std::shared_ptr<const PackageSnapshot> VerifyPackage(const std::filesystem::path& Root,
    const PackageOptions& Settings, std::string& Error) {
    Error.clear();
    try {
        Check(Settings.Preparation.Canceled);
        if (!Settings.IndexBytes || Settings.IndexBytes > PackageOptions{}.IndexBytes) Fail("InvalidOptions");
        Anchor Input(Root);
        const auto Index = Input.Read(IndexName, Settings.IndexBytes, Settings.Preparation.Canceled);
        auto Result = PackageCapture::Read(Input, "lui.json", Settings, Error, true);
        if (!Result) return {};
        // Index fields never direct IO. The shared manifest/resolver owns paths;
        // exact canonical comparison validates shape, ordering, hashes, profile,
        // compiler pin, metadata and provenance together.
        if (Result->GetIndex() != Index.Bytes) Fail("PackageMismatch");
        return Result;
    } catch (const Failure& Reason) { Message(Error, Reason); }
    catch (const std::bad_alloc&) { Error = "[LUI:Project] LimitExceeded"; }
    catch (...) { Error = "[LUI:Project] InvalidInput"; }
    return {};
}
}
