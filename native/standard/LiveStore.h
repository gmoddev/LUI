#pragma once
#include "ByteWorker.h"
#include <memory>
#include <sqlite3.h>

namespace Lui::Standard {
class LiveStore {
public:
    struct State;
    explicit LiveStore(const char* Root);
    ~LiveStore();
    uint32_t Open(unsigned WaitMs,bool Create,const std::atomic<bool>& Canceled,sqlite3** Db);
    void Unlock() noexcept;
private:
    std::unique_ptr<State> Data;
};
uint32_t ProcessLive(LiveStore* Store,uint32_t Method,const uint8_t* Bytes,size_t Size,
    const std::atomic<bool>& Canceled,std::vector<uint8_t>& Output);
}
