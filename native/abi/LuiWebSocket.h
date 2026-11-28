#ifndef LUI_WEBSOCKET_H
#define LUI_WEBSOCKET_H
#include "LuiRuntime.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Limits are shared by retained outbound and accepted hosted WebSocket roots.
   Optional owner-only narrowing before application/network startup. Outbound
   ws/wss uses HTTP origin/public-address and client NetworkPolicy restrictions,
   mapped to http/https. Hosted sockets use server bind policy and RouteWebSocket
   Origin/Authorize rules. Configuring limits grants no authority. */
typedef struct LuiWebSocketOptionsV1 {
    uint32_t StructSize, AbiVersion;
    uint32_t MaxConnections, MaxMessageBytes, MaxFrameBytes;
    uint32_t MaxQueuedBytes, MaxQueuedMessages, Reserved;
    uint64_t MaxReservedBytes;
} LuiWebSocketOptionsV1;
typedef struct LuiWebSocketStatsV1 {
    uint32_t StructSize, AbiVersion;
    uint64_t LiveConnections, PeakConnections, ReservedBytes, PeakReservedBytes;
    uint64_t InboundBytes, OutboundBytes, PendingSends, ReceivedMessages, SentMessages;
    uint64_t Accepted, Rejected, Closed;
} LuiWebSocketStatsV1;
LUI_API int LUI_CALL Lui_SetWebSocketOptionsV1(LuiRuntime* Runtime, const LuiWebSocketOptionsV1* Options);
LUI_API int LUI_CALL Lui_GetWebSocketStatsV1(LuiRuntime* Runtime, LuiWebSocketStatsV1* Stats);
#ifdef __cplusplus
}
#endif
#endif
