#pragma once
#include "LuiNativeServices.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Lui::Standard {
extern const LuiNativeServiceV2 Service;
uint32_t Process(uint32_t Method, const uint8_t* Bytes, size_t Size,
    const std::atomic<bool>& Canceled, std::vector<uint8_t>& Output);
}
