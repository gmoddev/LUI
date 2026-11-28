#include "Fixture.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
constexpr uint32_t MaximumBytes = 65536;
std::atomic<uint32_t> Live{0};
std::atomic<uint32_t> Waiting{0};
std::atomic<uint32_t> Releases{0};
std::mutex RegistrationMutex;
LuiFfiNotification Registered = nullptr;
uint32_t Delivering = 0;
thread_local uint32_t LastError = 0;

enum class JobPhase { Idle, Running, Canceled, Delivering };
struct BackgroundJob {
    std::mutex Mutex;
    std::thread Thread;
    std::atomic<JobPhase> Phase{JobPhase::Idle};
    bool Joining = false;
};
BackgroundJob* GetJob() {
    // A C client may close its own library reference with a job in flight.
    // Pin the code and keep a single native slot until process exit. Explicit
    // join releases the thread outside any platform loader teardown lock.
    static BackgroundJob* Job = []() -> BackgroundJob* {
#ifdef _WIN32
        HMODULE Module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&LuiFfiStartJob), &Module)) return nullptr;
#else
        Dl_info Info{};
        if (!dladdr(reinterpret_cast<void*>(&LuiFfiStartJob), &Info) ||
            !dlopen(Info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE)) return nullptr;
#endif
        return new (std::nothrow) BackgroundJob;
    }();
    return Job;
}
}

struct LuiFfiHandle {
    uint64_t Value;
};

int8_t LUI_FFI_CALL LuiFfiEchoI8(int8_t Value) { return Value; }
uint8_t LUI_FFI_CALL LuiFfiEchoU8(uint8_t Value) { return Value; }
int16_t LUI_FFI_CALL LuiFfiEchoI16(int16_t Value) { return Value; }
uint16_t LUI_FFI_CALL LuiFfiEchoU16(uint16_t Value) { return Value; }
int32_t LUI_FFI_CALL LuiFfiEchoI32(int32_t Value) { return Value; }
uint32_t LUI_FFI_CALL LuiFfiEchoU32(uint32_t Value) { return Value; }
int64_t LUI_FFI_CALL LuiFfiEchoI64(int64_t Value) { return Value; }
uint64_t LUI_FFI_CALL LuiFfiEchoU64(uint64_t Value) { return Value; }
float LUI_FFI_CALL LuiFfiEchoF32(float Value) { return Value; }
double LUI_FFI_CALL LuiFfiEchoF64(double Value) { return Value; }
intptr_t LUI_FFI_CALL LuiFfiEchoISize(intptr_t Value) { return Value; }
uintptr_t LUI_FFI_CALL LuiFfiEchoUSize(uintptr_t Value) { return Value; }
long LUI_FFI_CALL LuiFfiEchoLong(long Value) { return Value; }
uint8_t LUI_FFI_CALL LuiFfiBool8(uint8_t Value) { return Value != 0; }

double LUI_FFI_CALL LuiFfiMix(int8_t A, double B, uint16_t C, float D,
    int32_t E, double F, uint32_t G, float H) {
    return A + 2.0*B + 3.0*C + 4.0*D + 5.0*E + 6.0*F + 7.0*G + 8.0*H;
}
double LUI_FFI_CALL LuiFfiVariadicMix(int Seed, ...) {
    va_list Arguments;
    va_start(Arguments, Seed);
    const int A=va_arg(Arguments,int);
    const double B=va_arg(Arguments,double);
    const int C=va_arg(Arguments,int);
    const double D=va_arg(Arguments,double);
    const uint64_t Word=va_arg(Arguments,uint64_t);
    const char* Text=va_arg(Arguments,const char*);
    va_end(Arguments);
    return Seed+A+B+C+D+static_cast<unsigned>(Word&255)+static_cast<unsigned char>(Text[0]);
}
double LUI_FFI_CALL LuiFfiVariadicPair(int Seed, ...) {
    va_list Arguments;
    va_start(Arguments, Seed);
    const int Mark=va_arg(Arguments,int);
    const LuiFfiPair Pair=va_arg(Arguments,LuiFfiPair);
    const double Tail=va_arg(Arguments,double);
    va_end(Arguments);
    return Seed+Mark+Pair.Left+2*Pair.Right+3*Tail;
}
int32_t LUI_FFI_CALL LuiFfiDivide(int32_t Value, int32_t Divisor, int32_t* Output) {
    if (!Output) LastError = 1;
    else if (!Divisor) LastError = 2;
    else if (Value == INT32_MIN && Divisor == -1) LastError = 3;
    else {
        *Output = Value / Divisor;
        LastError = 0;
        return 1;
    }
    return 0;
}
uint32_t LUI_FFI_CALL LuiFfiLastError() { return LastError; }
int32_t LUI_FFI_CALL LuiFfiSetErrors(int32_t Value) {
    errno=EDOM;
#ifdef _WIN32
    SetLastError(1234);
#endif
    return Value;
}

