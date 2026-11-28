#pragma once
#include "LuiWebSocket.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <memory>
struct WebSocketRuntimeState {
    // Private, fault-stage-only qualification barrier; never enabled by hosts or Luau.
    std::mutex QualificationMutex;
    std::condition_variable QualificationChanged;
    bool QualificationCommitEntered=false,QualificationCommitReleased=false,QualificationCommitTimedOut=false;
    uint32_t MaxConnections = 8, MaxMessageBytes = 1024 * 1024, MaxFrameBytes = 1024 * 1024;
    uint32_t MaxQueuedBytes = 4 * 1024 * 1024, MaxQueuedMessages = 16;
    uint64_t MaxReservedBytes = 128ull * 1024 * 1024;
    bool Configured = false;
    // Private deterministic one-shot qualification seam, never exposed to Lua
    // or host ABI. Zero leaves production execution unchanged.
    std::atomic<int> FaultStage{0};
    // Stage 7 only: the worker publishes these before the terminal completion.
    // The fixture reads them after draining that completion's mutex barrier,
    // moves the strong pin out of State to break its test-only reservation
    // cycle, and releases it while the owning network io_context is still alive.
    std::shared_ptr<void> QualificationRetainedDial;
    std::weak_ptr<void> QualificationRetiredSocket;
    std::atomic<uint64_t> LiveConnections{0}, PeakConnections{0}, ReservedBytes{0}, PeakReservedBytes{0};
    std::atomic<uint64_t> InboundBytes{0}, OutboundBytes{0}, PendingSends{0}, ReceivedMessages{0}, SentMessages{0};
    std::atomic<uint64_t> Accepted{0}, Rejected{0}, Closed{0};
};
