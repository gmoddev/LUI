#ifndef LUI_FFI_FIXTURE_H
#define LUI_FFI_FIXTURE_H

#include <stdint.h>

#if defined(_WIN32)
# if defined(LUI_BUILD_FFI_FIXTURE)
#  define LUI_FFI_FIXTURE __declspec(dllexport)
# else
#  define LUI_FFI_FIXTURE __declspec(dllimport)
# endif
# define LUI_FFI_CALL __cdecl
#else
# define LUI_FFI_FIXTURE __attribute__((visibility("default")))
# define LUI_FFI_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

LUI_FFI_FIXTURE int8_t LUI_FFI_CALL LuiFfiEchoI8(int8_t Value);
LUI_FFI_FIXTURE uint8_t LUI_FFI_CALL LuiFfiEchoU8(uint8_t Value);
LUI_FFI_FIXTURE int16_t LUI_FFI_CALL LuiFfiEchoI16(int16_t Value);
LUI_FFI_FIXTURE uint16_t LUI_FFI_CALL LuiFfiEchoU16(uint16_t Value);
LUI_FFI_FIXTURE int32_t LUI_FFI_CALL LuiFfiEchoI32(int32_t Value);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiEchoU32(uint32_t Value);
LUI_FFI_FIXTURE int64_t LUI_FFI_CALL LuiFfiEchoI64(int64_t Value);
LUI_FFI_FIXTURE uint64_t LUI_FFI_CALL LuiFfiEchoU64(uint64_t Value);
LUI_FFI_FIXTURE float LUI_FFI_CALL LuiFfiEchoF32(float Value);
LUI_FFI_FIXTURE double LUI_FFI_CALL LuiFfiEchoF64(double Value);
LUI_FFI_FIXTURE intptr_t LUI_FFI_CALL LuiFfiEchoISize(intptr_t Value);
LUI_FFI_FIXTURE uintptr_t LUI_FFI_CALL LuiFfiEchoUSize(uintptr_t Value);
LUI_FFI_FIXTURE long LUI_FFI_CALL LuiFfiEchoLong(long Value);
LUI_FFI_FIXTURE uint8_t LUI_FFI_CALL LuiFfiBool8(uint8_t Value);
LUI_FFI_FIXTURE double LUI_FFI_CALL LuiFfiMix(int8_t A, double B, uint16_t C, float D,
    int32_t E, double F, uint32_t G, float H);
LUI_FFI_FIXTURE double LUI_FFI_CALL LuiFfiVariadicMix(int Seed, ...);
/* Divide returns 1 on success, 0 on failure. LastError is local to the calling
   native thread: 0 success, 1 null output, 2 division by zero, 3 overflow. */
LUI_FFI_FIXTURE int32_t LUI_FFI_CALL LuiFfiDivide(int32_t Value, int32_t Divisor, int32_t* Output);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiLastError(void);
LUI_FFI_FIXTURE int32_t LUI_FFI_CALL LuiFfiSetErrors(int32_t Value);

#pragma pack(push, 8)
typedef struct LuiFfiPair {
    int32_t Left;
    double Right;
} LuiFfiPair;
#pragma pack(pop)

typedef struct LuiFfiNested {
    uint8_t Tag;
    LuiFfiPair Pair;
    uint16_t Samples[3];
} LuiFfiNested;

#pragma pack(push, 2)
typedef struct LuiFfiPacked {
    uint8_t Tag;
    uint32_t Count;
    uint8_t Tail;
} LuiFfiPacked;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct LuiFfiPackedLarge {
    uint8_t Tag;
    uint64_t Value;
    uint16_t Flags;
} LuiFfiPackedLarge;
#pragma pack(pop)

typedef struct LuiFfiPackedContainer {
    uint8_t Header;
    LuiFfiPacked Rows[2];
    uint32_t Trailer;
} LuiFfiPackedContainer;

typedef enum LuiFfiLayoutField {
    LuiFfiPointerBytes, LuiFfiLongBytes, LuiFfiPairBytes, LuiFfiPairAlignment,
    LuiFfiLeftOffset, LuiFfiRightOffset, LuiFfiNestedBytes, LuiFfiNestedAlignment,
    LuiFfiTagOffset, LuiFfiNestedPairOffset, LuiFfiSamplesOffset,
    LuiFfiPackedBytes, LuiFfiPackedAlignment, LuiFfiPackedCountOffset, LuiFfiPackedTailOffset,
    LuiFfiContainerBytes, LuiFfiContainerAlignment, LuiFfiContainerRowsOffset, LuiFfiContainerTrailerOffset
} LuiFfiLayoutField;

LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiLayout(uint32_t Field);
LUI_FFI_FIXTURE LuiFfiPair LUI_FFI_CALL LuiFfiMakePair(int32_t Left, double Right);
LUI_FFI_FIXTURE LuiFfiPair LUI_FFI_CALL LuiFfiAdjustPair(LuiFfiPair Value, int32_t Delta);
LUI_FFI_FIXTURE double LUI_FFI_CALL LuiFfiVariadicPair(int Seed, ...);
LUI_FFI_FIXTURE double LUI_FFI_CALL LuiFfiSumPair(const LuiFfiPair* Value);
LUI_FFI_FIXTURE double LUI_FFI_CALL LuiFfiSumNested(const LuiFfiNested* Value);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiSumPacked(const LuiFfiPackedContainer* Value);
LUI_FFI_FIXTURE LuiFfiPacked LUI_FFI_CALL LuiFfiAdjustPacked(LuiFfiPacked Value, uint32_t Delta);
LUI_FFI_FIXTURE LuiFfiPackedLarge LUI_FFI_CALL LuiFfiAdjustPackedLarge(LuiFfiPackedLarge Value, uint32_t Delta);
LUI_FFI_FIXTURE void LUI_FFI_CALL LuiFfiAdjustPackedInto(LuiFfiPacked* Value, uint32_t Delta);
typedef int32_t (LUI_FFI_CALL *LuiFfiPackedCallback)(LuiFfiPacked Small, LuiFfiPackedLarge Large);
LUI_FFI_FIXTURE int32_t LUI_FFI_CALL LuiFfiCallPacked(LuiFfiPackedCallback Callback);

typedef struct LuiFfiOctets { uint8_t Bytes[8]; } LuiFfiOctets;
typedef struct LuiFfiWide { double Values[40]; } LuiFfiWide;
LUI_FFI_FIXTURE LuiFfiOctets LUI_FFI_CALL LuiFfiAdjustOctets(uint8_t Delta, LuiFfiOctets Value);
LUI_FFI_FIXTURE LuiFfiWide LUI_FFI_CALL LuiFfiAdjustWide(LuiFfiWide Value, double Delta);

/* Alloc accepts 1..65536 bytes. Release each allocation with its matching
   function. Borrowed Text has static lifetime while the library is loaded. */
typedef struct LuiFfiHandle LuiFfiHandle;
LUI_FFI_FIXTURE void* LUI_FFI_CALL LuiFfiAlloc(uint32_t Size);
LUI_FFI_FIXTURE void LUI_FFI_CALL LuiFfiFree(void* Memory);
LUI_FFI_FIXTURE LuiFfiHandle* LUI_FFI_CALL LuiFfiOpen(uint64_t Value);
LUI_FFI_FIXTURE uint64_t LUI_FFI_CALL LuiFfiRead(const LuiFfiHandle* Handle);
LUI_FFI_FIXTURE void LUI_FFI_CALL LuiFfiClose(LuiFfiHandle* Handle);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiLive(void);
LUI_FFI_FIXTURE const char* LUI_FFI_CALL LuiFfiText(void);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiMutate(uint8_t* Bytes, uint32_t Size, uint8_t Mask);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiHash(const uint8_t* Bytes, uint32_t Size);
/* Offset returns a borrowed identity or interior address. CountRelease only
   counts nonnull calls, allowing tests to observe ownership without freeing. */
LUI_FFI_FIXTURE void* LUI_FFI_CALL LuiFfiOffset(void* Bytes, uint32_t Size, uint32_t Offset);
typedef struct LuiFfiPointerNumber {
    void* Address;
    double Number;
} LuiFfiPointerNumber;
LUI_FFI_FIXTURE LuiFfiPointerNumber LUI_FFI_CALL LuiFfiPointerNumberResult(void* Address, uint32_t Finite);
typedef struct LuiFfiPointers { void* Entries[3]; } LuiFfiPointers;
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiCountNulls(LuiFfiPointers Value);
LUI_FFI_FIXTURE void LUI_FFI_CALL LuiFfiCountRelease(void* Memory);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiReleaseCount(void);

typedef int32_t (LUI_FFI_CALL *LuiFfiCallback)(int32_t Value);
typedef void (LUI_FFI_CALL *LuiFfiNotification)(int32_t Value);
LUI_FFI_FIXTURE int32_t LUI_FFI_CALL LuiFfiCall(LuiFfiCallback Callback, int32_t Value);
LUI_FFI_FIXTURE int32_t LUI_FFI_CALL LuiFfiCallOnThread(LuiFfiCallback Callback, int32_t Value);
LUI_FFI_FIXTURE void LUI_FFI_CALL LuiFfiRememberComputed(LuiFfiCallback Callback);
LUI_FFI_FIXTURE int32_t LUI_FFI_CALL LuiFfiCallRemembered(int32_t Value);
LUI_FFI_FIXTURE void LUI_FFI_CALL LuiFfiNotify(LuiFfiNotification Callback, int32_t Value);
LUI_FFI_FIXTURE void LUI_FFI_CALL LuiFfiNotifyOnThread(LuiFfiNotification Callback, int32_t Value);
/* A single process-wide retained notification. Unregister stops new deliveries.
   Repeat it until it returns 1 before releasing the callback trampoline. */
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiRegister(LuiFfiNotification Callback);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiEmit(int32_t Value);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiUnregister(void);
/* Joined callbacks end before return. Wait accepts 0..250 ms. Waiting exposes
   the current count so lifecycle tests can observe an in-flight native call. */
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiWait(uint32_t Milliseconds);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiWaiting(void);
/* One background job, 1..4096 rounds. Its callback receives a positive
   checksum or -1 for cancellation. Cancel signals without waiting. Join waits
   for callback retirement, returns 1 if idle/joined, 0 for a concurrent join
   or an attempted join from the callback itself. */
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiStartJob(LuiFfiNotification Callback, uint32_t Rounds);
LUI_FFI_FIXTURE void LUI_FFI_CALL LuiFfiCancelJob(void);
LUI_FFI_FIXTURE uint32_t LUI_FFI_CALL LuiFfiJoinJob(void);

#ifdef __cplusplus
}
#endif
#endif
