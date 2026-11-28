/* Direct C compiler-generated calls provide a reference independent of libffi
   and the Luau binding. Failures go to stderr and never open a dialog. */
#include "Fixture.h"
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define LUI_LOCAL __declspec(thread)
#define LUI_ALIGNOF(Type) __alignof(Type)
static double Now(void) {
    LARGE_INTEGER Counter, Frequency;
    QueryPerformanceCounter(&Counter);
    QueryPerformanceFrequency(&Frequency);
    return (double)Counter.QuadPart / (double)Frequency.QuadPart;
}
#else
#include <time.h>
#define LUI_LOCAL _Thread_local
#define LUI_ALIGNOF(Type) _Alignof(Type)
static double Now(void) {
    struct timespec Counter;
    clock_gettime(CLOCK_MONOTONIC, &Counter);
    return (double)Counter.tv_sec + (double)Counter.tv_nsec * 1e-9;
}
#endif

#define CHECK(Condition) do { if (!(Condition)) { \
    fprintf(stderr, "[LUI:FfiOracle] line %d failed: %s\n", __LINE__, #Condition); return 0; \
} } while (0)

static int Scalars(void) {
    CHECK(LuiFfiEchoI8(INT8_MIN) == INT8_MIN && LuiFfiEchoI8(INT8_MAX) == INT8_MAX);
    CHECK(LuiFfiEchoU8(UINT8_MAX) == UINT8_MAX && LuiFfiEchoU8(0) == 0);
    CHECK(LuiFfiEchoI16(INT16_MIN) == INT16_MIN && LuiFfiEchoI16(INT16_MAX) == INT16_MAX);
    CHECK(LuiFfiEchoU16(UINT16_MAX) == UINT16_MAX && LuiFfiEchoU16(0) == 0);
    CHECK(LuiFfiEchoI32(INT32_MIN) == INT32_MIN && LuiFfiEchoI32(INT32_MAX) == INT32_MAX);
    CHECK(LuiFfiEchoU32(UINT32_MAX) == UINT32_MAX && LuiFfiEchoU32(0) == 0);
    CHECK(LuiFfiEchoI64(INT64_MIN) == INT64_MIN && LuiFfiEchoI64(INT64_MAX) == INT64_MAX);
    CHECK(LuiFfiEchoU64(UINT64_MAX) == UINT64_MAX && LuiFfiEchoU64(0) == 0);
    CHECK(LuiFfiEchoI64(-INT64_C(9007199254740993)) == -INT64_C(9007199254740993));
    CHECK(LuiFfiEchoU64(UINT64_C(9007199254740993)) == UINT64_C(9007199254740993));
    CHECK(LuiFfiEchoISize(INTPTR_MIN) == INTPTR_MIN && LuiFfiEchoISize(INTPTR_MAX) == INTPTR_MAX);
    CHECK(LuiFfiEchoUSize(UINTPTR_MAX) == UINTPTR_MAX);
    CHECK(LuiFfiEchoLong(LONG_MIN) == LONG_MIN && LuiFfiEchoLong(LONG_MAX) == LONG_MAX);
    CHECK(LuiFfiBool8(0) == 0 && LuiFfiBool8(1) == 1 && LuiFfiBool8(255) == 1);
    CHECK(LuiFfiEchoF32(FLT_MAX) == FLT_MAX && LuiFfiEchoF32(-1.25f) == -1.25f);
    CHECK(LuiFfiEchoF64(DBL_MIN) == DBL_MIN && LuiFfiEchoF64(0.125) == 0.125);
    {
        const float F = -0.0f, FResult = LuiFfiEchoF32(F);
        const double D = -0.0, DResult = LuiFfiEchoF64(D);
        CHECK(memcmp(&F, &FResult, sizeof(F)) == 0 && memcmp(&D, &DResult, sizeof(D)) == 0);
    }
    CHECK(LuiFfiMix(-7, 1.5, 65000, -2.25f, -3000, .125, 123456, 4.0f) == 1044211.75);
    CHECK(LuiFfiVariadicMix(10, (int8_t)-7, 1.5f, (uint16_t)65000, -2.25f, UINT64_MAX, (const char*)"A") == 65322.25);
    return 1;
}

