#pragma once
#include "State.h"
#include <map>
#include "../project/Declarations.h"

struct NativeServiceWaiter { int Complete = 0, Progress = 0; };
struct NativeServiceBinding {
    NativeServiceBinding() = default;
    NativeServiceBinding(const NativeServiceBinding&) = delete;
    NativeServiceBinding& operator=(const NativeServiceBinding&) = delete;
    ~NativeServiceBinding() { if (OwnsSession) Host.ReleaseSession(Host.Session); }
    LuiNativeHostV2 Host{sizeof(LuiNativeHostV2), 2};
    // Host retirement may precede VM teardown. Keep callbacks/userdata valid
    // until this binding is released after lua_close, even for retired sessions.
    bool OwnsSession = false;
    std::map<uint64_t, NativeServiceWaiter> Waiters;
    bool IsExtension = false;
    bool IsPlatform = false;
};
void RegisterNativeTypes(lua_State* State);
bool HasNativeService(const LuiRuntime* Runtime, const std::string& Name);
bool NativeServiceNeedsHostGrant(const LuiRuntime* Runtime, const std::string& Name);
uint32_t RegisterPlatformNativeService(LuiRuntime* Runtime, const LuiNativeServiceV2* Service,
    const LuiNativeProviderV2* Provider, void* Context, LuiNativeHostV2* Host);
int PushNativeService(lua_State* State, LuiRuntime* Runtime, const std::string& Name);
int DrainNativeServices(LuiRuntime* Runtime, size_t Maximum = 64,
    std::chrono::steady_clock::time_point Deadline = std::chrono::steady_clock::time_point::max());
void CloseNativeServices(LuiRuntime* Runtime);
std::string NativeServiceSchema(const LuiRuntime* Runtime, const std::string& Base);
Lui::Project::ServiceDeclaration DescribeNativeService(LuiNativeSessionV2* Session);
void ObserveNativeServices(const LuiRuntime* Runtime, Lui::Project::DeclarationSet& Observed, bool HostOnly);
uint32_t LoadNativeImageV2(LuiRuntime* Runtime, void* Image, std::shared_ptr<void> Pin,
    const Lui::Project::DeclarationSet* Declared = nullptr, const Lui::Project::NativeObservation* Expected = nullptr);