void LUI_FFI_CALL LuiFfiAdjustPackedInto(LuiFfiPacked* Value, uint32_t Delta) {
    *Value = LuiFfiAdjustPacked(*Value, Delta);
}
int32_t LUI_FFI_CALL LuiFfiCallPacked(LuiFfiPackedCallback Callback) {
    const LuiFfiPacked Small{2, 127, 3};
    const LuiFfiPackedLarge Large{4, UINT64_C(0x123456789abcdef0), 5};
    return Callback ? Callback(Small, Large) : -1;
}
LuiFfiPointerNumber LUI_FFI_CALL LuiFfiPointerNumberResult(void* Address, uint32_t Finite) {
    return {Address, Finite ? 2.5 : std::numeric_limits<double>::quiet_NaN()};
}
uint32_t LUI_FFI_CALL LuiFfiCountNulls(LuiFfiPointers Value) {
    return (Value.Entries[0] == nullptr) + (Value.Entries[1] == nullptr) + (Value.Entries[2] == nullptr);
}

LuiFfiOctets LUI_FFI_CALL LuiFfiAdjustOctets(uint8_t Delta, LuiFfiOctets Value) {
    for (auto& Byte : Value.Bytes) Byte = static_cast<uint8_t>(Byte + Delta);
    return Value;
}
LuiFfiWide LUI_FFI_CALL LuiFfiAdjustWide(LuiFfiWide Value, double Delta) {
    for (auto& Item : Value.Values) Item += Delta;
    return Value;
}

uint32_t LUI_FFI_CALL LuiFfiLayout(uint32_t Field) {
    constexpr uint32_t Fields[] = {
        sizeof(void*), sizeof(long), sizeof(LuiFfiPair), alignof(LuiFfiPair),
        offsetof(LuiFfiPair, Left), offsetof(LuiFfiPair, Right),
        sizeof(LuiFfiNested), alignof(LuiFfiNested), offsetof(LuiFfiNested, Tag),
        offsetof(LuiFfiNested, Pair), offsetof(LuiFfiNested, Samples),
        sizeof(LuiFfiPacked), alignof(LuiFfiPacked), offsetof(LuiFfiPacked, Count), offsetof(LuiFfiPacked, Tail),
        sizeof(LuiFfiPackedContainer), alignof(LuiFfiPackedContainer),
        offsetof(LuiFfiPackedContainer, Rows), offsetof(LuiFfiPackedContainer, Trailer)
    };
    return Field < sizeof(Fields)/sizeof(Fields[0]) ? Fields[Field] : UINT32_MAX;
}
uint32_t LUI_FFI_CALL LuiFfiSumPacked(const LuiFfiPackedContainer* Value) {
    uint32_t Total=Value->Header+Value->Trailer;
    for(const auto& Row:Value->Rows) Total+=Row.Tag+Row.Count+Row.Tail;
    return Total;
}