static int Structures(void) {
    const uint32_t Expected[] = {
        sizeof(void*), sizeof(long), sizeof(LuiFfiPair), LUI_ALIGNOF(LuiFfiPair),
        offsetof(LuiFfiPair, Left), offsetof(LuiFfiPair, Right),
        sizeof(LuiFfiNested), LUI_ALIGNOF(LuiFfiNested), offsetof(LuiFfiNested, Tag),
        offsetof(LuiFfiNested, Pair), offsetof(LuiFfiNested, Samples),
        sizeof(LuiFfiPacked), LUI_ALIGNOF(LuiFfiPacked), offsetof(LuiFfiPacked, Count), offsetof(LuiFfiPacked, Tail),
        sizeof(LuiFfiPackedContainer), LUI_ALIGNOF(LuiFfiPackedContainer),
        offsetof(LuiFfiPackedContainer, Rows), offsetof(LuiFfiPackedContainer, Trailer)
    };
    uint32_t Index;
    LuiFfiPair Original = LuiFfiMakePair(-42, .25);
    LuiFfiPair Shifted = LuiFfiAdjustPair(Original, -20);
    LuiFfiNested Nested = {9, {-42, .25}, {1, 2, 4}};
    LuiFfiPackedContainer Packed = {7, {{1, 250, 3}, {4, 413, 6}}, 8};
    for (Index = 0; Index < sizeof(Expected)/sizeof(Expected[0]); ++Index)
        CHECK(LuiFfiLayout(Index) == Expected[Index]);
    CHECK(LuiFfiLayout(UINT32_MAX) == UINT32_MAX);
    CHECK(Original.Left == -42 && Original.Right == .25);
    CHECK(Shifted.Left == -42 && Shifted.Right == -19.75);
    CHECK(LuiFfiVariadicPair(3, 4, Original, 2.0) == -28.5);
    CHECK(LuiFfiSumPair(&Original) == -41.75 && LuiFfiSumNested(&Nested) == -15.75);
    CHECK(LuiFfiSumPair(NULL) == 0 && LuiFfiSumNested(NULL) == 0);
    CHECK(LuiFfiSumPacked(&Packed) == 692);
    CHECK(sizeof(LuiFfiPackedLarge) == 11 && offsetof(LuiFfiPackedLarge, Value) == 1);
    {
        LuiFfiPacked Small = LuiFfiAdjustPacked(Packed.Rows[0], 7);
        LuiFfiPackedLarge LargeInput = {2, UINT64_C(0x123456789abcdef0), 7};
        LuiFfiPackedLarge Large = LuiFfiAdjustPackedLarge(LargeInput, 2);
        CHECK(Small.Tag == 1 && Small.Count == 257 && Small.Tail == 3);
        CHECK(Large.Tag == 2 && Large.Value == UINT64_C(0x123456789abcdef2) && Large.Flags == 0x52);
        LuiFfiAdjustPackedInto(&Small, 3);
        CHECK(Small.Tag == 1 && Small.Count == 260 && Small.Tail == 3);
    }
    return 1;
}

static int Memory(void) {
    const uint8_t Original[] = {0, 1, 127, 128, 255};
    const uint8_t Mutated[] = {90, 91, 37, 218, 165};
    const uint64_t Token = UINT64_C(0xfedcba9876543210);
    uint8_t* Buffer = (uint8_t*)LuiFfiAlloc(7);
    LuiFfiHandle* Object = LuiFfiOpen(Token);
    CHECK(Buffer && Object && LuiFfiLive() == 2 && LuiFfiRead(Object) == Token);
    {
        LuiFfiPointerNumber Pair = LuiFfiPointerNumberResult(Buffer, 1);
        CHECK(Pair.Address == Buffer && Pair.Number == 2.5);
    }
    CHECK(LuiFfiOffset(Buffer, 7, 0) == Buffer && LuiFfiOffset(Buffer, 7, 3) == Buffer + 3);
    CHECK(LuiFfiOffset(Buffer, 7, 7) == Buffer + 7 && LuiFfiOffset(Buffer, 7, 8) == NULL);
    LuiFfiCountRelease(Buffer);
    LuiFfiCountRelease(NULL);
    CHECK(LuiFfiReleaseCount() == 1 && LuiFfiLive() == 2);
    CHECK(memcmp(Buffer, "\0\0\0\0\0\0", 7) == 0);
    Buffer[0] = Buffer[6] = 55;
    memcpy(Buffer + 1, Original, sizeof(Original));
    CHECK(LuiFfiHash(Buffer + 1, 5) == UINT32_C(0xd9901cb6));
    CHECK(LuiFfiMutate(Buffer + 1, 5, 90) == 5);
    CHECK(memcmp(Buffer + 1, Mutated, 5) == 0 && Buffer[0] == 55 && Buffer[6] == 55);
    CHECK(LuiFfiHash(Buffer + 1, 5) == UINT32_C(0xb006e8f8));
    CHECK(LuiFfiMutate(Buffer + 1, 65537, 1) == UINT32_MAX && memcmp(Buffer + 1, Mutated, 5) == 0);
    CHECK(LuiFfiMutate(NULL, 0, 1) == 0 && LuiFfiMutate(NULL, 1, 1) == UINT32_MAX);
    CHECK(LuiFfiHash(NULL, 0) == UINT32_C(2166136261));
    CHECK(LuiFfiAlloc(0) == NULL && LuiFfiAlloc(65537) == NULL);
    CHECK(!strcmp(LuiFfiText(), "caf\xc3\xa9") && LuiFfiHash((const uint8_t*)LuiFfiText(), 5) == UINT32_C(0xa82b5049));
    CHECK(LuiFfiRead(NULL) == 0);
    LuiFfiFree(NULL);
    LuiFfiClose(NULL);
    CHECK(LuiFfiLive() == 2);
    LuiFfiClose(Object);
    LuiFfiFree(Buffer);
    CHECK(LuiFfiLive() == 0);
    Buffer = (uint8_t*)LuiFfiAlloc(65536);
    CHECK(Buffer && LuiFfiMutate(Buffer, 65536, 7) == 65536 && Buffer[65535] == 7);
    LuiFfiFree(Buffer);
    CHECK(LuiFfiLive() == 0);
    return 1;
}

