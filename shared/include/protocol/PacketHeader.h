#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol/Command.h"

struct PacketHeader {
    uint16_t signature;
    uint32_t length;
    CMD::Type command;
    uint8_t md5_hash[16];
};

constexpr size_t PACKET_SIGNATURE_SIZE = sizeof(uint16_t);
constexpr size_t PACKET_LENGTH_SIZE = sizeof(uint32_t);
constexpr size_t PACKET_COMMAND_SIZE = sizeof(uint16_t);
constexpr size_t PACKET_MD5_SIZE = 16;

constexpr size_t PACKET_HEADER_SIZE =
    PACKET_SIGNATURE_SIZE +
    PACKET_LENGTH_SIZE +
    PACKET_COMMAND_SIZE +
    PACKET_MD5_SIZE;

constexpr uint16_t PACKET_SIGNATURE = 0xABCD;
constexpr uint32_t MAX_BODY_SIZE = 1048576;
