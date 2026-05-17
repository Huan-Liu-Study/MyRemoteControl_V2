#include "protocol/Messages.h"

#include <utility>

#include "protocol/BinaryReader.h"
#include "protocol/BinaryWriter.h"

ByteBuffer serializeDriveListResponse(const DriveListResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(static_cast<uint32_t>(response.drives.size()));

    for (const std::string& drive : response.drives) {
        writer.writeString(drive);
    }

    return writer.buffer();
}

bool deserializeDriveListResponse(const ByteBuffer& payload, DriveListResponse& outResponse)
{
    BinaryReader reader(payload);

    uint32_t driveCount = 0;
    if (!reader.readUint32(driveCount)) {
        return false;
    }

    std::vector<std::string> drives;
    drives.reserve(driveCount);

    for (uint32_t i = 0; i < driveCount; ++i) {
        std::string drive;
        if (!reader.readString(drive)) {
            return false;
        }

        drives.push_back(std::move(drive));
    }

    if (!reader.isFinished()) {
        return false;
    }

    outResponse.drives = std::move(drives);
    return true;
}

ByteBuffer serializeListDirRequest(const ListDirRequest& request)
{
    BinaryWriter writer;
    writer.writeString(request.path);
    return writer.buffer();
}

bool deserializeListDirRequest(const ByteBuffer& payload, ListDirRequest& outRequest)
{
    BinaryReader reader(payload);

    std::string path;
    if (!reader.readString(path)) {
        return false;
    }

    if (!reader.isFinished()) {
        return false;
    }

    outRequest.path = std::move(path);
    return true;
}

ByteBuffer serializeListDirResponse(const ListDirResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(static_cast<uint32_t>(response.entries.size()));

    for (const FileEntry& entry : response.entries) {
        writer.writeString(entry.name);
        writer.writeUint32(entry.isDirectory);
    }

    return writer.buffer();
}

bool deserializeListDirResponse(const ByteBuffer& payload, ListDirResponse& outResponse)
{
    BinaryReader reader(payload);

    uint32_t entryCount = 0;
    if (!reader.readUint32(entryCount)) {
        return false;
    }

    std::vector<FileEntry> entries;
    entries.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        FileEntry entry{};

        if (!reader.readString(entry.name) || !reader.readUint32(entry.isDirectory)) {
            return false;
        }

        entries.push_back(std::move(entry));
    }

    if (!reader.isFinished()) {
        return false;
    }

    outResponse.entries = std::move(entries);
    return true;
}

ByteBuffer serializeDownloadStartRequest(const DownloadStartRequest& request)
{
    BinaryWriter writer;
    writer.writeString(request.path);
    return writer.buffer();
}

bool deserializeDownloadStartRequest(const ByteBuffer& payload, DownloadStartRequest& outRequest)
{
    BinaryReader reader(payload);

    std::string path;
    if (!reader.readString(path)) {
        return false;
    }

    if (!reader.isFinished()) {
        return false;
    }

    outRequest.path = std::move(path);
    return true;
}

ByteBuffer serializeDownloadStartResponse(const DownloadStartResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(response.ok);
    writer.writeUint64(response.fileSize);
    writer.writeString(response.fileName);
    writer.writeString(response.errorMessage);
    return writer.buffer();
}

bool deserializeDownloadStartResponse(const ByteBuffer& payload, DownloadStartResponse& outResponse)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outResponse.ok)) {
        return false;
    }

    if (!reader.readUint64(outResponse.fileSize)) {
        return false;
    }

    if (!reader.readString(outResponse.fileName) || !reader.readString(outResponse.errorMessage)) {
        return false;
    }

    return reader.isFinished();
}

ByteBuffer serializeMouseMoveRequest(const MouseMoveRequest& request)
{
    BinaryWriter writer;
    writer.writeUint32(static_cast<uint32_t>(request.x));
    writer.writeUint32(static_cast<uint32_t>(request.y));
    return writer.buffer();
}

bool deserializeMouseMoveRequest(const ByteBuffer& payload, MouseMoveRequest& outRequest)
{
    BinaryReader reader(payload);

    uint32_t x = 0;
    uint32_t y = 0;
    if (!reader.readUint32(x) || !reader.readUint32(y)) {
        return false;
    }

    if (!reader.isFinished()) {
        return false;
    }

    outRequest.x = static_cast<int32_t>(x);
    outRequest.y = static_cast<int32_t>(y);
    return true;
}

ByteBuffer serializeMouseClickRequest(const MouseClickRequest& request)
{
    BinaryWriter writer;
    writer.writeUint32(request.button);
    writer.writeUint32(request.action);
    return writer.buffer();
}