LuiFfiPair LUI_FFI_CALL LuiFfiMakePair(int32_t Left, double Right) {
    return {Left, Right};
}
LuiFfiPair LUI_FFI_CALL LuiFfiAdjustPair(LuiFfiPair Value, int32_t Delta) {
    Value.Right += Delta;
    return Value;
}
LuiFfiPacked LUI_FFI_CALL LuiFfiAdjustPacked(LuiFfiPacked Value,uint32_t Delta) {
    Value.Count+=Delta;
    return Value;
}
LuiFfiPackedLarge LUI_FFI_CALL LuiFfiAdjustPackedLarge(LuiFfiPackedLarge Value,uint32_t Delta) {
    Value.Value+=Delta;
    Value.Flags^=0x55;
    return Value;
}
double LUI_FFI_CALL LuiFfiSumPair(const LuiFfiPair* Value) {
    return Value ? Value->Left + Value->Right : 0.0;
}
double LUI_FFI_CALL LuiFfiSumNested(const LuiFfiNested* Value) {
    if (!Value) return 0.0;
    return Value->Tag + LuiFfiSumPair(&Value->Pair) +
        Value->Samples[0] + 2.0*Value->Samples[1] + 3.0*Value->Samples[2];
}

void* LUI_FFI_CALL LuiFfiAlloc(uint32_t Size) {
    if (!Size || Size > MaximumBytes) return nullptr;
    auto* Memory = std::calloc(Size, 1);
    if (Memory) ++Live;
    return Memory;
}
void LUI_FFI_CALL LuiFfiFree(void* Memory) {
    if (!Memory) return;
    std::free(Memory);
    --Live;
}
LuiFfiHandle* LUI_FFI_CALL LuiFfiOpen(uint64_t Value) {
    auto* Handle = new (std::nothrow) LuiFfiHandle{Value};
    if (Handle) ++Live;
    return Handle;
}
uint64_t LUI_FFI_CALL LuiFfiRead(const LuiFfiHandle* Handle) {
    return Handle ? Handle->Value : 0;
}
void LUI_FFI_CALL LuiFfiClose(LuiFfiHandle* Handle) {
    if (!Handle) return;
    delete Handle;
    --Live;
}
uint32_t LUI_FFI_CALL LuiFfiLive() { return Live.load(); }
const char* LUI_FFI_CALL LuiFfiText() { return "caf\xc3\xa9"; }
uint32_t LUI_FFI_CALL LuiFfiMutate(uint8_t* Bytes, uint32_t Size, uint8_t Mask) {
    if (Size > MaximumBytes || (!Bytes && Size)) return UINT32_MAX;
    for (uint32_t Index = 0; Index < Size; ++Index) Bytes[Index] ^= Mask;
    return Size;
}
uint32_t LUI_FFI_CALL LuiFfiHash(const uint8_t* Bytes, uint32_t Size) {
    if (Size > MaximumBytes || (!Bytes && Size)) return 0;
    uint32_t Hash = UINT32_C(2166136261);
    for (uint32_t Index = 0; Index < Size; ++Index) Hash = (Hash ^ Bytes[Index]) * UINT32_C(16777619);
    return Hash;
}
void* LUI_FFI_CALL LuiFfiOffset(void* Bytes, uint32_t Size, uint32_t Offset) {
    if (!Bytes || Size > MaximumBytes || Offset > Size) return nullptr;
    return static_cast<uint8_t*>(Bytes) + Offset;
}
void LUI_FFI_CALL LuiFfiCountRelease(void* Memory) { if (Memory) ++Releases; }
uint32_t LUI_FFI_CALL LuiFfiReleaseCount() { return Releases.load(); }

