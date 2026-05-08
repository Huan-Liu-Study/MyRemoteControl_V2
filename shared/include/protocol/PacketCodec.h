#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "protocol/Command.h"
#include "protocol/PacketHeader.h"

using ByteBuffer = std::vector<uint8_t>;

struct ParsedPacket {
    PacketHeader header{};
    ByteBuffer payload;
};

class PacketCodec {
public:
    static ByteBuffer stringToBytes(const std::string& text);
    static std::string bytesToString(const ByteBuffer& bytes);

    static ByteBuffer pack(CMD::Type command, const ByteBuffer& payload = {});
    static ByteBuffer pack(CMD::Type command, const std::string& text);

    static bool decodeHeader(const ByteBuffer& bytes, PacketHeader& outHeader);
    static bool unpack(const PacketHeader& header, const ByteBuffer& payload, ParsedPacket& outPacket);

private:
    static ByteBuffer encodeHeader(const PacketHeader& header);
    static void xorCrypt(ByteBuffer& data);
    static void calculateMD5(const ByteBuffer& data, uint8_t outHash[16]);
};
