#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "protocol/BinaryReader.h"
#include "protocol/BinaryWriter.h"

namespace {

void expectTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

uint32_t readNetworkUint32At(const ByteBuffer& buffer, size_t offset) {
    expectTrue(offset + sizeof(uint32_t) <= buffer.size(), "readUint32At out of range");

    return (static_cast<uint32_t>(buffer[offset]) << 24)
        | (static_cast<uint32_t>(buffer[offset + 1]) << 16)
        | (static_cast<uint32_t>(buffer[offset + 2]) << 8)
        | static_cast<uint32_t>(buffer[offset + 3]);
}

void testWriteUint32WritesFourBytes() {
    BinaryWriter writer;
    writer.writeUint32(123456u);

    const ByteBuffer& buffer = writer.buffer();
    expectTrue(buffer.size() == sizeof(uint32_t), "writeUint32 should write 4 bytes");
    expectTrue(readNetworkUint32At(buffer, 0) == 123456u, "writeUint32 stored wrong value");
}

void testWriteUint32UsesNetworkByteOrder() {
    BinaryWriter writer;
    writer.writeUint32(0x12345678u);

    const ByteBuffer& buffer = writer.buffer();
    expectTrue(buffer.size() == 4, "writeUint32 should write 4 bytes");
    expectTrue(buffer[0] == 0x12, "first byte should be high byte");
    expectTrue(buffer[1] == 0x34, "second byte mismatch");
    expectTrue(buffer[2] == 0x56, "third byte mismatch");
    expectTrue(buffer[3] == 0x78, "fourth byte should be low byte");
}

void testWriteStringWritesLengthAndContent() {
    BinaryWriter writer;
    writer.writeString("abc");

    const ByteBuffer& buffer = writer.buffer();
    expectTrue(buffer.size() == sizeof(uint32_t) + 3, "writeString(\"abc\") should write 7 bytes");
    expectTrue(readNetworkUint32At(buffer, 0) == 3u, "string length field should be 3");
    expectTrue(buffer[4] == static_cast<uint8_t>('a'), "first char should be a");
    expectTrue(buffer[5] == static_cast<uint8_t>('b'), "second char should be b");
    expectTrue(buffer[6] == static_cast<uint8_t>('c'), "third char should be c");
}

void testWriteEmptyStringOnlyWritesZeroLength() {
    BinaryWriter writer;
    writer.writeString("");

    const ByteBuffer& buffer = writer.buffer();
    expectTrue(buffer.size() == sizeof(uint32_t), "empty string should only write length field");
    expectTrue(readNetworkUint32At(buffer, 0) == 0u, "empty string length should be 0");
}

void testWriteMultipleFieldsKeepsCorrectOrder() {
    BinaryWriter writer;
    writer.writeUint32(2u);
    writer.writeString("C:\\");
    writer.writeString("D:\\");

    const ByteBuffer& buffer = writer.buffer();
    expectTrue(buffer.size() == 18, "two-drive sample should be 18 bytes");

    expectTrue(readNetworkUint32At(buffer, 0) == 2u, "first field should be count = 2");
    expectTrue(readNetworkUint32At(buffer, 4) == 3u, "first drive length should be 3");
    expectTrue(buffer[8] == static_cast<uint8_t>('C'), "first drive content mismatch");
    expectTrue(buffer[9] == static_cast<uint8_t>(':'), "first drive content mismatch");
    expectTrue(buffer[10] == static_cast<uint8_t>('\\'), "first drive content mismatch");

    expectTrue(readNetworkUint32At(buffer, 11) == 3u, "second drive length should be 3");
    expectTrue(buffer[15] == static_cast<uint8_t>('D'), "second drive content mismatch");
    expectTrue(buffer[16] == static_cast<uint8_t>(':'), "second drive content mismatch");
    expectTrue(buffer[17] == static_cast<uint8_t>('\\'), "second drive content mismatch");
}

void testReadUint32RoundTrip() {
    BinaryWriter writer;
    writer.writeUint32(123456u);

    BinaryReader reader(writer.buffer());
    uint32_t value = 0;
    expectTrue(reader.readUint32(value), "readUint32 should succeed");
    expectTrue(value == 123456u, "readUint32 returned wrong value");
}

void testReadStringRoundTrip() {
    BinaryWriter writer;
    writer.writeString("abc");

    BinaryReader reader(writer.buffer());
    std::string text;
    expectTrue(reader.readString(text), "readString should succeed");
    expectTrue(text == "abc", "readString returned wrong text");
}

void testReadMultipleFieldsRoundTrip() {
    BinaryWriter writer;
    writer.writeUint32(2u);
    writer.writeString("C:\\");
    writer.writeString("D:\\");

    BinaryReader reader(writer.buffer());
    uint32_t count = 0;
    std::string first;
    std::string second;

    expectTrue(reader.readUint32(count), "reader should read count");
    expectTrue(reader.readString(first), "reader should read first drive");
    expectTrue(reader.readString(second), "reader should read second drive");

    expectTrue(count == 2u, "count should be 2");
    expectTrue(first == "C:\\", "first drive text mismatch");
    expectTrue(second == "D:\\", "second drive text mismatch");
}

void testReadStringFailsWhenBodyIsIncomplete() {
    BinaryWriter writer;
    writer.writeUint32(5u);

    BinaryReader reader(writer.buffer());
    std::string text;
    expectTrue(!reader.readString(text), "readString should fail on incomplete payload");
}

} // namespace

int main() {
    try {
        testWriteUint32WritesFourBytes();
        testWriteUint32UsesNetworkByteOrder();
        testWriteStringWritesLengthAndContent();
        testWriteEmptyStringOnlyWritesZeroLength();
        testWriteMultipleFieldsKeepsCorrectOrder();
        testReadUint32RoundTrip();
        testReadStringRoundTrip();
        testReadMultipleFieldsRoundTrip();
        testReadStringFailsWhenBodyIsIncomplete();

        std::cout << "All binary reader/writer tests passed." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Binary reader/writer test failed: " << ex.what() << std::endl;
        return 1;
    }
}
