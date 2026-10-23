#include "../internal/Modules.h"
#include "../internal/NativeServices.h"
#include "lua.h"
#include "lualib.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>

namespace {
LuiRuntime* RuntimeOf(lua_State* State) { return static_cast<LuiRuntime*>(lua_callbacks(State)->userdata); }
int Raise(lua_State* State, const char* Code) {
    auto* Runtime = RuntimeOf(State);
    if (Runtime->Modules && !Runtime->Modules->Stack.empty() &&
        Runtime->Modules->Stack.back()->Status != ModuleRecord::State::Failed) Runtime->Modules->Stack.back()->Failure = Code;
    lua_pushfstring(State, "[LUI:Modules] %s", Code);
    lua_error(State); return 0;
}
const char* ResolveCode(const std::string& Error) {
    for (const auto* Code : {"InvalidPath", "MissingModule", "ForbiddenImport", "EntryImportDenied", "LimitExceeded"})
        if (Error == std::string("[LUI:Project] ") + Code) return Code;
    return "LoadFailed";
}
int Require(lua_State* State);
int Evaluate(lua_State* State) {
    auto* Runtime = RuntimeOf(State);
    auto& Context = *Runtime->Modules;
    auto* Record = static_cast<ModuleRecord*>(lua_touserdata(State, 1));
    lua_settop(State, 0);
    lua_newtable(State);
    const int Environment = lua_gettop(State);
    lua_pushvalue(State, Environment); lua_setfield(State, Environment, "_G");
    lua_pushlightuserdata(State, Record);
    lua_pushcclosure(State, Require, "require", 1); lua_setfield(State, Environment, "require");
    lua_newtable(State); lua_getref(State, Context.CoreReference); lua_setfield(State, -2, "__index");
    lua_setreadonly(State, -1, true); lua_setmetatable(State, Environment);
    // A private chunk may write/shadow globals; never assume its environment is immutable.
    lua_setsafeenv(State, Environment, false);
    if (luau_load(State, Record->Id.c_str(), Record->Artifact->Bytes.data(), Record->Artifact->Bytes.size(), Environment) != LUA_OK)
        return Raise(State, "LoadFailed");
    lua_remove(State, Environment);
    lua_call(State, 0, LUA_MULTRET);
    return lua_gettop(State);
}
int Initialize(lua_State* State) {
    auto* Record = static_cast<ModuleRecord*>(lua_touserdata(State, 1));
    const int Count = Evaluate(State);
    if (Record->Status == ModuleRecord::State::Failed) return Raise(State, Record->Failure);
    if (Count != 1 || lua_isnil(State, -1)) return Raise(State, "InvalidResult");
    Record->ExportReference = lua_ref(State, -1);
    return 0;
}
int Require(lua_State* State) {
    auto* Runtime = RuntimeOf(State);
    if (!Runtime->Modules) return Raise(State, "ProjectNotConfigured");
    if (lua_gettop(State) != 1 || lua_type(State, 1) != LUA_TSTRING) return Raise(State, "InvalidPath");
    size_t Length = 0; const char* Request = lua_tolstring(State, 1, &Length);
    auto& Context = *Runtime->Modules;
    auto* Importer = static_cast<ModuleRecord*>(lua_touserdata(State, lua_upvalueindex(1)));
    if (!Importer) return Raise(State, "ProjectNotConfigured");
    std::string Error;
    const auto* Source = Context.Snapshot->GetSources().Resolve(Importer->Id, std::string_view(Request, Length), Error);
    if (!Source) return Raise(State, ResolveCode(Error));
    auto& Record = Context.Records.at(Source->Id);
    if (Record.Status == ModuleRecord::State::Loaded) {
        lua_getref(State, Record.ExportReference); return 1;
    }
    if (Record.Status == ModuleRecord::State::Failed) return Raise(State, Record.Failure);
    if (Record.Status == ModuleRecord::State::Evaluating) {
        // Poison every participant, including ones which catch the inner error.
        auto First = std::find(Context.Stack.begin(), Context.Stack.end(), &Record);
        for (auto Item = First; Item != Context.Stack.end(); ++Item) {
            (*Item)->Status = ModuleRecord::State::Failed; (*Item)->Failure = "Cycle";
        }
        return Raise(State, "Cycle");
    }
    if (Context.Stack.size() >= Context.MaxDepth) {
        Record.Status = ModuleRecord::State::Failed; Record.Failure = "LimitExceeded";
        return Raise(State, Record.Failure);
    }
    const int Base = lua_gettop(State);
    Record.Status = ModuleRecord::State::Evaluating; Record.Failure = "LoadFailed";
    Context.Stack.push_back(&Record); ++Runtime->ModuleDepth;
    // cpcall protects even closure/environment/reference allocation; no VM
    // allocation occurs between setting Evaluating and installing protection.
    const int Status = lua_cpcall(State, Initialize, &Record);
    --Runtime->ModuleDepth; Context.Stack.pop_back();
    if (Status != LUA_OK || Record.Status == ModuleRecord::State::Failed) {
        if (Status == LUA_ERRMEM) Record.Failure = "LimitExceeded";
        Record.Status = ModuleRecord::State::Failed; lua_settop(State, Base);
        return Raise(State, Record.Failure);
    }
    Record.Status = ModuleRecord::State::Loaded;
    lua_getref(State, Record.ExportReference);
    return 1;
}
int Yield(lua_State* State) {
    if (RuntimeOf(State)->ModuleDepth) return Raise(State, "YieldNotAllowed");
    return lua_yield(State, lua_gettop(State));
}
}
void CheckModuleEffect(lua_State* State) {
    if (RuntimeOf(State)->ModuleDepth) Raise(State, "InitializationEffect");
}
void MarkModuleLimit(LuiRuntime* Runtime) {
    if (Runtime->Modules) for (auto* Record : Runtime->Modules->Stack) Record->Failure = "LimitExceeded";
}
void RegisterModules(lua_State* State) {
    lua_pushlightuserdata(State, nullptr); lua_pushcclosure(State, Require, "require", 1); lua_setglobal(State, "require");
    lua_getglobal(State, "coroutine"); lua_pushcfunction(State, Yield, "coroutine.yield"); lua_setfield(State, -2, "yield");
    lua_pop(State, 1);
}
bool ConfigureProject(LuiRuntime* Runtime, std::shared_ptr<const Lui::Project::CompiledSnapshot> Snapshot,
    const LuiProjectRunOptionsV1* Options) {
    auto Fail = [Runtime](const char* Code) { Runtime->LastError = std::string("[LUI:Modules] ") + Code; return false; };
    try {
        if (!Snapshot || !Options || Options->StructSize != sizeof(*Options) || Options->AbiVersion != LUI_PROJECT_RUN_ABI_VERSION ||
            (Options->Flags != LUI_PROJECT_RUN_DEVELOPMENT_DIAGNOSTICS && !(Runtime->DiagnosticMode && Options->Flags==0)) || Options->Reserved || Options->MaxImportDepth > 64)
            return Fail("InvalidOptions");
        const auto Manifest = nlohmann::json::parse(Snapshot->GetDescriptor()).at("Manifest");
        if(Manifest.at("Diagnostics").at("Mode")=="Restricted") {
            if(!Runtime->DiagnosticMode) return Fail("PolicyNotSupported");
            Runtime->DiagnosticMode=LUI_DIAGNOSTICS_RESTRICTED;
            Runtime->DiagnosticRecords.clear(); Runtime->DiagnosticBytes=0;
        }
        uint64_t Requested = 0;
        const std::map<std::string, uint64_t> Capabilities{{"NativeExtensions",LUI_CAPABILITY_NATIVE_EXTENSIONS},
            {"HostServices",LUI_CAPABILITY_HOST_SERVICES}, {"Clipboard",LUI_CAPABILITY_CLIPBOARD},
            {"Dialogs",LUI_CAPABILITY_DIALOGS}, {"network.client",LUI_CAPABILITY_NETWORK_CLIENT},
            {"network.server",LUI_CAPABILITY_NETWORK_SERVER}, {"network.raw",LUI_CAPABILITY_NETWORK_RAW},
            {"Storage",LUI_CAPABILITY_STORAGE},{"Notifications",LUI_CAPABILITY_NOTIFICATIONS},{"Tray",LUI_CAPABILITY_TRAY}};
        for (const auto& Name : Manifest.at("Capabilities")) Requested |= Capabilities.at(Name.get<std::string>());
        if (Requested & ~Runtime->GrantedCapabilities) return Fail("CapabilityDenied");
        const bool Native=Requested & LUI_CAPABILITY_NATIVE_EXTENSIONS;
        if((Native || !Runtime->Extensions.empty()) && (!Runtime->DiagnosticMode || !Runtime->ProjectNativeVerified ||
            Runtime->VerifiedProjectDigest!=Snapshot->GetDigest())) return Fail("UnsupportedFeature");
        const auto& Declared = Snapshot->GetSources().GetDeclarations();
        if (!Declared.Files.empty() && !Native) {
            // Owner-host registration must exactly match the captured contract.
            // Compare only metadata; no callbacks, DLL or application code run.
            try {
                if (Runtime->ExtensionMethods.size() > 256 || Runtime->ExtensionSignals.size() > 256 - Runtime->ExtensionMethods.size())
                    return Fail("RegistrationMismatch");
                Lui::Project::DeclarationSet Observed;
                ObserveNativeServices(Runtime, Observed, false);
                for (const auto& Method : Runtime->ExtensionMethods) {
                    if (Method->ExtensionName != "Host") return Fail("RegistrationMismatch");
                    Observed.Services[Method->ServiceName].Methods.emplace(Method->Name,
                        Lui::Project::ParseMethod(Method->ServiceName,Method->Type));
                }
                for (const auto& Signal : Runtime->ExtensionSignals) {
                    if (Signal->ExtensionName != "Host") return Fail("RegistrationMismatch");
                    Observed.Services[Signal->ServiceName].Signals.emplace(Signal->Name,Lui::Project::ParseSignal(Signal->Type));
                }
                Lui::Project::FinishDeclarations(Observed);
                if (Observed.Digest != Declared.Digest) return Fail("RegistrationMismatch");
            } catch (...) { return Fail("RegistrationMismatch"); }
        }
        for (const auto& Name : Manifest.at("Assets")) if (!Runtime->Assets.count(Name.get<std::string>())) return Fail("AssetNotRegistered");
        // Until the managed policy adapter arrives, explicit project network
        // policy must exactly match the already configured owner-host policy.
        if (Manifest.contains("NetworkPolicy")) {
            const auto& Policy = Manifest.at("NetworkPolicy");
            const uint32_t Flags = (Policy.at("ClientLoopbackOnly").get<bool>() ? LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY : 0) |
                (Policy.at("ServerLoopbackOnly").get<bool>() ? LUI_NETWORK_POLICY_SERVER_LOOPBACK_ONLY : 0);
            if (!Runtime->NetworkPolicyDeclared || Flags != Runtime->NetworkPolicyFlags ||
                Policy.at("ClientPortMin") != Runtime->ClientPortMin || Policy.at("ClientPortMax") != Runtime->ClientPortMax ||
                Policy.at("ServerPortMin") != Runtime->ServerPortMin || Policy.at("ServerPortMax") != Runtime->ServerPortMax)
                return Fail("PolicyNotConfigured");
        }
        auto Context = std::make_shared<ModuleContext>(); Context->Snapshot = std::move(Snapshot);
        Context->MaxDepth = Options->MaxImportDepth ? Options->MaxImportDepth : 64;
        Context->Stack.reserve(64);
        for (const auto& Pair : Context->Snapshot->GetArtifacts()) {
            ModuleRecord Record; Record.Id = Pair.first; Record.Artifact = &Pair.second;
            Context->Records.emplace(Pair.first, std::move(Record));
        }
        if (Context->Snapshot->GetRetainedBytes() > 128 * 1024 * 1024 - Context->Records.size() * 2048 - sizeof(ModuleContext))
            return Fail("LimitExceeded");
        uint64_t Memory = 16 * 1024 * 1024, Interrupts = 10000;
        if (Manifest.contains("Sandbox")) {
            Memory = Manifest.at("Sandbox").at("MaxMemoryBytes").get<uint64_t>();
            Interrupts = Manifest.at("Sandbox").at("MaxInterrupts").get<uint64_t>();
        }
        if (Runtime->Sandboxed) {
            if (Memory < Runtime->VmBytes) return Fail("LimitExceeded");
            Runtime->VmLimitBytes = std::min(Runtime->VmLimitBytes, Memory);
            Runtime->MaxInterrupts = std::min(Runtime->MaxInterrupts, Interrupts);
        } else {
            if(Native) {
                // Quotas bound Luau allocations/execution only. Trusted native
                // code remains privileged, as in the existing extension ABI.
                if(Memory<Runtime->VmBytes) return Fail("LimitExceeded");
                Runtime->VmLimitBytes=Memory; Runtime->MaxInterrupts=Interrupts;
            } else {
                LuiSandboxLimitsV1 Limits{sizeof(Limits),LUI_EXTENSION_ABI_VERSION,Memory,Interrupts};
                if (!Lui_ConfigureSandbox(Runtime, &Limits)) return Fail("LimitExceeded");
            }
        }
        Runtime->GrantedCapabilities = Requested;
        std::unordered_set<std::string> Assets;
        for (const auto& Name : Manifest.at("Assets")) Assets.insert(Name.get<std::string>());
        Runtime->Assets.swap(Assets);
        Runtime->Modules = std::move(Context); return true;
    } catch (const std::bad_alloc&) { return Fail("LimitExceeded"); }
    catch (...) { return Fail("LoadFailed"); }
}
int ProjectEntry(lua_State* State) {
    auto& Context = *RuntimeOf(State)->Modules;
    lua_settop(State, 0);
    lua_setreadonly(State, LUA_GLOBALSINDEX, true);
    lua_pushvalue(State, LUA_GLOBALSINDEX); Context.CoreReference = lua_ref(State, -1); lua_pop(State, 1);
    auto& Entry = Context.Records.at(Context.Snapshot->GetSources().GetEntry());
    lua_pushlightuserdata(State, &Entry); Evaluate(State); lua_settop(State, 0); return 0;
}
