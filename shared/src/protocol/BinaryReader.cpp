#include "protocol/BinaryReader.h"

#include <cstring>

BinaryReader::BinaryReader(const ByteBuffer& buffer)
    : buffer_(buffer)
{
}

bool BinaryReader::readUint16(uint16_t& outValue)
{
    if (offset_ + sizeof(uint16_t) > buffer_.size()) {
        return false;
    }

    outValue = (static_cast<uint16_t>(buffer_[offset_]) << 8)
        | static_cast<uint16_t>(buffer_[offset_ + 1]);
    offset_ += sizeof(uint16_t);
    return true;
}

bool BinaryReader::readUint32(uint32_t& outValue)
{
    if (offset_ + sizeof(uint32_t) > buffer_.size()) {
        return false;
    }

    outValue = (static_cast<uint32_t>(buffer_[offset_]) << 24)
        | (static_cast<uint32_t>(buffer_[offset_ + 1]) << 16)
        | (static_cast<uint32_t>(buffer_[offset_ + 2]) << 8)
        | static_cast<uint32_t>(buffer_[offset_ + 3]);
    offset_ += sizeof(uint32_t);
    return true;
}

bool BinaryReader::readUint64(uint64_t& outValue)
{
    uint32_t high = 0;
    uint32_t low = 0;
    if (!readUint32(high) || !readUint32(low)) {
        return false;
    }

    outValue = (static_cast<uint64_t>(high) << 32) | low;
    return true;
}

bool BinaryReader::readBytes(uint8_t* outData, size_t length)
{
    if (offset_ + length > buffer_.size()) {
        return false;
    }

    std::memcpy(outData, buffer_.data() + offset_, length);
    offset_ += length;
    return true;
}

bool BinaryReader::readString(std::string& outText)
{
    uint32_t length = 0;
    if (!readUint32(length)) {
        return false;
    }

    if (offset_ + length > buffer_.size()) {
        return false;
    }

    outText.assign(reinterpret_cast<const char*>(buffer_.data() + offset_), length);
    offset_ += length;
    return true;
}

bool BinaryReader::isFinished() const
{
    return offset_ == buffer_.size();
}
