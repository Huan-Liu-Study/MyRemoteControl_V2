#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using ByteBuffer = std::vector<uint8_t>;

class BinaryWriter {
public:
    void writeUint16(uint16_t value);
    void writeUint32(uint32_t value);
    void writeUint64(uint64_t value);
    void writeBytes(const uint8_t* data, size_t length);
    void writeString(const std::string& text);

    const ByteBuffer& buffer() const;

private:
    ByteBuffer buffer_;
};
