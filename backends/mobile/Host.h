#pragma once
#include "../../native/abi/LuiRuntime.h"
#include "../../native/abi/LuiNativeServices.h"
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <thread>
#include <array>
#include <vector>

namespace Lui::Mobile {
// Private bounded mobile control host. All calls, including destruction,
// belong to the constructing thread. No toolkit objects or shared VM pointers.
class Host final {
public:
    explicit Host(uint32_t Generation);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    bool Start(const std::string& Bundle, const std::string& StorageRoot = {});
    bool Attach(uint32_t Epoch, double Width, double Height);
    bool Detach(uint32_t Epoch);
    std::string Take();
    bool Acknowledge(uint32_t Epoch, uint32_t Revision, bool Success);
    bool Activate(uint32_t Epoch, uint32_t Revision, uint32_t Sequence, int Id);
    int Event(uint32_t Epoch, uint32_t Revision, uint32_t Sequence, int Id, const std::string& Input);
    bool Pump();
    bool Theme(const std::string& Value);
    std::string Asset(const std::string& Path) const;
    bool Retire();
    bool IsOwner() const;
    void RecordOwner(double Milliseconds);
    std::string Metrics() const;
    static std::string PrepareBundle(const std::map<std::string, std::string>& Files);
private:
    using Json = nlohmann::json;
    std::thread::id Owner;
    uint32_t Generation, Epoch = 0, Revision = 0, InFlight = 0, Applied = 0, Sequence = 0;
    LuiRuntime* Runtime = nullptr;
    LuiNativeDomainV2* Domain = nullptr;
    std::map<int, Json> Nodes;
    std::map<std::string, std::string> Assets;
    std::map<int, int> Edits;
    std::map<int, uint64_t> ContentEpochs;
    std::map<int, size_t> NodeBytes;
    size_t RetainedBytes = 0;
    uint64_t OwnerCalls = 0, OwnerOver8Ms = 0;
    double OwnerTotalMs = 0, OwnerMaxMs = 0;
    struct ContextNode { int Id, Parent, Rank; bool Input; std::string Class; std::array<double, 4> Rect; Json Record; };
    mutable std::vector<ContextNode> Context;
    mutable bool ContextDirty = true;
    bool Attached = false, Dirty = false, Failed = false, Started = false;
    double Width = 0, Height = 0;
    int Window = 0;
    std::string SourceDigest, CompiledDigest;
    bool Fail();
    bool Resize();
    bool Admitted(uint32_t Epoch, uint32_t Revision, uint32_t Sequence, int Id);
    bool WithinBounds(int Id);
    void CancelInput();
    Json Properties(int Id) const;
    std::string Content(int Id) const;
    std::string Observation(int Id) const;
    void PrepareContext() const;
    static int Create(void*, int, const char*);
    static int Property(void*, int, const char*, const char*);
    static int Parent(void*, int, int);
    static int Arrange(void*, int, double, double, double, double, int);
    static int Destroy(void*, int);
    static int Present(void*, int, int, double, double, double, double, int, int);
    static int LoadImage(void*, int, uint64_t, const char*);
    static void CancelImage(void*, uint64_t);
};
}
