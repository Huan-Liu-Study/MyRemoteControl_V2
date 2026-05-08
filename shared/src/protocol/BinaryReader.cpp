#include "protocol/BinaryReader.h"

#include <cstring>

BinaryReader::BinaryReader(const ByteBuffer& buffer)
    : buffer_(buffer)
{
}

bool BinaryReader::readUint32(uint32_t& outValue)
{
    if (offset_ + sizeof(uint32_t) > buffer_.size()) {
        return false;
    }

    std::memcpy(&outValue, buffer_.data() + offset_, sizeof(uint32_t));
    offset_ += sizeof(uint32_t);
    return true;
}

bool BinaryReader::readUint64(uint64_t& outValue)
{
    if (offset_ + sizeof(uint64_t) > buffer_.size()) {
        return false;
    }

    std::memcpy(&outValue, buffer_.data() + offset_, sizeof(uint64_t));
    offset_ += sizeof(uint64_t);
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
