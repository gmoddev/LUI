#include "Project.h"
#include "Files.h"
#include "Json.h"
#include "../../native/abi/LuiExtension.h"
#include "../../native/abi/LuiApplicationServices.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <set>

namespace Lui::Project {
using Json = nlohmann::json;
using namespace Internal;
namespace {
Json Parse(std::string_view Text) {
    return ParseJson(Text);
}
void Shape(const Json& Object, std::initializer_list<const char*> Required, std::initializer_list<const char*> Optional = {}) {
    if (!Object.is_object()) Fail("InvalidManifest");
    std::set<std::string> Names;
    for (const char* Name : Required) { Names.insert(Name); if (!Object.contains(Name)) Fail("InvalidManifest"); }
    for (const char* Name : Optional) Names.insert(Name);
    for (auto Item = Object.begin(); Item != Object.end(); ++Item) if (!Names.count(Item.key())) Fail("InvalidManifest");
}
std::string Text(const Json& Value, size_t Maximum = 512) {
    if (!Value.is_string()) Fail("InvalidManifest");
    auto Result = Value.get<std::string>();
    if (Result.empty() || Result.size() > Maximum || Result.find('\0') != std::string::npos) Fail("InvalidManifest");
    return Result;
}
uint64_t Number(const Json& Value) {
    if (!Value.is_number_unsigned() && (!Value.is_number_integer() || Value.get<int64_t>() < 0)) Fail("InvalidManifest");
    return Value.get<uint64_t>();
}
bool Alias(const std::string& Value) {
    if (Value.empty() || Value.size() > 64 || !(Value[0] == '_' ||
        (Value[0] >= 'A' && Value[0] <= 'Z') || (Value[0] >= 'a' && Value[0] <= 'z'))) return false;
    for (char Character : Value) if (!(Character == '_' || (Character >= 'A' && Character <= 'Z') ||
        (Character >= 'a' && Character <= 'z') || (Character >= '0' && Character <= '9'))) return false;
    return true;
}
std::vector<std::string> Strings(const Json& Value, size_t Maximum, bool CaseFold = false) {
    if (!Value.is_array()) Fail("InvalidManifest");
    if (Value.size() > Maximum) Fail("LimitExceeded");
    std::vector<std::string> Result;
    std::set<std::string> Unique;
    for (const auto& Item : Value) {
        auto Name = Text(Item);
        if (!Unique.insert(CaseFold ? Fold(Name) : Name).second) Fail("DuplicateIdentity");
        Result.push_back(std::move(Name));
    }
    std::sort(Result.begin(), Result.end()); return Result;
}
std::string Relative(const std::string& Root, const std::string& Path) { return Root == "." ? Path : Root + "/" + Path; }
bool ContainsRoot(const std::string& Root, const std::string& Path) {
    return Root == "." || Path == Root || Path.substr(0, Root.size() + 1) == Root + "/";
}
void CheckLegacyFields(Json& Manifest) {
    const auto& Input = Manifest["Capabilities"];
    if (!Input.is_array() || Input.size() > 32) Fail("InvalidManifest");
    std::vector<std::string> Capabilities;
    const std::set<std::string> Allowed{"NativeExtensions","HostServices","Clipboard","Dialogs","network.client","network.server","network.raw","Storage","Notifications","Tray"};
    std::set<std::string> Seen;
    for (const auto& Value : Input) {
        if (!Value.is_string() || !Allowed.count(Value.get<std::string>())) Fail("UnknownCapability");
        auto Capability = Value.get<std::string>();
        if (!Seen.insert(Capability).second) Fail("DuplicateCapability");
        Capabilities.push_back(std::move(Capability));
    }
    std::sort(Capabilities.begin(), Capabilities.end());
    Manifest["Capabilities"] = Capabilities;
    auto Extensions = Strings(Manifest["Extensions"], 32, true);
    for(const auto& Extension:Extensions) if(!Alias(Extension) || Extension=="Host") Fail("InvalidManifest");
    Manifest["Extensions"] = Extensions;
    auto Declarations = Strings(Manifest["ExtensionDeclarations"], 16, true);
    for (const auto& Path : Declarations)
        if (!PathValid(Path) || Path.size() < 6 || Path.substr(Path.size() - 5) != ".json") Fail("InvalidPath");
    if(!Declarations.empty() && !std::binary_search(Capabilities.begin(),Capabilities.end(),"HostServices") &&
        !std::binary_search(Capabilities.begin(),Capabilities.end(),"NativeExtensions")) Fail("InvalidManifest");
    Manifest["ExtensionDeclarations"] = Declarations;
    auto Assets = Strings(Manifest["Assets"], 256, true);
    for (const auto& Asset : Assets) {
        if (Asset.size() > 128 || Asset.find('/') != std::string::npos || !PathValid(Asset)) Fail("InvalidPath");
    }
    Manifest["Assets"] = Assets;
    if (Manifest.contains("Sandbox")) {
        auto& Sandbox = Manifest["Sandbox"];
        Shape(Sandbox, {"MaxMemoryBytes","MaxInterrupts"});
        if (Number(Sandbox["MaxMemoryBytes"]) < 1024 * 1024 || !Number(Sandbox["MaxInterrupts"]) ||
            std::binary_search(Capabilities.begin(), Capabilities.end(), "NativeExtensions")) Fail("InvalidManifest");
    }
    if (Manifest.contains("NetworkPolicy")) {
        auto& Policy = Manifest["NetworkPolicy"];
        Shape(Policy, {"ClientLoopbackOnly","ServerLoopbackOnly","ClientPortMin","ClientPortMax","ServerPortMin","ServerPortMax"});
        if (!Policy["ClientLoopbackOnly"].is_boolean() || !Policy["ServerLoopbackOnly"].is_boolean()) Fail("InvalidManifest");
        for (const auto& Prefix : {std::string("Client"), std::string("Server")}) {
            auto Minimum = Number(Policy[Prefix + "PortMin"]), Maximum = Number(Policy[Prefix + "PortMax"]);
            if (!((!Minimum && !Maximum) || (Minimum >= 1 && Minimum <= Maximum && Maximum <= 65535))) Fail("InvalidManifest");
        }
    }
    Shape(Manifest["Diagnostics"], {"Mode"});
    auto Mode = Text(Manifest["Diagnostics"]["Mode"], 32);
    if (Mode != "Development" && Mode != "Restricted") Fail("InvalidManifest");
}
struct Package { Owner Data; std::vector<std::string> Files; };
Package ReadPackage(Json& Object, const std::string& Id, size_t Maximum) {
    Package Result;
    Result.Data.Id = Id;
    Result.Data.Root = Text(Object["Root"]);
    if (!PathValid(Result.Data.Root, true)) Fail("InvalidPath");
    Result.Files = Strings(Object["Files"], Maximum, true);
    if (Result.Files.empty()) Fail("InvalidManifest");
    for (const auto& File : Result.Files) if (!PathValid(File) || File.size() < 6 || File.substr(File.size() - 5) != ".luau") Fail("InvalidPath");
    Result.Data.Requires = Strings(Object["Requires"], 32, true);
    for (const auto& Name : Result.Data.Requires) if (!Alias(Name)) Fail("InvalidManifest");
    Object["Files"] = Result.Files; Object["Requires"] = Result.Data.Requires;
    return Result;
}
void Visit(const std::string& Id, const std::map<std::string, Package>& Packages, std::map<std::string,int>& States) {
    if (States[Id] == 1) Fail("Cycle");
    if (States[Id] == 2) return;
    States[Id] = 1;
    for (const auto& Alias : Packages.at(Id).Data.Requires) {
        auto Target = "dep:" + Alias;
        if (!Packages.count(Target)) Fail("UnknownAlias");
        Visit(Target, Packages, States);
    }
    States[Id] = 2;
}
bool Hex(const std::string& Value, size_t Size) {
    if (Value.size() != Size) return false;
    return std::all_of(Value.begin(), Value.end(), [](char Character) { return (Character >= '0' && Character <= '9') || (Character >= 'a' && Character <= 'f'); });
}
}

std::shared_ptr<const Snapshot> Internal::PrepareSources(const SourceReader& Files, const std::string& ManifestName,
    const Options& Settings, std::string& Error, std::map<std::string, std::string>* Metadata) {
    Error.clear();
    try {
        const auto& Bounds = Settings.Bounds;
        if (Bounds.ManifestBytes > 256 * 1024 || Bounds.LockBytes > 512 * 1024 || Bounds.Owners > 33 ||
            Bounds.Files > 256 || Bounds.FileBytes > 8 * 1024 * 1024 || Bounds.SourceBytes > 32 * 1024 * 1024 ||
            !Bounds.BinaryBytes || !Bounds.NativeBytes || Bounds.BinaryBytes>16*1024*1024 || Bounds.NativeBytes>64*1024*1024)
            Fail("InvalidOptions");
        if (Settings.Canceled && Settings.Canceled()) Fail("Canceled");
        std::map<std::string, std::string> Captured;
        Captured.emplace(ManifestName, Files.Read(ManifestName, Bounds.ManifestBytes, Settings.Canceled).Bytes);
        Json Manifest = Parse(Captured.at(ManifestName));
        Shape(Manifest, {"SchemaVersion","Script","Capabilities","Extensions","Assets","Modules","ExtensionDeclarations","Diagnostics"},
            {"Sandbox","NetworkPolicy","Storage"});
        if (Number(Manifest["SchemaVersion"]) != 2) Fail("InvalidSchema");
        auto Script = Text(Manifest["Script"]);
        if (!PathValid(Script)) Fail("InvalidPath");
        CheckLegacyFields(Manifest);
        if (Manifest.contains("Storage")) {
            Shape(Manifest["Storage"], {"ApplicationId"});
            const auto Id = Text(Manifest["Storage"]["ApplicationId"], 64);
            if (Id.empty() || Id.front() < 'a' || Id.front() > 'z' ||
                !std::all_of(Id.begin(), Id.end(), [](char Byte) {
                    return (Byte >= 'a' && Byte <= 'z') || (Byte >= '0' && Byte <= '9') || Byte == '.' || Byte == '_' || Byte == '-';
                }) || std::find(Manifest["Capabilities"].begin(), Manifest["Capabilities"].end(), "Storage") == Manifest["Capabilities"].end()) Fail("InvalidManifest");
        }
        auto& Modules = Manifest["Modules"];
        Shape(Modules, {"Root","Files","Requires","Dependencies"}, {"Lock"});
        if (!Modules["Dependencies"].is_array()) Fail("InvalidManifest");
        if (Modules["Dependencies"].size() + 1 > Bounds.Owners) Fail("LimitExceeded");
        std::map<std::string, Package> Packages;
        Packages.emplace("app", ReadPackage(Modules, "app", Bounds.Files));
        std::set<std::string> AliasNames;
        for (auto& Item : Modules["Dependencies"]) {
            Shape(Item, {"Alias","Root","Files","Requires"});
            auto Name = Text(Item["Alias"], 64);
            if (!Alias(Name) || !AliasNames.insert(Fold(Name)).second) Fail("DuplicateIdentity");
            Packages.emplace("dep:" + Name, ReadPackage(Item, "dep:" + Name, Bounds.Files));
        }
        auto& Dependencies = Modules["Dependencies"];
        std::sort(Dependencies.begin(), Dependencies.end(), [](const Json& Left, const Json& Right) { return Left["Alias"] < Right["Alias"]; });
        size_t Count = 0;
        for (const auto& Pair : Packages) {
            Count += Pair.second.Files.size();
            if (Count > Bounds.Files) Fail("LimitExceeded");
            for (const auto& Other : Packages) if (Pair.first != Other.first &&
                ContainsRoot(Fold(Pair.second.Data.Root), Fold(Other.second.Data.Root))) Fail("DuplicateIdentity");
        }
        std::map<std::string,int> States;
        for (const auto& Pair : Packages) Visit(Pair.first, Packages, States);
        // Validate the entire portable path tree before opening source files.
        // Full-filename folding alone misses Foo/A versus foo/B on Linux.
        PathIndex Paths; Paths.Add(ManifestName);
        if (Modules.contains("Lock")) Paths.Add(Text(Modules["Lock"]));
        for (const auto& Asset : Manifest["Assets"]) Paths.Add(Asset.get<std::string>());
        for (const auto& Path : Manifest["ExtensionDeclarations"]) Paths.Add(Path.get<std::string>());
        for (const auto& Pair : Packages)
            for (const auto& Path : Pair.second.Files) Paths.Add(Relative(Pair.second.Data.Root, Path));
        for (const auto& Asset : Manifest["Assets"]) Files.Validate(Asset.get<std::string>());
        auto Result = std::make_shared<Snapshot>();
        std::set<std::string> Physical;
        size_t DeclarationBytes = 0;
        for (const auto& Path : Manifest["ExtensionDeclarations"]) {
            if (Settings.Canceled && Settings.Canceled()) Fail("Canceled");
            const auto Name = Path.get<std::string>();
            auto File = Files.Read(Name,std::min<size_t>(256 * 1024,1024 * 1024 - DeclarationBytes),Settings.Canceled);
            if (!Physical.insert(File.Identity).second) Fail("DuplicateIdentity");
            DeclarationBytes += File.Bytes.size(); AddDeclaration(Result->Declarations,Name,std::move(File.Bytes));
        }
        if (!Result->Declarations.Files.empty()) FinishDeclarations(Result->Declarations);
        const auto& Declared=Result->Declarations;
        const auto CapNames=Manifest.at("Capabilities").get<std::vector<std::string>>();
        for(const auto& Pair:Declared.ServiceProviders)
            if(Pair.second=="Host" && !std::binary_search(CapNames.begin(),CapNames.end(),"HostServices")) Fail("InvalidManifest");
        std::vector<std::string> ProviderNames;
        const std::map<std::string,uint64_t> Bits{{"NativeExtensions",LUI_CAPABILITY_NATIVE_EXTENSIONS},{"HostServices",LUI_CAPABILITY_HOST_SERVICES},
            {"Clipboard",LUI_CAPABILITY_CLIPBOARD},{"Dialogs",LUI_CAPABILITY_DIALOGS},{"network.client",LUI_CAPABILITY_NETWORK_CLIENT},
            {"network.server",LUI_CAPABILITY_NETWORK_SERVER},{"network.raw",LUI_CAPABILITY_NETWORK_RAW},
            {"Storage",LUI_CAPABILITY_STORAGE},{"Notifications",LUI_CAPABILITY_NOTIFICATIONS},{"Tray",LUI_CAPABILITY_TRAY}};
        uint64_t Requested=0; for(const auto& Name:CapNames) Requested|=Bits.at(Name);
        // Preflight every binding path before opening a binary, so collisions
        // and ownership aliases cannot change across platforms.
        for(const auto& Pair:Declared.NativeProviders) {
            ProviderNames.push_back(Pair.first);
            if(Pair.second.RequiredCapabilities & ~Requested) Fail("CapabilityMismatch");
            for(const auto& Binding:Pair.second.Bindings) Paths.Add(Binding.second.Path);
        }
        if(ProviderNames!=Manifest.at("Extensions").get<std::vector<std::string>>()) Fail("InvalidManifest");
        size_t NativeBytes=0;
        for(const auto& Pair:Declared.NativeProviders) for(const auto& Binding:Pair.second.Bindings) {
            if(Settings.Canceled && Settings.Canceled()) Fail("Canceled");
            auto File=Files.Read(Binding.second.Path,std::min(Bounds.BinaryBytes,Bounds.NativeBytes-NativeBytes),Settings.Canceled);
            if(!Physical.insert(File.Identity).second) Fail("DuplicateIdentity");
            if(File.Bytes.empty()) Fail("BinaryMismatch");
            const auto Digest=Hash(File.Bytes); if(Digest!=Binding.second.Hash) Fail("BinaryMismatch");
            NativeBytes+=File.Bytes.size();
            Result->NativeBinaries.emplace(Binding.second.Path,NativeBinary{Pair.first,Binding.first,Binding.second.Path,Digest,std::move(File.Bytes)});
        }
        size_t SourceBytes = 0;
        Json SourceHashes = Json::object();
        for (const auto& Pair : Packages) {
            Result->Owners.emplace(Pair.first, Pair.second.Data);
            for (const auto& Path : Pair.second.Files) {
                if (Settings.Canceled && Settings.Canceled()) Fail("Canceled");
                Source Value;
                Value.Owner = Pair.first; Value.Path = Path; Value.Id = Pair.first + ":" + Path;
                // Dependency IDs use dep:Alias/Path, not a second colon.
                if (Pair.first != "app") Value.Id = Pair.first + "/" + Path;
                Value.StagedPath = Relative(Pair.second.Data.Root, Path);
                auto Input = Files.Read(Value.StagedPath, std::min(Bounds.FileBytes, Bounds.SourceBytes - SourceBytes), Settings.Canceled);
                if (!Physical.insert(Input.Identity).second) Fail("DuplicateIdentity");
                if (!Utf8(Input.Bytes)) Fail("InvalidSource");
                SourceBytes += Input.Bytes.size();
                Value.Bytes = std::move(Input.Bytes); Value.Hash = Hash(Value.Bytes);
                SourceHashes[Value.Id] = Value.Hash;
                if (Pair.first == "app" && Value.StagedPath == Script) Result->Entry = Value.Id;
                Result->Sources.emplace(Value.Id, std::move(Value));
            }
        }
        if (Result->Entry.empty()) Fail("InvalidManifest");
        Json Lock = Json::object();
        if (Packages.size() > 1 && !Modules.contains("Lock")) Fail("InvalidLock");
        if (Modules.contains("Lock")) {
            auto Path = Text(Modules["Lock"]);
            if (!PathValid(Path)) Fail("InvalidPath");
            for (const auto& Pair : Packages) if (ContainsRoot(Pair.second.Data.Root, Path)) Fail("InvalidLock");
            Captured.emplace(Path, Files.Read(Path, Bounds.LockBytes, Settings.Canceled).Bytes);
            Lock = Parse(Captured.at(Path));
            Shape(Lock, {"SchemaVersion","Packages"});
            if (Number(Lock["SchemaVersion"]) != 1 || !Lock["Packages"].is_array() || Lock["Packages"].size() != Packages.size() - 1)
                Fail("InvalidLock");
            std::set<std::string> Seen;
            for (auto& Item : Lock["Packages"]) {
                Shape(Item, {"Alias","Provenance","Files"});
                auto Name = Text(Item["Alias"], 64), Id = "dep:" + Name;
                if (!Packages.count(Id) || !Seen.insert(Id).second) Fail("InvalidLock");
                Shape(Item["Provenance"], {"Kind","Id"});
                auto Kind = Text(Item["Provenance"]["Kind"], 32), Revision = Text(Item["Provenance"]["Id"], 128);
                if (Kind != "LocalVendor" && Kind != "Git") Fail("InvalidLock");
                if (Kind == "Git" && !Hex(Revision, 40) && !Hex(Revision, 64)) Fail("InvalidLock");
                for (unsigned char Character : Revision) if (Character < 32 || Character > 126) Fail("InvalidLock");
                if (!Item["Files"].is_array() || Item["Files"].size() != Packages.at(Id).Files.size()) Fail("InvalidLock");
                std::set<std::string> FileNames;
                for (const auto& File : Item["Files"]) {
                    Shape(File, {"Path","Sha256"});
                    auto PathValue = Text(File["Path"]), Digest = Text(File["Sha256"], 64);
                    auto Found = Result->Sources.find(Id + "/" + PathValue);
                    if (!FileNames.insert(PathValue).second || Found == Result->Sources.end() || !Hex(Digest,64)) Fail("InvalidLock");
                    if (Found->second.Hash != Digest) Fail("DependencyMismatch");
                }
                std::sort(Item["Files"].begin(), Item["Files"].end(), [](const Json& Left, const Json& Right) { return Left["Path"] < Right["Path"]; });
            }
            std::sort(Lock["Packages"].begin(), Lock["Packages"].end(), [](const Json& Left, const Json& Right) { return Left["Alias"] < Right["Alias"]; });
        }
        Json Descriptor{{"PreparationVersion",1}, {"CompileProfile","SourcesOnly"},
            {"LuauRevision",LUI_PROJECT_LUAU_REVISION}, {"Manifest",Manifest}, {"Lock",Lock}, {"SourceHashes",SourceHashes}};
        if (!Result->Declarations.Files.empty()) {
            Json Hashes = Json::object(); for (const auto& Pair : Result->Declarations.Files) Hashes[Pair.first] = Pair.second.Hash;
            Descriptor["ExtensionDeclarationHashes"] = std::move(Hashes);
            Descriptor["ExtensionRegistration"] = Json::parse(Result->Declarations.Descriptor);
            Descriptor["ExtensionRegistrationDigest"] = Result->Declarations.Digest;
        }
        if(!Result->NativeBinaries.empty()) {
            Json Bindings=Json::array();
            for(const auto& Pair:Result->NativeBinaries) {
                const auto& Value=Pair.second; const auto& Provider=Declared.NativeProviders.at(Value.Provider);
                Bindings.push_back({{"Provider",Value.Provider},{"Platform",Value.Platform},{"Path",Value.Path},{"Sha256",Value.Hash},{"Bytes",Value.Bytes.size()},
                    {"Version",Provider.Version},{"RequiredCapabilities",Provider.RequiredCapabilities},
                    {"Provenance",{{"Kind",Provider.ProvenanceKind},{"Id",Provider.ProvenanceId}}}});
            }
            Descriptor["NativeBindings"]=std::move(Bindings);
        }
        Result->Descriptor = Descriptor.dump(); Result->Digest = Hash(Result->Descriptor);
        if (Metadata) *Metadata = std::move(Captured);
        return Result;
    } catch (const Failure& Reason) { Error = "[LUI:Project] " + std::string(Reason.what()); }
    catch (const std::bad_alloc&) { Error = "[LUI:Project] LimitExceeded"; }
    catch (...) { Error = "[LUI:Project] InvalidInput"; }
    return {};
}
#ifndef LUI_CAPTURED_ONLY
std::shared_ptr<const Snapshot> Prepare(const std::filesystem::path& ManifestPath, const Options& Settings, std::string& Error) {
    Error.clear();
    try {
        if (Settings.Canceled && Settings.Canceled()) Fail("Canceled");
        const auto Absolute = std::filesystem::absolute(ManifestPath);
        Anchor Files(Absolute.parent_path());
        return Internal::PrepareSources(Files, Absolute.filename().u8string(), Settings, Error);
    } catch (const Failure& Reason) { Error = "[LUI:Project] " + std::string(Reason.what()); }
    catch (const std::bad_alloc&) { Error = "[LUI:Project] LimitExceeded"; }
    catch (...) { Error = "[LUI:Project] InvalidInput"; }
    return {};
}

#endif
const Source* Snapshot::GetSource(std::string_view Id) const {
    auto Found = Sources.find(std::string(Id)); return Found == Sources.end() ? nullptr : &Found->second;
}
const Source* Snapshot::Resolve(std::string_view Importer, std::string_view Request, std::string& Error) const {
    Error.clear();
    try {
        if (Importer.size() > 640 || Request.empty() || Request.size() > 640) Fail("InvalidPath");
        auto Context = GetSource(Importer);
        if (!Context) Fail("MissingModule");
        std::string OwnerId = Context->Owner;
        std::vector<std::string> Parts;
        if (Request.front() == '@') {
            auto Slash = Request.find('/');
            if (Slash == std::string_view::npos) Fail("InvalidPath");
            std::string Name(Request.substr(1, Slash - 1));
            if (!Alias(Name)) Fail("InvalidPath");
            const auto& Allowed = Owners.at(OwnerId).Requires;
            if (!std::binary_search(Allowed.begin(), Allowed.end(), Name)) Fail("ForbiddenImport");
            OwnerId = "dep:" + Name; Request.remove_prefix(Slash + 1);
        } else {
            if (Request.substr(0,2) != "./" && Request.substr(0,3) != "../") Fail("InvalidPath");
            std::string Parent = Context->Path.substr(0, Context->Path.find_last_of('/') == std::string::npos ? 0 : Context->Path.find_last_of('/'));
            size_t Start = 0;
            while (Start < Parent.size()) { auto End = Parent.find('/', Start); if (End == std::string::npos) End = Parent.size();
                Parts.push_back(Parent.substr(Start, End - Start)); Start = End + 1; }
        }
        size_t Start = 0;
        while (Start < Request.size()) {
            auto End = Request.find('/', Start); if (End == std::string_view::npos) End = Request.size();
            std::string Part(Request.substr(Start, End - Start));
            if (Part.empty()) Fail("InvalidPath");
            if (End == Request.size() && (Part == "." || Part == "..")) Fail("InvalidPath");
            if (Part == "..") { if (Parts.empty()) Fail("InvalidPath"); Parts.pop_back(); }
            else if (Part != ".") Parts.push_back(std::move(Part));
            Start = End + 1;
        }
        if (Parts.empty() || Request.empty() || Request.back() == '/') Fail("InvalidPath");
        auto& Last = Parts.back();
        if (Last.find('.') == std::string::npos) Last += ".luau";
        if (Last.size() < 6 || Last.substr(Last.size()-5) != ".luau") Fail("InvalidPath");
        std::string Path;
        for (const auto& Part : Parts) { if (!Path.empty()) Path += '/'; Path += Part; }
        if (!PathValid(Path)) Fail("InvalidPath");
        auto Id = OwnerId + (OwnerId == "app" ? ":" : "/") + Path;
        if (Id == Entry) Fail("EntryImportDenied");
        auto Found = GetSource(Id); if (!Found) Fail("MissingModule");
        return Found;
    } catch (const Failure& Reason) { Error = "[LUI:Project] " + std::string(Reason.what()); }
    catch (...) { Error = "[LUI:Project] InvalidInput"; }
    return nullptr;
}
}
