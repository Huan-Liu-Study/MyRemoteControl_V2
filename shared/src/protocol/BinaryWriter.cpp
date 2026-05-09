#include "protocol/BinaryWriter.h"

#include <limits>
#include <stdexcept>
#include <winsock2.h>

void BinaryWriter::writeUint16(uint16_t value)
{
    uint16_t networkValue = htons(value);
    writeBytes(reinterpret_cast<const uint8_t*>(&networkValue), sizeof(networkValue));
}

void BinaryWriter::writeUint32(uint32_t value)
{
    uint32_t networkValue = htonl(value);
    writeBytes(reinterpret_cast<const uint8_t*>(&networkValue), sizeof(networkValue));
}

void BinaryWriter::writeUint64(uint64_t value)
{
    writeUint32(static_cast<uint32_t>(value >> 32));
    writeUint32(static_cast<uint32_t>(value & 0xFFFFFFFFu));
}

void BinaryWriter::writeBytes(const uint8_t* data, size_t length)
{
    buffer_.insert(buffer_.end(), data, data + length);
}

void BinaryWriter::writeString(const std::string& text)
{
    if (text.size() > (std::numeric_limits<uint32_t>::max)()) {
        throw std::length_error("String is too large to serialize.");
    }

    writeUint32(static_cast<uint32_t>(text.size()));

    writeBytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

const ByteBuffer& BinaryWriter::buffer() const
{
    return buffer_;
}