bool deserializeMouseClickRequest(const ByteBuffer& payload, MouseClickRequest& outRequest)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outRequest.button) || !reader.readUint32(outRequest.action)) {
        return false;
    }

    return reader.isFinished();
}

ByteBuffer serializeMousePositionResponse(const MousePositionResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(static_cast<uint32_t>(response.x));
    writer.writeUint32(static_cast<uint32_t>(response.y));
    return writer.buffer();
}

bool deserializeMousePositionResponse(const ByteBuffer& payload, MousePositionResponse& outResponse)
{
    BinaryReader reader(payload);

    uint32_t x = 0;
    uint32_t y = 0;
    if (!reader.readUint32(x) || !reader.readUint32(y)) {
        return false;
    }

    if (!reader.isFinished()) {
        return false;
    }

    outResponse.x = static_cast<int32_t>(x);
    outResponse.y = static_cast<int32_t>(y);
    return true;
}

ByteBuffer serializeKeyboardEventRequest(const KeyboardEventRequest& request)
{
    BinaryWriter writer;
    writer.writeUint32(request.virtualKey);
    writer.writeUint32(request.action);
    return writer.buffer();
}

bool deserializeKeyboardEventRequest(const ByteBuffer& payload, KeyboardEventRequest& outRequest)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outRequest.virtualKey) || !reader.readUint32(outRequest.action)) {
        return false;
    }

    return reader.isFinished();
}

ByteBuffer serializeMouseWheelRequest(const MouseWheelRequest& request)
{
    BinaryWriter writer;
    writer.writeUint32(static_cast<uint32_t>(request.delta));
    return writer.buffer();
}

bool deserializeMouseWheelRequest(const ByteBuffer& payload, MouseWheelRequest& outRequest)
{
    BinaryReader reader(payload);

    uint32_t delta = 0;
    if (!reader.readUint32(delta)) {
        return false;
    }

    if (!reader.isFinished()) {
        return false;
    }

    outRequest.delta = static_cast<int32_t>(delta);
    return true;
}

ByteBuffer serializeSessionHelloRequest(const SessionHelloRequest& request)
{
    BinaryWriter writer;
    writer.writeUint32(request.protocolVersion);
    writer.writeUint32(request.channel);
    return writer.buffer();
}

bool deserializeSessionHelloRequest(const ByteBuffer& payload, SessionHelloRequest& outRequest)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outRequest.protocolVersion) || !reader.readUint32(outRequest.channel)) {
        return false;
    }

    return reader.isFinished();
}

ByteBuffer serializeSessionHelloResponse(const SessionHelloResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(response.ok);
    writer.writeUint32(response.protocolVersion);
    writer.writeString(response.errorMessage);
    return writer.buffer();
}

bool deserializeSessionHelloResponse(const ByteBuffer& payload, SessionHelloResponse& outResponse)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outResponse.ok) || !reader.readUint32(outResponse.protocolVersion)) {
        return false;
    }

    if (!reader.readString(outResponse.errorMessage)) {
        return false;
    }

    return reader.isFinished();
}

ByteBuffer serializeScreenshotStartRequest(const ScreenshotStartRequest& request)
{
    BinaryWriter writer;
    writer.writeUint32(request.quality);
    return writer.buffer();
}

bool deserializeScreenshotStartRequest(const ByteBuffer& payload, ScreenshotStartRequest& outRequest)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outRequest.quality)) {
        return false;
    }

    return reader.isFinished();
}

ByteBuffer serializeScreenshotStartResponse(const ScreenshotStartResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(response.ok);
    writer.writeUint64(response.imageSize);
    writer.writeUint32(response.screenWidth);
    writer.writeUint32(response.screenHeight);
    writer.writeString(response.fileName);
    writer.writeString(response.imageFormat);
    writer.writeString(response.errorMessage);
    return writer.buffer();
}

bool deserializeScreenshotStartResponse(const ByteBuffer& payload, ScreenshotStartResponse& outResponse)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outResponse.ok)) {
        return false;
    }

    if (!reader.readUint64(outResponse.imageSize)) {
        return false;
    }

    if (!reader.readUint32(outResponse.screenWidth) || !reader.readUint32(outResponse.screenHeight)) {
        return false;
    }

    if (!reader.readString(outResponse.fileName)
        || !reader.readString(outResponse.imageFormat)
        || !reader.readString(outResponse.errorMessage)) {
        return false;
    }

    return reader.isFinished();
}

ByteBuffer serializeScreenStreamStartRequest(const ScreenStreamStartRequest& request)
{
    BinaryWriter writer;
    writer.writeUint32(request.quality);
    writer.writeUint32(request.intervalMs);
    return writer.buffer();
}

