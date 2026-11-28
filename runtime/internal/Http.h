#pragma once
#include "LuiHttp.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>
struct HttpRuntimeState {
    uint32_t Flags = 0, MaxRequestBytes = 1024 * 1024, MaxResponseBytes = 8 * 1024 * 1024, MaxConcurrent = 32;
    uint64_t MaxReservedBytes = 608ull * 1024 * 1024;
    std::vector<std::string> Origins;
    bool Configured = false;
    std::atomic<uint64_t> LiveRequests{0}, PeakRequests{0}, ReservedBytes{0}, PeakReservedBytes{0};
    std::atomic<uint64_t> Accepted{0}, Rejected{0}, Completed{0};
    std::atomic<uint64_t> LiveCancellationTokens{0};
};
