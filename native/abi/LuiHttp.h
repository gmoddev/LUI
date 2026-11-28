#ifndef LUI_HTTP_H
#define LUI_HTTP_H
#include "LuiRuntime.h"
#ifdef __cplusplus
extern "C" {
#endif
#define LUI_HTTP_PUBLIC_ONLY 1u
typedef struct LuiHttpOriginV1 {
    const char* Url;
    uint32_t UrlBytes;
    uint32_t Reserved;
} LuiHttpOriginV1;
/* Optional trusted-host restriction, configured once before startup/network use.
   Origins are exact http[s] scheme, ASCII host and effective port; no paths,
   credentials, fragments or wildcards. Empty list leaves legacy host authority.
   All existing NetworkPolicy restrictions remain conjunctive. PUBLIC_ONLY
   rejects nonpublic resolved endpoints before connect, including mapped IPv4.
   Defaults: request 1MiB, response 8MiB, concurrent 32, reservation 608MiB.
   Limits can only narrow these ceilings. MaxReservedBytes accounts conservative
   payload reservations, not total TLS/provider/OS or Luau allocator memory. */
typedef struct LuiHttpOptionsV1 {
    uint32_t StructSize, AbiVersion;
    uint32_t Flags, MaxRequestBytes, MaxResponseBytes, MaxConcurrent;
    uint64_t MaxReservedBytes;
    const LuiHttpOriginV1* Origins;
    uint32_t OriginCount, Reserved;
} LuiHttpOptionsV1;
typedef struct LuiHttpStatsV1 {
    uint32_t StructSize, AbiVersion;
    uint64_t LiveRequests, PeakRequests, ReservedBytes, PeakReservedBytes;
    uint64_t Accepted, Rejected, Completed;
    uint64_t LiveCancellationTokens;
} LuiHttpStatsV1;
LUI_API int LUI_CALL Lui_SetHttpOptionsV1(LuiRuntime* Runtime, const LuiHttpOptionsV1* Options);
/* Owner-only numerical snapshot. Does not start workers or allocate a domain. */
LUI_API int LUI_CALL Lui_GetHttpStatsV1(LuiRuntime* Runtime, LuiHttpStatsV1* Stats);
#ifdef __cplusplus
}
#endif
#endif
