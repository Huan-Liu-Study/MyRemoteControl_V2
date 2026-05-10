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

void testMouseMoveRequestRoundTrip() {
    MouseMoveRequest request{500, 300};

    ByteBuffer payload = serializeMouseMoveRequest(request);

    MouseMoveRequest decoded{};
    expectTrue(deserializeMouseMoveRequest(payload, decoded), "mouse move request deserialize should succeed");
    expectTrue(decoded.x == 500, "mouse move x mismatch");
    expectTrue(decoded.y == 300, "mouse move y mismatch");
}

void testMouseClickRequestRoundTrip() {
    MouseClickRequest request{1u, 3u};

    ByteBuffer payload = serializeMouseClickRequest(request);

    MouseClickRequest decoded{};
    expectTrue(deserializeMouseClickRequest(payload, decoded), "mouse click request deserialize should succeed");
    expectTrue(decoded.button == 1u, "mouse click button mismatch");
    expectTrue(decoded.action == 3u, "mouse click action mismatch");
}

void testMousePositionResponseRoundTrip() {
    MousePositionResponse response{900, 200};

    ByteBuffer payload = serializeMousePositionResponse(response);

    MousePositionResponse decoded{};
    expectTrue(deserializeMousePositionResponse(payload, decoded), "mouse position response deserialize should succeed");
    expectTrue(decoded.x == 900, "mouse position x mismatch");
    expectTrue(decoded.y == 200, "mouse position y mismatch");
}

void testScreenshotStartResponseRoundTrip() {
    ScreenshotStartResponse response{1u, 123456u, 1920u, 1080u, "screenshot.jpg", "JPG", ""};

    ByteBuffer payload = serializeScreenshotStartResponse(response);

    ScreenshotStartResponse decoded{};
    expectTrue(deserializeScreenshotStartResponse(payload, decoded), "screenshot response deserialize should succeed");
    expectTrue(decoded.ok == 1u, "screenshot ok mismatch");
    expectTrue(decoded.imageSize == 123456u, "screenshot size mismatch");
    expectTrue(decoded.screenWidth == 1920u, "screenshot screen width mismatch");
    expectTrue(decoded.screenHeight == 1080u, "screenshot screen height mismatch");
    expectTrue(decoded.fileName == "screenshot.jpg", "screenshot file name mismatch");
    expectTrue(decoded.imageFormat == "JPG", "screenshot image format mismatch");
    expectTrue(decoded.errorMessage.empty(), "screenshot error message should be empty");
}

void testKeyboardEventRequestRoundTrip() {
    KeyboardEventRequest request{65u, 1u};

    ByteBuffer payload = serializeKeyboardEventRequest(request);

    KeyboardEventRequest decoded{};
    expectTrue(deserializeKeyboardEventRequest(payload, decoded), "keyboard request deserialize should succeed");
    expectTrue(decoded.virtualKey == 65u, "keyboard virtual key mismatch");
    expectTrue(decoded.action == 1u, "keyboard action mismatch");
}

void testMouseWheelRequestRoundTrip() {
    MouseWheelRequest request{-120};

    ByteBuffer payload = serializeMouseWheelRequest(request);

    MouseWheelRequest decoded{};
    expectTrue(deserializeMouseWheelRequest(payload, decoded), "mouse wheel request deserialize should succeed");
    expectTrue(decoded.delta == -120, "mouse wheel delta mismatch");
}

void testScreenshotStartRequestRoundTrip() {
    ScreenshotStartRequest request{85u, 50u};

    ByteBuffer payload = serializeScreenshotStartRequest(request);

    ScreenshotStartRequest decoded{};
    expectTrue(deserializeScreenshotStartRequest(payload, decoded), "screenshot request deserialize should succeed");
    expectTrue(decoded.quality == 85u, "screenshot quality mismatch");
    expectTrue(decoded.scalePercent == 50u, "screenshot scale mismatch");
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
        testMouseMoveRequestRoundTrip();
        testMouseClickRequestRoundTrip();
        testMousePositionResponseRoundTrip();
        testScreenshotStartResponseRoundTrip();
        testKeyboardEventRequestRoundTrip();
        testMouseWheelRequestRoundTrip();
        testScreenshotStartRequestRoundTrip();

        std::cout << "All message serialization tests passed." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Message serialization test failed: " << ex.what() << std::endl;
        return 1;
    }
}
