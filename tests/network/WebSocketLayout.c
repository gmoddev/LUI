#include "LuiWebSocket.h"
#include <stddef.h>

_Static_assert(sizeof(void*) == 8, "WebSocket host profile is x64");
_Static_assert(sizeof(LuiWebSocketOptionsV1) == 40, "WebSocket options layout");
_Static_assert(offsetof(LuiWebSocketOptionsV1, MaxReservedBytes) == 32, "WebSocket budget alignment");
_Static_assert(sizeof(LuiWebSocketStatsV1) == 104, "WebSocket stats layout");
_Static_assert(offsetof(LuiWebSocketStatsV1, LiveConnections) == 8, "WebSocket counters alignment");

int Lui_WebSocketLayoutProof(void) { return 1; }
