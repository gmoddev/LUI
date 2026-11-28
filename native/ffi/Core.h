#pragma once
#include <ffi.h>
#include <nlohmann/json.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Lui::Ffi {
using Json = nlohmann::json;
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
inline void Require(bool Condition, const char* Reason) { if (!Condition) throw Failure(Reason); }
constexpr size_t MaxValueBytes = 4096, MaxChunk = 16384, MaxArguments = 16;
struct Resource { virtual ~Resource() = default; };
using Held = std::vector<std::shared_ptr<Resource>>;

struct Type : Resource {
    std::string Name;
    ffi_type* Abi = nullptr;
    ffi_type Aggregate{};
    std::vector<ffi_type*> Elements;
    std::vector<std::shared_ptr<Type>> Children;
    std::vector<std::string> Fields;
    std::vector<size_t> Offsets;
    size_t Depth = 1, Leaves = 1, PointerResults = 0;
    size_t MemorySize = 0, MemoryAlignment = 0;
    bool Array = false, HasPointers = false, Boolean = false, PackedAbi = false;
    size_t Size() const { return Abi ? Abi->size : MemorySize; }
    size_t Alignment() const { return Abi ? Abi->alignment : MemoryAlignment; }
};
using NativeType = std::shared_ptr<Type>;
struct Image : Resource {
    virtual void* Symbol(const std::string&) const = 0;
};
std::shared_ptr<Image> OpenImage(const Json&);
void PinCallbackModule();
struct Signature : Resource {
    enum class CompactKind { Number, Handle, Word };
    struct CompactField {
        NativeType Value;
        size_t Offset;
        CompactKind Kind;
    };
    ffi_cif Cif{};
    std::vector<NativeType> Parameters;
    std::vector<ffi_type*> Abi;
    NativeType Result;
    std::array<size_t,MaxArguments> ArgumentOffsets{};
    size_t ReturnOffset = 0, ScratchBytes = 0;
    bool Scalar = false;
    bool Compact = false;
    bool CompactHandles = false;
    std::vector<CompactField> CompactArguments, CompactResult;
};
struct Function : Resource {
    std::shared_ptr<Image> Library;
    std::shared_ptr<Signature> Declaration;
    void* Address = nullptr;
};
struct Ownership {
    uintptr_t Base = 0;
    size_t Extent = 0;
    bool Locked = false;
    std::shared_ptr<void> Storage;
    std::shared_ptr<Function> Release;
    std::shared_ptr<Ownership> Parent;
    ~Ownership();
};
struct Pointer : Resource {
    void* Address = nullptr;
    size_t Size = 0;
    bool Bounded = false, LibraryOwned = false;
    Held Pins;
    std::shared_ptr<Ownership> Owner;
};
struct alignas(std::max_align_t) Slot { std::array<unsigned char, MaxValueBytes> Data{}; };

class Callbacks;
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Json Dispatch(const Json&);
    std::string DispatchBinary(std::string_view);
    unsigned Add(const std::shared_ptr<Resource>&);
    unsigned HandleCheckpoint() const { return Next; }
    void RollbackHandles(unsigned First);
    std::shared_ptr<Pointer> ManagedPointer(void* Address, size_t Size, std::shared_ptr<void> Storage);
    template<class T> std::shared_ptr<T> Get(const Json& Value) const {
        return Get<T>(Count(Value, 0xffffffffu));
    }
    template<class T> std::shared_ptr<T> Get(unsigned Id) const {
        const auto It = Resources.find(Id);
        Require(It != Resources.end(), "Closed");
        auto Result = std::dynamic_pointer_cast<T>(It->second);
        Require(bool(Result), "WrongHandle");
        return Result;
    }
    NativeType Resolve(const Json&) const;
    std::shared_ptr<Signature> Declare(const Json& Parameters, const Json& Result, unsigned Fixed = 0) const;
    Json Layout(const Json&);
    void Encode(const NativeType&, const Json&, void*, Held&);
    Json Decode(const NativeType&, const void*, const Held&);
    static uint32_t Count(const Json&, uint32_t Maximum);
private:
    std::shared_ptr<Ownership> FindOwner(void* Address);
    void ExtendOwner(const std::shared_ptr<Ownership>& Owner, size_t Size);
    std::shared_ptr<Pointer> ReturnedPointer(void* Address, const Held&);
    void RequireHandleCapacity(size_t Count) const;
    Json Invoke(const std::shared_ptr<Function>&, const Json&);
    std::string InvokeCompact(const std::shared_ptr<Function>&, std::string_view);
    std::map<unsigned, std::shared_ptr<Resource>> Resources;
    std::map<uintptr_t, std::weak_ptr<Ownership>> Owners;
    unsigned Next = 1;
    std::shared_ptr<size_t> Allocated;
    std::unique_ptr<Callbacks> Notices;
};
std::string HexBytes(const unsigned char*, size_t);
std::vector<unsigned char> ParseBytes(const std::string&, size_t Maximum);
void EncodeNumeric(const Type&, double, bool, void*);
double DecodeNumeric(const Type&, const void*);
}
