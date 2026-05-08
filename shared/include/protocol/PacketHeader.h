#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol/Command.h"

#pragma pack(push, 1)

struct PacketHeader {
    uint16_t signature;
    uint32_t length;
    CMD::Type command;
    uint8_t md5_hash[16];
};

#pragma pack(pop)

constexpr size_t PACKET_HEADER_SIZE = sizeof(PacketHeader);
constexpr uint16_t PACKET_SIGNATURE = 0xABCD;
constexpr uint32_t MAX_BODY_SIZE = 1048576;
