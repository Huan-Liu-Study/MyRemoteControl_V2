#pragma once

#include <cstdint>
#include <string>
#include <vector>

using ByteBuffer = std::vector<uint8_t>;

class BinaryWriter {
public:
    void writeUint32(uint32_t value);
    void writeUint64(uint64_t value);
    void writeString(const std::string& text);

    const ByteBuffer& buffer() const;

private:
    ByteBuffer buffer_;
};
