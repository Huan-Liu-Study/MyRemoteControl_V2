#include "protocol/BinaryWriter.h"

void BinaryWriter::writeUint16(uint16_t value)
{
    buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
}

void BinaryWriter::writeUint32(uint32_t value)
{
    buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
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
    writeUint32(static_cast<uint32_t>(text.size()));

    writeBytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

const ByteBuffer& BinaryWriter::buffer() const
{
    return buffer_;
}