bool deserializeScreenStreamStartRequest(const ByteBuffer& payload, ScreenStreamStartRequest& outRequest)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outRequest.quality)
        || !reader.readUint32(outRequest.intervalMs)) {
        return false;
    }

    return reader.isFinished();
}

ByteBuffer serializeScreenStreamFrameHeader(const ScreenStreamFrameHeader& header)
{
    BinaryWriter writer;
    writer.writeUint64(header.imageSize);
    writer.writeUint32(header.screenWidth);
    writer.writeUint32(header.screenHeight);
    writer.writeUint32(header.captureWidth);
    writer.writeUint32(header.captureHeight);
    writer.writeUint32(header.frameType);
    writer.writeUint64(header.frameId);
    writer.writeUint64(header.baseFrameId);
    writer.writeUint32(header.rectX);
    writer.writeUint32(header.rectY);
    writer.writeUint32(header.rectWidth);
    writer.writeUint32(header.rectHeight);
    writer.writeUint32(static_cast<uint32_t>(header.rects.size()));
    writer.writeUint64(header.estimatedFullImageSize);
    writer.writeUint32(header.captureMs);
    writer.writeUint32(header.bltMs);
    writer.writeUint32(header.copyMs);
    writer.writeUint32(header.compareMs);
    writer.writeUint32(header.encodeMs);
    writer.writeUint32(header.sendMs);
    writer.writeUint32(header.ackWaitMs);
    writer.writeUint32(header.fallbackToKeyFrame);
    for (const ScreenStreamRect& rect : header.rects) {
        writer.writeUint32(rect.x);
        writer.writeUint32(rect.y);
        writer.writeUint32(rect.width);
        writer.writeUint32(rect.height);
        writer.writeUint64(rect.imageSize);
    }
    writer.writeString(header.imageFormat);
    return writer.buffer();
}

bool deserializeScreenStreamFrameHeader(const ByteBuffer& payload, ScreenStreamFrameHeader& outHeader)
{
    BinaryReader reader(payload);

    if (!reader.readUint64(outHeader.imageSize)
        || !reader.readUint32(outHeader.screenWidth)
        || !reader.readUint32(outHeader.screenHeight)
        || !reader.readUint32(outHeader.captureWidth)
        || !reader.readUint32(outHeader.captureHeight)
        || !reader.readUint32(outHeader.frameType)
        || !reader.readUint64(outHeader.frameId)
        || !reader.readUint64(outHeader.baseFrameId)
        || !reader.readUint32(outHeader.rectX)
        || !reader.readUint32(outHeader.rectY)
        || !reader.readUint32(outHeader.rectWidth)
        || !reader.readUint32(outHeader.rectHeight)
        || !reader.readUint32(outHeader.rectCount)
        || !reader.readUint64(outHeader.estimatedFullImageSize)
        || !reader.readUint32(outHeader.captureMs)
        || !reader.readUint32(outHeader.bltMs)
        || !reader.readUint32(outHeader.copyMs)
        || !reader.readUint32(outHeader.compareMs)
        || !reader.readUint32(outHeader.encodeMs)
        || !reader.readUint32(outHeader.sendMs)
        || !reader.readUint32(outHeader.ackWaitMs)
        || !reader.readUint32(outHeader.fallbackToKeyFrame)) {
        return false;
    }

    std::vector<ScreenStreamRect> rects;
    rects.reserve(outHeader.rectCount);
    for (uint32_t i = 0; i < outHeader.rectCount; ++i) {
        ScreenStreamRect rect{};
        if (!reader.readUint32(rect.x)
            || !reader.readUint32(rect.y)
            || !reader.readUint32(rect.width)
            || !reader.readUint32(rect.height)
            || !reader.readUint64(rect.imageSize)) {
            return false;
        }
        rects.push_back(rect);
    }

    if (!reader.readString(outHeader.imageFormat)) {
        return false;
    }

    outHeader.rects = std::move(rects);
    return reader.isFinished();
}

ByteBuffer serializeScreenStreamFrameAck(const ScreenStreamFrameAck& ack)
{
    BinaryWriter writer;
    writer.writeUint64(ack.frameId);
    writer.writeUint32(ack.ok);
    return writer.buffer();
}

bool deserializeScreenStreamFrameAck(const ByteBuffer& payload, ScreenStreamFrameAck& outAck)
{
    BinaryReader reader(payload);

    if (!reader.readUint64(outAck.frameId) || !reader.readUint32(outAck.ok)) {
        return false;
    }

    return reader.isFinished();
}