static LUI_LOCAL int OwnerThread;
static int32_t Observed;
static int Notifications, OnOwner, Reentered;
static int32_t LUI_FFI_CALL AddOne(int32_t Value) { return Value + 1; }
static int32_t LUI_FFI_CALL ComputePacked(LuiFfiPacked Small, LuiFfiPackedLarge Large) {
    if (Small.Tag != 2 || Small.Count != 127 || Small.Tail != 3 ||
        Large.Tag != 4 || Large.Value != UINT64_C(0x123456789abcdef0) || Large.Flags != 5) return -2;
    return 51;
}
static int32_t LUI_FFI_CALL SetForeignError(int32_t Value) {
    return LuiFfiDivide(Value, 1, NULL) == 0 && LuiFfiLastError() == 1;
}
static int Errors(void) {
    int32_t Output = 27;
    CHECK(LuiFfiDivide(1, 0, &Output) == 0 && LuiFfiLastError() == 2 && Output == 27);
    CHECK(LuiFfiCallOnThread(SetForeignError, 5) == 1 && LuiFfiLastError() == 2);
    CHECK(LuiFfiDivide(INT32_MIN, -1, &Output) == 0 && LuiFfiLastError() == 3 && Output == 27);
    CHECK(LuiFfiDivide(-100, 9, &Output) == 1 && LuiFfiLastError() == 0 && Output == -11);
    return 1;
}
static void LUI_FFI_CALL Observe(int32_t Value) {
    Observed = Value;
    OnOwner = OwnerThread;
    ++Notifications;
}
static int32_t LUI_FFI_CALL Transform(int32_t Value) {
    Observe(Value);
    return LuiFfiEchoI32(Value) - 3;
}
static void LUI_FFI_CALL RetireDuringDelivery(int32_t Value) {
    Observe(Value);
    Reentered = LuiFfiUnregister() == 0 && LuiFfiRegister(Observe) == 0 && LuiFfiEmit(Value) == 0;
}
static int Callbacks(void) {
    CHECK(LuiFfiCallPacked(ComputePacked) == 51);
    OwnerThread = 1;
    CHECK(LuiFfiCall(Transform, -37) == -40 && OnOwner && Observed == -37);
    CHECK(LuiFfiCallOnThread(Transform, 45) == 42 && !OnOwner && Observed == 45);
    LuiFfiNotify(Observe, -7);
    CHECK(OnOwner && Observed == -7 && Notifications == 3);
    LuiFfiNotifyOnThread(Observe, 39);
    CHECK(!OnOwner && Observed == 39 && Notifications == 4);
    CHECK(LuiFfiCall(NULL, 0) == INT32_MIN && LuiFfiCallOnThread(NULL, 0) == INT32_MIN);
    LuiFfiNotify(NULL, 7);
    LuiFfiNotifyOnThread(NULL, 8);
    CHECK(LuiFfiRegister(NULL) == 0 && LuiFfiRegister(RetireDuringDelivery) == 1);
    CHECK(LuiFfiRegister(Observe) == 0 && LuiFfiEmit(31) == 1);
    CHECK(Reentered && Observed == 31 && OnOwner && Notifications == 5);
    CHECK(LuiFfiEmit(45) == 0 && LuiFfiUnregister() == 1);
    CHECK(LuiFfiRegister(Observe) == 1 && LuiFfiEmit(21) == 1 && Observed == 21);
    CHECK(LuiFfiUnregister() == 1 && LuiFfiUnregister() == 1 && LuiFfiEmit(22) == 0);
    CHECK(Notifications == 6);
    return 1;
}

