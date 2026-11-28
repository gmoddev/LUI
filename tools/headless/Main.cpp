#include "LuiExecution.h"
#include "LuiFiles.h"
#include "LuiApplicationServices.h"
#include "LuiHttp.h"
#include "LuiOwnerWake.h"
#include "LuiProject.h"
#include "Stdio.h"
#include "ProcessHost.h"
#include "Environment.h"
#include "StandardProviders.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
volatile std::sig_atomic_t Interrupted = 0;
void Interrupt(int) { Interrupted = 1; }
struct Options {
    std::string Project;
    uint64_t Grants = 0;
    uint32_t TimeoutMs = 30000, PollMs = 16;
    bool Development = false, PublicOnly = false, Help = false, Streams = false, TrustedFfiWorkers = false;
    std::vector<std::string> Arguments;
    std::string ReadFile, WriteFile, ReadDirectory, WriteDirectory, StorageRoot, DatabaseRoot;
    std::string Tool,ToolDirectory;
    bool Context=false;
    std::set<std::string> Providers,EnvironmentKeys,ChildKeys;
    std::vector<std::string> Origins;
};
uint32_t Number(const std::string& Value, uint32_t Maximum) {
    if (Value.empty() || Value.size() > 10) throw std::runtime_error("InvalidArguments");
    uint64_t Result = 0;
    for (const unsigned char Byte : Value) {
        if (Byte < '0' || Byte > '9') throw std::runtime_error("InvalidArguments");
        Result = Result * 10 + Byte - '0';
        if (Result > Maximum) throw std::runtime_error("InvalidArguments");
    }
    if (!Result) throw std::runtime_error("InvalidArguments");
    return static_cast<uint32_t>(Result);
}
uint64_t Capability(const std::string& Name) {
    if (Name == "network.client") return LUI_CAPABILITY_NETWORK_CLIENT;
    if (Name == "network.server") return LUI_CAPABILITY_NETWORK_SERVER;
    if (Name == "network.raw") return LUI_CAPABILITY_NETWORK_RAW;
    if (Name == "Storage") return LUI_CAPABILITY_STORAGE;
    throw std::runtime_error("UnsupportedCapability");
}
Options Parse(int Count, char** Arguments) {
    Options Result;
    bool Project = false, Timeout = false, Poll = false, Diagnostics = false;
    for (int Index = 1; Index < Count; ++Index) {
        const std::string Key = Arguments[Index];
        if (Key == "--trusted-ffi") {
            if (!Result.Providers.insert("Ffi").second) throw std::runtime_error("InvalidArguments");
            continue;
        }
        if (Key == "--trusted-ffi-workers" && !Result.TrustedFfiWorkers) { Result.TrustedFfiWorkers=true; continue; }
        if (Key == "--context" && !Result.Context) { Result.Context=true; continue; }
        if (Key == "--stdio" && !Result.Streams) { Result.Streams = true; continue; }
        if (Key == "--" && Result.Streams) {
            size_t Bytes = 0;
            while (++Index < Count) {
                const size_t Length = std::strlen(Arguments[Index]);
                if (Result.Arguments.size() >= 64 || Length > 4096 || (Bytes += Length) > 32768)
                    throw std::runtime_error("InvalidArguments");
                Result.Arguments.emplace_back(Arguments[Index]);
            }
            break;
        }
        if (Key == "--help") { Result.Help = true; continue; }
        if (Key == "--http-public-only") { Result.PublicOnly = true; continue; }
        if (++Index == Count) throw std::runtime_error("InvalidArguments");
        const std::string Value = Arguments[Index];
        if (Key == "--project" && !Project) { Result.Project = Value; Project = true; }
        else if (Key == "--provider") {
            if ((Value != "Compression" && Value != "Crypto" && Value != "SQLite" && Value != "Regex" && Value != "Toml") || !Result.Providers.insert(Value).second)
                throw std::runtime_error("InvalidArguments");
        }
        else if ((Key == "--read-file" || Key == "--write-file" || Key == "--database-root" || Key == "--storage-root") && !Value.empty() && Value.size() <= 4096) {
            auto& Destination = Key == "--read-file" ? Result.ReadFile : Key == "--write-file" ? Result.WriteFile : Key == "--database-root" ? Result.DatabaseRoot : Result.StorageRoot;
            if (!Destination.empty()) throw std::runtime_error("InvalidArguments");
            Destination = std::filesystem::absolute(std::filesystem::u8path(Value)).u8string();
        }
        else if (Key=="--read-directory" || Key=="--write-directory") {
            if (Value.empty() || Value.size()>4096 || !Result.ReadDirectory.empty() || !Result.WriteDirectory.empty()) throw std::runtime_error("InvalidArguments");
            auto& Path=Key=="--read-directory"?Result.ReadDirectory:Result.WriteDirectory;
            auto Selected=std::filesystem::absolute(std::filesystem::u8path(Value));
            while (Selected.has_relative_path() && (Selected.filename().empty() || Selected.filename()==".")) Selected=Selected.parent_path();
            Path=Selected.u8string();
        }
        else if (Key=="--tool" || Key=="--tool-cwd") {
            auto& Path=Key=="--tool"?Result.Tool:Result.ToolDirectory;
            if (!Path.empty() || Value.empty() || Value.size()>4096 || !std::filesystem::u8path(Value).is_absolute()) throw std::runtime_error("InvalidArguments");
            Path=Value;
        } else if (Key=="--env-key" || Key=="--child-env-key") {
            auto& Keys=Key=="--env-key"?Result.EnvironmentKeys:Result.ChildKeys;
            if (Keys.size()>=16 || !Lui::Headless::Environment::ValidKey(Value) || !Keys.insert(Value).second) throw std::runtime_error("InvalidArguments");
        }
        else if (Key == "--grant") {
            const auto Grant = Capability(Value);
            if (Result.Grants & Grant) throw std::runtime_error("InvalidArguments");
            Result.Grants |= Grant;
        } else if (Key == "--http-origin") {
            if (Result.Origins.size() >= 32 || Value.size() > 2048) throw std::runtime_error("InvalidArguments");
            Result.Origins.push_back(Value);
        } else if (Key == "--timeout-ms" && !Timeout) { Result.TimeoutMs = Number(Value, 3600000); Timeout = true; }
        else if (Key == "--poll-ms" && !Poll) { Result.PollMs = Number(Value, 1000); Poll = true; }
        else if (Key == "--diagnostics" && !Diagnostics) {
            if (Value != "development" && Value != "restricted") throw std::runtime_error("InvalidArguments");
            Result.Development = Value == "development"; Diagnostics = true;
        } else throw std::runtime_error("InvalidArguments");
    }
    if (Result.Tool.empty()!=Result.ToolDirectory.empty() || (!Result.ChildKeys.empty() && Result.Tool.empty()) ||
        (!Result.EnvironmentKeys.empty() && !Result.Context)) throw std::runtime_error("InvalidArguments");
#if defined(_WIN32)
    if (!Result.Help && Result.Project.empty()) {
        wchar_t Buffer[32768]; const auto Size=GetModuleFileNameW(nullptr,Buffer,32768);
        if (Size && Size<32768) {
            const std::filesystem::path Self(std::wstring(Buffer,Size));
            if (Self.filename()==L"Start-LUI.exe") Result.Project=(Self.parent_path()/"Application"/"lui.json").u8string();
        }
    }
#endif
    if (!Result.Help && Result.Project.empty()) throw std::runtime_error("InvalidArguments");
    if (!Result.StorageRoot.empty() && !(Result.Grants & LUI_CAPABILITY_STORAGE)) throw std::runtime_error("CapabilityDenied");
    if ((Result.Grants & LUI_CAPABILITY_NETWORK_CLIENT) && Result.Origins.empty() && !Result.PublicOnly)
        throw std::runtime_error("HttpPolicyRequired");
    return Result;
}
struct Inbox {
    std::mutex Mutex;
    std::condition_variable Changed;
    bool Pending = false, Stopped = false;
    static uint32_t LUI_CALL Request(void* Context) {
        const auto& Value = *static_cast<std::shared_ptr<Inbox>*>(Context);
        std::lock_guard<std::mutex> Lock(Value->Mutex);
        if (Value->Stopped) return 0;
        Value->Pending = true;
        Value->Changed.notify_one();
        return 1;
    }
    static void LUI_CALL Release(void* Context) { delete static_cast<std::shared_ptr<Inbox>*>(Context); }
};
struct Host {
    LuiRuntime* Runtime = nullptr;
    LuiNativeDomainV2* Domain = nullptr;
    std::shared_ptr<Inbox> Wake = std::make_shared<Inbox>();
    uint64_t Registration = 0;
    std::unique_ptr<Lui::Headless::Stdio> Streams;
    std::unique_ptr<Lui::Headless::Environment> Context;
    std::unique_ptr<Lui::Headless::ProcessHost> Processes;
    bool StreamMode = false;
    std::string ReadFile, WriteFile, ReadDirectory, WriteDirectory;
    static uint32_t LUI_CALL Pick(void* Context,uint64_t Request,uint32_t Kind,const char*) {
        auto& Owner = *static_cast<Host*>(Context);
        if (Kind == LUI_FILE_PICK_OPEN && !Owner.ReadFile.empty())
            return Lui_CompleteFilePickerV1(Owner.Runtime, Request, Owner.ReadFile.c_str(), 0);
        if (Kind == LUI_FILE_PICK_FOLDER && !Owner.WriteDirectory.empty())
            return Lui_CompleteWritableFolderV1(Owner.Runtime,Request,Owner.WriteDirectory.c_str());
        if (Kind == LUI_FILE_PICK_FOLDER && !Owner.ReadDirectory.empty())
            return Lui_CompleteFilePickerV1(Owner.Runtime,Request,Owner.ReadDirectory.c_str(),0);
        if (Kind == LUI_FILE_PICK_SAVE && !Owner.WriteFile.empty())
            return Lui_CompleteWriteOnlyPickerV1(Owner.Runtime, Request, Owner.WriteFile.c_str());
        return LUI_NATIVE_CAPABILITY_DENIED;
    }
    static void LUI_CALL CancelPicker(void*,uint64_t) {}
    bool Failed = false, Clean = false, Development = false;
    static void LUI_CALL Log(void* Context, const char* Level, const char* Message) {
        auto& Value = *static_cast<Host*>(Context);
        if (std::strcmp(Level, "Error") == 0) {
            Value.Failed = true;
            if (!Value.StreamMode) std::fprintf(stderr, "[LUI:Headless] RuntimeFailed\n");
        } else if (Value.Development && std::strcmp(Level, "Print") == 0) {
            // Explicit development prints are application disclosure. The runtime
            // already bounds each record; framework failures never echo payloads.
            std::printf("[LUI:Application] %s\n", Message);
        }
    }
    static int LUI_CALL Create(void* Context, int, const char*) {
        auto& Value = *static_cast<Host*>(Context);
        Value.Failed = true;
        if (!Value.StreamMode) std::fprintf(stderr, "[LUI:Headless] PresentationUnsupported\n");
        Lui_ReportBackendError(Value.Runtime, "[LUI:Headless] PresentationUnsupported");
        return 0;
    }
    void Close() {
        Processes.reset(); Streams.reset();
        if (!Runtime && !Domain) return;
        {
            std::lock_guard<std::mutex> Lock(Wake->Mutex);
            Wake->Stopped = true;
            Wake->Changed.notify_all();
        }
        if (Domain && Registration) Lui_SetNativeWakeV1(Domain, nullptr, nullptr);
        Registration = 0;
        if (Runtime) { Lui_Destroy(Runtime); Runtime = nullptr; }
        if (Domain) {
            LuiNativeStatsV2 Stats{sizeof(Stats), 2};
            const auto Limit = Clock::now() + std::chrono::seconds(2);
            do {
                Lui_PumpNativeDomainV2(Domain);
                if (Lui_GetNativeStatsV2(Domain, &Stats) != LUI_NATIVE_OK) break;
                Clean = !Stats.Bytes && !Stats.ReservedBytes && !Stats.Requests && !Stats.Handles &&
                    !Stats.Sessions && !Stats.RetiringSessions && !Stats.Leases && !Stats.Events;
                if (Clean || !Stats.RetiringSessions) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (Clock::now() < Limit);
            Lui_ReleaseNativeDomainV2(Domain); Domain = nullptr;
        }
    }
    ~Host() { Close(); }
};
void Configure(Host& Value, const Options& Settings, const Json& Manifest, LuiProject* Project) {
    Value.Development = Settings.Development && !Settings.Streams;
    Value.StreamMode = Settings.Streams;
    const bool HasHost=Settings.Streams || Settings.Context || !Settings.Tool.empty();
    const bool Declared = !Manifest.at("ExtensionDeclarations").empty();
    if ((!Manifest.at("Extensions").empty() || (Declared && !HasHost)) && Settings.Providers.empty())
        throw std::runtime_error("NativeProvidersUnsupported");
    uint64_t Requested = 0;
    for (const auto& Name : Manifest.at("Capabilities"))
        Requested |= Name == "NativeExtensions" ? LUI_CAPABILITY_NATIVE_EXTENSIONS : Name == "HostServices" ? LUI_CAPABILITY_HOST_SERVICES :
            Name == "Dialogs" ? LUI_CAPABILITY_DIALOGS : Capability(Name.get<std::string>());
    const uint64_t Authorized = Settings.Grants | (HasHost ? LUI_CAPABILITY_HOST_SERVICES : 0) |
        (!Settings.ReadFile.empty() || !Settings.WriteFile.empty() || !Settings.ReadDirectory.empty() || !Settings.WriteDirectory.empty() ? LUI_CAPABILITY_DIALOGS : 0) | (!Settings.Providers.empty()?LUI_CAPABILITY_NATIVE_EXTENSIONS:0) |
        (!Settings.Providers.empty() ? LUI_CAPABILITY_HOST_SERVICES : 0);
    if ((Requested & ~Authorized) || (HasHost && (!Declared || !(Requested & LUI_CAPABILITY_HOST_SERVICES))))
        throw std::runtime_error("CapabilityDenied");
    LuiDiagnosticPolicyV1 Policy{sizeof(Policy), 1,
        Settings.Development ? LUI_DIAGNOSTICS_DEVELOPMENT : LUI_DIAGNOSTICS_RESTRICTED, 0};
    Value.Runtime = Lui_CreateWithPolicy(&Policy);
    if (!Value.Runtime) throw std::runtime_error("RuntimeUnavailable");
    Lui_SetLogCallback(Value.Runtime, &Value, Host::Log);
    Lui_SetBackendName(Value.Runtime, "headless");
    LuiBackendCallbacks Backend{}; Backend.Context = &Value; Backend.Create = Host::Create;
    Lui_SetBackend(Value.Runtime, Backend);
    LuiCapabilityDeclarationV1 Grants{sizeof(Grants), 1, Authorized};
    if (!Lui_DeclareCapabilities(Value.Runtime, &Grants)) throw std::runtime_error("CapabilityDenied");
    if (Requested & LUI_CAPABILITY_DIALOGS) {
        Value.ReadFile = Settings.ReadFile;
        Value.WriteFile = Settings.WriteFile;
        Value.ReadDirectory=Settings.ReadDirectory; Value.WriteDirectory=Settings.WriteDirectory;
        LuiFilePickerCallbacksV1 Picker{sizeof(Picker),1,&Value,Host::Pick,Host::CancelPicker};
        if (Lui_InstallFilesV1(Value.Runtime,&Picker)) throw std::runtime_error("FileServiceUnavailable");
    }
    if (Requested & LUI_CAPABILITY_STORAGE) {
        std::string Root = Settings.StorageRoot;
        if (Root.empty()) {
#if defined(_WIN32)
            const auto* Local = _wgetenv(L"LOCALAPPDATA");
            if (Local) Root = std::filesystem::path(Local).u8string();
#else
            const auto* Home = std::getenv("HOME");
            if (Home) Root = Home;
#endif
        }
        const auto AppRoot = std::filesystem::absolute(std::filesystem::u8path(Settings.Project)).parent_path().u8string();
        if (Root.empty() || Lui_InstallProjectStorageV1(Value.Runtime,Project,Root.c_str(),AppRoot.c_str()))
            throw std::runtime_error("StorageUnavailable");
    }
    auto Notify=[Wake=Value.Wake] {
        std::lock_guard<std::mutex> Lock(Wake->Mutex);
        Wake->Pending=true; Wake->Changed.notify_one();
    };
    if (Settings.Streams) Value.Streams=std::make_unique<Lui::Headless::Stdio>(Value.Runtime,Settings.Arguments,Notify);
    if (Settings.Context) Value.Context=std::make_unique<Lui::Headless::Environment>(Value.Runtime,Settings.EnvironmentKeys);
    if (!Settings.Tool.empty()) Value.Processes=std::make_unique<Lui::Headless::ProcessHost>(Value.Runtime,Settings.Tool,Settings.ToolDirectory,Settings.ChildKeys,Notify);
    for (const auto& Name : Manifest.at("Assets"))
        if (!Lui_RegisterAsset(Value.Runtime, Name.get_ref<const std::string&>().c_str())) throw std::runtime_error("InvalidAssets");
    if (Manifest.contains("NetworkPolicy")) {
        const auto& Input = Manifest.at("NetworkPolicy");
        LuiNetworkPolicyV1 Network{sizeof(Network), 1,
            (Input.at("ClientLoopbackOnly").get<bool>() ? LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY : 0u) |
            (Input.at("ServerLoopbackOnly").get<bool>() ? LUI_NETWORK_POLICY_SERVER_LOOPBACK_ONLY : 0u),
            Input.at("ClientPortMin").get<uint32_t>(), Input.at("ClientPortMax").get<uint32_t>(),
            Input.at("ServerPortMin").get<uint32_t>(), Input.at("ServerPortMax").get<uint32_t>()};
        if (!Lui_SetNetworkPolicy(Value.Runtime, &Network)) throw std::runtime_error("InvalidNetworkPolicy");
    }
    std::vector<LuiHttpOriginV1> Origins;
    for (const auto& Origin : Settings.Origins) Origins.push_back({Origin.c_str(), static_cast<uint32_t>(Origin.size()), 0});
    LuiHttpOptionsV1 Http{sizeof(Http), 1, Settings.PublicOnly ? LUI_HTTP_PUBLIC_ONLY : 0u,
        1024 * 1024, 1024 * 1024, 8, 64ull * 1024 * 1024, Origins.data(), static_cast<uint32_t>(Origins.size()), 0};
    if (!Lui_SetHttpOptionsV1(Value.Runtime, &Http)) throw std::runtime_error("InvalidHttpPolicy");
    Value.Domain = Lui_GetOwnerDomainV1(Value.Runtime);
    if (!Value.Domain) throw std::runtime_error("DomainUnavailable");
    auto Root = std::make_unique<std::shared_ptr<Inbox>>(Value.Wake);
    LuiNativeWakeV1 Wake{sizeof(Wake), 1, Root.get(), Inbox::Request, Inbox::Release};
    if (Lui_SetNativeWakeV1(Value.Domain, &Wake, &Value.Registration) != LUI_NATIVE_OK)
        throw std::runtime_error("WakeUnavailable");
    Root.release();
}
int Run(const Options& Settings) {
    LuiProject* Raw = nullptr;
    LuiProjectOptionsV1 Prepare{sizeof(Prepare), 1};
    char Error[128]{};
    const auto Path = std::filesystem::absolute(std::filesystem::u8path(Settings.Project)).u8string();
    if (!Lui_ProjectPrepare(&Prepare, Path.data(), Path.size(), &Raw, Error, sizeof(Error))) {
        std::fprintf(stderr, "[LUI:Headless] ProjectPreparationFailed\n"); return 1;
    }
    std::unique_ptr<LuiProject, decltype(&Lui_ProjectRelease)> Project(Raw, Lui_ProjectRelease);
    LuiProjectSummaryV1 Summary{sizeof(Summary), 1};
    if (!Lui_ProjectGetSummary(Project.get(), &Summary)) throw std::runtime_error("ProjectUnavailable");
    const auto Descriptor = Json::parse(Summary.Descriptor, Summary.Descriptor + Summary.DescriptorBytes);
    const auto& Extensions=Descriptor.at("Manifest").at("Extensions");
    if (Settings.TrustedFfiWorkers && !Settings.Providers.count("Ffi")) throw std::runtime_error("FfiWorkerTrustRequired");
    if (std::find(Extensions.begin(),Extensions.end(),"Ffi")!=Extensions.end() && !Settings.Providers.count("Ffi"))
        throw std::runtime_error("FfiTrustRequired");
    if (!Settings.Providers.empty() && !Lui::Headless::CheckProviders(Descriptor,Settings.Providers))
        throw std::runtime_error("ProviderNotQualified");
    Host Value;
    Configure(Value, Settings, Descriptor.at("Manifest"), Project.get());
    LuiProjectRunOptionsV1 Execute{sizeof(Execute), 1};
    if (!Settings.Providers.empty()) {
        LuiProjectNativeOptionsV1 Trust{sizeof(Trust),1,LUI_PROJECT_NATIVE_TRUSTED,0};
        if(Settings.TrustedFfiWorkers) Trust.Flags|=LUI_PROJECT_NATIVE_TRUSTED_WORKERS;
        if (!Lui_LoadProjectNative(Value.Runtime,Project.get(),&Trust)) throw std::runtime_error("ProviderNotQualified");
        Execute.Flags=LUI_PROJECT_RUN_SANDBOX_NATIVE_SCRIPT;
    }
    if (!Settings.DatabaseRoot.empty() && (!Settings.Providers.count("SQLite") || Lui_GrantSQLiteDirectoryV1(Value.Runtime,Settings.DatabaseRoot.c_str())!=LUI_NATIVE_OK))
        throw std::runtime_error("DatabaseGrantUnavailable");
    const auto Deadline = Clock::now() + std::chrono::milliseconds(Settings.TimeoutMs);
    if (!Lui_RunProject(Value.Runtime, Project.get(), &Execute)) Value.Failed = true;
    int Status = Value.Failed ? 1 : 0;
    while (!Status) {
        if (Interrupted) { Status = 130; break; }
        if (Clock::now() >= Deadline) { Status = 124; break; }
        {
            std::lock_guard<std::mutex> Lock(Value.Wake->Mutex);
            Value.Wake->Pending = false;
        }
        Lui_AcknowledgeNativeWakeV1(Value.Domain, Value.Registration);
        Lui_Pump(Value.Runtime);
        if (Value.Streams && !Value.Streams->Pump()) { Status = 1; break; }
        if (Value.Processes && !Value.Processes->Pump()) { Status=1; break; }
        if (Value.Failed || Lui_IsOwnerPumpReadyV1(Value.Runtime) < 0) { Status = 1; break; }
        const int Pending = Lui_HasPendingWorkV1(Value.Runtime);
        if (Pending < 0) { Status = 1; break; }
        if (!Pending && !(Value.Streams && Value.Streams->Pending()) && !(Value.Processes && Value.Processes->Pending())) break;
        std::unique_lock<std::mutex> Lock(Value.Wake->Mutex);
        Value.Wake->Changed.wait_until(Lock, std::min(Deadline, Clock::now() + std::chrono::milliseconds(Settings.PollMs)),
            [&] { return Value.Wake->Pending || Value.Wake->Stopped; });
    }
    if (!Settings.Streams && Status == 124) std::fprintf(stderr, "[LUI:Headless] TimedOut\n");
    else if (!Settings.Streams && Status == 130) std::fprintf(stderr, "[LUI:Headless] Interrupted\n");
    LuiHttpStatsV1 Http{sizeof(Http), 1};
    if (!Lui_GetHttpStatsV1(Value.Runtime, &Http)) Status = 1;
    if (!Status && Value.Streams) Status = Value.Streams->ExitStatus;
    if (Value.Streams && (Status == 124 || Status == 130)) Value.Streams->ShutdownStatus = Status;
    Value.Close();
    if (!Value.Clean) Status = 1;
    if (!Settings.Streams) std::printf("[LUI:Headless] Exit=%d NativeResources=%d HttpLiveBeforeShutdown=%llu HttpReservedBeforeShutdown=%llu HttpPeak=%llu HttpPeakReserved=%llu\n",
        Status, Value.Clean ? 0 : 1, static_cast<unsigned long long>(Http.LiveRequests),
        static_cast<unsigned long long>(Http.ReservedBytes), static_cast<unsigned long long>(Http.PeakRequests),
        static_cast<unsigned long long>(Http.PeakReservedBytes));
    return Status;
}
}
int main(int Count, char** Arguments) {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    try {
        std::vector<std::string> Utf8;
        std::vector<char*> Pointers;
#if defined(_WIN32)
        int WideCount = 0;
        auto* Wide = CommandLineToArgvW(GetCommandLineW(), &WideCount);
        if (!Wide) throw std::runtime_error("InvalidArguments");
        struct Release { wchar_t** Value; ~Release() { LocalFree(Value); } } Owned{Wide};
        if (WideCount > 128) throw std::runtime_error("InvalidArguments");
        Utf8.reserve(WideCount); Pointers.reserve(WideCount);
        for (int Index = 0; Index < WideCount; ++Index) {
            const int Bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide[Index], -1, nullptr, 0, nullptr, nullptr);
            if (Bytes <= 0 || Bytes > 65536) throw std::runtime_error("InvalidArguments");
            std::string Value(Bytes, '\0');
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide[Index], -1, Value.data(), Bytes, nullptr, nullptr)) throw std::runtime_error("InvalidArguments");
            Value.pop_back(); Utf8.push_back(std::move(Value));
        }
        for (auto& Value : Utf8) Pointers.push_back(Value.data());
        Count = WideCount; Arguments = Pointers.data();
