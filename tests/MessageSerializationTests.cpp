#include <iostream>
#include <stdexcept>
#include <string>

#include "protocol/BinaryWriter.h"
#include "protocol/Messages.h"

namespace {

void expectTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testDriveListResponseRoundTrip() {
    DriveListResponse response{{"C:\\", "D:\\"}};

    ByteBuffer payload = serializeDriveListResponse(response);

    DriveListResponse decoded{};
    expectTrue(deserializeDriveListResponse(payload, decoded), "deserialize should succeed");
    expectTrue(decoded.drives.size() == 2, "drive count should be 2");
    expectTrue(decoded.drives[0] == "C:\\", "first drive mismatch");
    expectTrue(decoded.drives[1] == "D:\\", "second drive mismatch");
}

void testEmptyDriveListRoundTrip() {
    DriveListResponse response{};

    ByteBuffer payload = serializeDriveListResponse(response);

    DriveListResponse decoded{};
    expectTrue(deserializeDriveListResponse(payload, decoded), "empty list deserialize should succeed");
    expectTrue(decoded.drives.empty(), "decoded drive list should be empty");
}

void testDeserializeFailsWhenPayloadIsIncomplete() {
    BinaryWriter writer;
    writer.writeUint32(2u);
    writer.writeString("C:\\");

    DriveListResponse decoded{};
    expectTrue(!deserializeDriveListResponse(writer.buffer(), decoded), "deserialize should fail on incomplete payload");
}

void testDeserializeFailsWhenPayloadHasTrailingBytes() {
    BinaryWriter writer;
    writer.writeUint32(1u);
    writer.writeString("C:\\");
    writer.writeUint32(999u);

    DriveListResponse decoded{};
    expectTrue(!deserializeDriveListResponse(writer.buffer(), decoded), "deserialize should fail on trailing bytes");
}

void testListDirRequestRoundTrip() {
    ListDirRequest request{"C:\\"};

    ByteBuffer payload = serializeListDirRequest(request);

    ListDirRequest decoded{};
    expectTrue(deserializeListDirRequest(payload, decoded), "list dir request deserialize should succeed");
    expectTrue(decoded.path == "C:\\", "list dir request path mismatch");
}

void testListDirResponseRoundTrip() {
    ListDirResponse response{{
        {"Windows", 1u},
        {"boot.ini", 0u}
    }};

    ByteBuffer payload = serializeListDirResponse(response);

    ListDirResponse decoded{};
    expectTrue(deserializeListDirResponse(payload, decoded), "list dir response deserialize should succeed");
    expectTrue(decoded.entries.size() == 2, "entry count should be 2");
    expectTrue(decoded.entries[0].name == "Windows", "first entry name mismatch");
    expectTrue(decoded.entries[0].isDirectory == 1u, "first entry type mismatch");
    expectTrue(decoded.entries[1].name == "boot.ini", "second entry name mismatch");
    expectTrue(decoded.entries[1].isDirectory == 0u, "second entry type mismatch");
}

void testListDirRequestFailsWhenPayloadHasTrailingBytes() {
    BinaryWriter writer;
    writer.writeString("C:\\");
    writer.writeUint32(999u);

    ListDirRequest decoded{};
    expectTrue(!deserializeListDirRequest(writer.buffer(), decoded), "list dir request should fail on trailing bytes");
}

void testListDirResponseFailsWhenPayloadIsIncomplete() {
    BinaryWriter writer;
    writer.writeUint32(2u);
    writer.writeString("Windows");
    writer.writeUint32(1u);

    ListDirResponse decoded{};
    expectTrue(!deserializeListDirResponse(writer.buffer(), decoded), "list dir response should fail on incomplete payload");
}

} // namespace

int main() {
    try {
        testDriveListResponseRoundTrip();
        testEmptyDriveListRoundTrip();
        testDeserializeFailsWhenPayloadIsIncomplete();
        testDeserializeFailsWhenPayloadHasTrailingBytes();
        testListDirRequestRoundTrip();
        testListDirResponseRoundTrip();
        testListDirRequestFailsWhenPayloadHasTrailingBytes();
        testListDirResponseFailsWhenPayloadIsIncomplete();

        std::cout << "All message serialization tests passed." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Message serialization test failed: " << ex.what() << std::endl;
        return 1;
    }
}