static int Blocking(void) {
    double Started = Now();
    CHECK(LuiFfiWait(15) == 15 && Now() - Started >= .010);
    CHECK(LuiFfiWaiting() == 0 && LuiFfiWait(251) == UINT32_MAX && LuiFfiWait(0) == 0);
    return 1;
}
static int32_t JobResult;
static int JobCompletions, JobSelfJoin;
static void LUI_FFI_CALL ObserveJob(int32_t Value) {
    JobResult = Value;
    ++JobCompletions;
    JobSelfJoin = LuiFfiJoinJob();
}
static int Jobs(void) {
    uint32_t Index, Expected = UINT32_C(2166136261);
    for (Index = 0; Index < 32768; ++Index) Expected = (Expected ^ Index) * UINT32_C(16777619);
    CHECK(!LuiFfiStartJob(NULL, 5) && !LuiFfiStartJob(ObserveJob, 0) && !LuiFfiStartJob(ObserveJob, 4097));
    CHECK(LuiFfiStartJob(ObserveJob, 1) && LuiFfiJoinJob());
    CHECK(JobResult == (int32_t)((Expected & UINT32_C(0x3fffffff)) + 1) && !JobSelfJoin && JobCompletions == 1);
    CHECK(LuiFfiStartJob(ObserveJob, 4096) && !LuiFfiStartJob(ObserveJob, 1));
    LuiFfiCancelJob();
    CHECK(LuiFfiJoinJob() && JobResult == -1 && JobCompletions == 2 && !JobSelfJoin);
    CHECK(LuiFfiJoinJob());
    return 1;
}

static int Benchmark(void) {
    const uint32_t Calls = UINT32_C(1048576);
    uint32_t Index, Value = 0, CallbacksSum = 0;
    double Start, ScalarSeconds, CallbackSeconds;
    for (Index = 0; Index < 4096; ++Index) (void)LuiFfiEchoU32(Index);
    Start = Now();
    for (Index = 0; Index < Calls; ++Index) Value = LuiFfiEchoU32(Value + 1);
    ScalarSeconds = Now() - Start;
    Start = Now();
    for (Index = 0; Index < Calls; ++Index) CallbacksSum += (uint32_t)LuiFfiCall(AddOne, Index & 255);
    CallbackSeconds = Now() - Start;
    CHECK(Value == Calls && CallbacksSum == (Calls/256) * UINT32_C(32896));
    printf("{\"Calls\":%" PRIu32 ",\"ScalarNsPerCall\":%.3f,\"CallbackNsPerCall\":%.3f,"
           "\"ScalarChecksum\":%" PRIu32 ",\"CallbackChecksum\":%" PRIu32 "}\n",
        Calls, ScalarSeconds * 1e9 / Calls, CallbackSeconds * 1e9 / Calls, Value, CallbacksSum);
    return 1;
}

int main(int Count, char** Arguments) {
    if (Count == 2 && strcmp(Arguments[1], "--benchmark") == 0) return Benchmark() ? 0 : 1;
    if (Count != 1) return 2;
    if (!Scalars() || !Structures() || !Memory() || !Errors() || !Callbacks() || !Blocking() || !Jobs()) return 1;
    printf("{\"Passed\":true,\"PointerBytes\":%zu,\"LongBytes\":%zu,\"PairBytes\":%zu,"
           "\"PairAlignment\":%zu,\"PairRightOffset\":%zu,\"NestedBytes\":%zu,"
           "\"NestedPairOffset\":%zu,\"SamplesOffset\":%zu,\"Unsigned64Hex\":\"%016" PRIx64 "\","
           "\"Mixed\":1044211.75,\"InputHash\":\"d9901cb6\",\"MutatedHash\":\"b006e8f8\","
           "\"Notifications\":%d,\"Live\":%" PRIu32 "}\n",
        sizeof(void*), sizeof(long), sizeof(LuiFfiPair), (size_t)LUI_ALIGNOF(LuiFfiPair),
        offsetof(LuiFfiPair, Right), sizeof(LuiFfiNested), offsetof(LuiFfiNested, Pair),
        offsetof(LuiFfiNested, Samples), LuiFfiEchoU64(UINT64_MAX), Notifications, LuiFfiLive());
    return 0;
}
