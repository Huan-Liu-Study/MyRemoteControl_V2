#include "protocol/BinaryWriter.h"

#include <cstring>

void BinaryWriter::writeUint32(uint32_t value)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    buffer_.insert(buffer_.end(), bytes, bytes + sizeof(value));
}

void BinaryWriter::writeUint64(uint64_t value)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    buffer_.insert(buffer_.end(), bytes, bytes + sizeof(value));
}

void BinaryWriter::writeString(const std::string& text)
{
    writeUint32(static_cast<uint32_t>(text.size()));

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(text.data());
    buffer_.insert(buffer_.end(), bytes, bytes + text.size());
}

const ByteBuffer& BinaryWriter::buffer() const
{
    return buffer_;
}