int32_t LUI_FFI_CALL LuiFfiCall(LuiFfiCallback Callback, int32_t Value) {
    return Callback ? Callback(Value) : INT32_MIN;
}
int32_t LUI_FFI_CALL LuiFfiCallOnThread(LuiFfiCallback Callback, int32_t Value) {
    if (!Callback) return INT32_MIN;
    int32_t Result = INT32_MIN;
    try {
        std::thread Worker([&] { Result = Callback(Value); });
        Worker.join();
    } catch (const std::system_error&) { return INT32_MIN; }
    return Result;
}
namespace {
LuiFfiCallback RememberedComputed=nullptr;
}
void LUI_FFI_CALL LuiFfiRememberComputed(LuiFfiCallback Callback) { RememberedComputed=Callback; }
int32_t LUI_FFI_CALL LuiFfiCallRemembered(int32_t Value) {
    return RememberedComputed?RememberedComputed(Value):INT32_MIN;
}
void LUI_FFI_CALL LuiFfiNotify(LuiFfiNotification Callback, int32_t Value) {
    if (Callback) Callback(Value);
}
void LUI_FFI_CALL LuiFfiNotifyOnThread(LuiFfiNotification Callback, int32_t Value) {
    if (!Callback) return;
    try {
        std::thread Worker([=] { Callback(Value); });
        Worker.join();
    } catch (const std::system_error&) {}
}
uint32_t LUI_FFI_CALL LuiFfiRegister(LuiFfiNotification Callback) {
    std::lock_guard<std::mutex> Lock(RegistrationMutex);
    if (!Callback || Registered || Delivering) return 0;
    Registered = Callback;
    return 1;
}
uint32_t LUI_FFI_CALL LuiFfiEmit(int32_t Value) {
    LuiFfiNotification Callback;
    {
        std::lock_guard<std::mutex> Lock(RegistrationMutex);
        if (!Registered) return 0;
        Callback = Registered;
        ++Delivering;
    }
    Callback(Value);
    std::lock_guard<std::mutex> Lock(RegistrationMutex);
    --Delivering;
    return 1;
}
uint32_t LUI_FFI_CALL LuiFfiUnregister() {
    std::lock_guard<std::mutex> Lock(RegistrationMutex);
    Registered = nullptr;
    return Delivering == 0;
}
uint32_t LUI_FFI_CALL LuiFfiWait(uint32_t Milliseconds) {
    if (Milliseconds > 250) return UINT32_MAX;
    ++Waiting;
    std::this_thread::sleep_for(std::chrono::milliseconds(Milliseconds));
    --Waiting;
    return Milliseconds;
}
uint32_t LUI_FFI_CALL LuiFfiWaiting() { return Waiting.load(); }

uint32_t LUI_FFI_CALL LuiFfiStartJob(LuiFfiNotification Callback, uint32_t Rounds) {
    if (!Callback || !Rounds || Rounds > 4096) return 0;
    auto* Job = GetJob();
    if (!Job) return 0;
    std::lock_guard<std::mutex> Lock(Job->Mutex);
    if (Job->Joining || Job->Phase != JobPhase::Idle) return 0;
    if (Job->Thread.joinable()) Job->Thread.join();
    Job->Phase = JobPhase::Running;
    try {
        Job->Thread = std::thread([=] {
            uint32_t Hash = UINT32_C(2166136261);
            for (uint32_t Round = 0; Round < Rounds && Job->Phase == JobPhase::Running; ++Round)
                for (uint32_t Index = 0; Index < 32768; ++Index)
                    Hash = (Hash ^ (Index + Round)) * UINT32_C(16777619);
            const bool Canceled = Job->Phase.exchange(JobPhase::Delivering) == JobPhase::Canceled;
            Callback(Canceled ? -1 : static_cast<int32_t>((Hash & UINT32_C(0x3fffffff)) + 1));
            Job->Phase = JobPhase::Idle;
        });
    } catch (const std::system_error&) {
        Job->Phase = JobPhase::Idle;
        return 0;
    }
    return 1;
}
void LUI_FFI_CALL LuiFfiCancelJob() {
    if (auto* Job = GetJob()) {
        auto Expected = JobPhase::Running;
        Job->Phase.compare_exchange_strong(Expected, JobPhase::Canceled);
    }
}
uint32_t LUI_FFI_CALL LuiFfiJoinJob() {
    auto* Job = GetJob();
    if (!Job) return 0;
    std::thread Previous;
    {
        std::lock_guard<std::mutex> Lock(Job->Mutex);
        if (Job->Joining || Job->Thread.get_id() == std::this_thread::get_id()) return 0;
        if (!Job->Thread.joinable()) return 1;
        Job->Joining = true;
        Previous = std::move(Job->Thread);
    }
    Previous.join();
    std::lock_guard<std::mutex> Lock(Job->Mutex);
    Job->Joining = false;
    return 1;
}