#endif
        const auto Settings = Parse(Count, Arguments);
        if (Settings.Help) {
            std::puts("LuiHeadlessHost --project <lui.json> [--grant network.client|network.server|network.raw]"
                " [--http-origin <exact origin>] [--http-public-only] [--diagnostics restricted|development]"
                " [--read-file path] [--write-file path] [--read-directory path|--write-directory path] [--database-root directory] [--grant Storage --storage-root directory]"
                " [--context [--env-key NAME]] [--tool executable --tool-cwd directory [--child-env-key NAME]]"
                " [--provider Compression|Crypto|SQLite|Regex|Toml] [--trusted-ffi [--trusted-ffi-workers]]"
                " [--timeout-ms 1..3600000] [--poll-ms 1..1000] [--stdio [-- application arguments]]");
            return 0;
        }
        std::signal(SIGINT, Interrupt); std::signal(SIGTERM, Interrupt);
        return Run(Settings);
    } catch (const std::runtime_error& Error) {
        // Only locally generated allowlisted messages reach this projection.
        const std::string Code = Error.what();
        for (const char* Allowed : {"InvalidArguments", "UnsupportedCapability", "HttpPolicyRequired", "NativeProvidersUnsupported", "StdioUnavailable",
             "CapabilityDenied", "FfiTrustRequired", "FfiWorkerTrustRequired", "ProviderNotQualified", "ToolUnavailable", "ContextUnavailable", "DatabaseGrantUnavailable", "FileServiceUnavailable", "StorageUnavailable", "RuntimeUnavailable", "InvalidAssets", "InvalidNetworkPolicy", "InvalidHttpPolicy",
             "DomainUnavailable", "WakeUnavailable", "ProjectUnavailable"}) {
            if (Code == Allowed) { std::fprintf(stderr, "[LUI:Headless] %s\n", Allowed); return 1; }
        }
        std::fprintf(stderr, "[LUI:Headless] Failed\n"); return 1;
    } catch (...) { std::fprintf(stderr, "[LUI:Headless] Failed\n"); return 1; }
}
