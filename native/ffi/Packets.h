#pragma once
#include <string_view>

namespace Lui::Ffi {
inline constexpr std::string_view BinaryTag = "LFB1";
inline bool IsBinaryPacket(std::string_view Packet) {
    return Packet.substr(0, BinaryTag.size()) == BinaryTag;
}
inline bool IsWorkerBinaryPacket(std::string_view Packet) {
    return IsBinaryPacket(Packet) && Packet.size()>BinaryTag.size() &&
        std::string_view("CERWADHJ").find(Packet[BinaryTag.size()]) != std::string_view::npos;
}
}
