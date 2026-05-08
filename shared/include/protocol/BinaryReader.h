#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using ByteBuffer = std::vector<uint8_t>;

class BinaryReader {
public:
    explicit BinaryReader(const ByteBuffer& buffer);
    
    bool readUint16(uint16_t& outValue);
    bool readUint32(uint32_t& outValue);
    bool readUint64(uint64_t& outValue);
    bool readBytes(uint8_t* outData, size_t length);
    bool readString(std::string& outText);
    bool isFinished() const;

private:
    const ByteBuffer& buffer_;
    size_t offset_ = 0;
};
