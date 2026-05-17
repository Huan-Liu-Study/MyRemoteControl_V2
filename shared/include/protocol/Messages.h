#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "protocol/PacketCodec.h"

// Protocol strings are encoded as UTF-8 byte strings.

constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr uint32_t SESSION_CHANNEL_CONTROL = 1;
constexpr uint32_t SESSION_CHANNEL_SCREEN = 2;

struct DriveListResponse {
    std::vector<std::string> drives;
};

struct ListDirRequest {
    std::string path;
};

struct FileEntry {
    std::string name;
    uint32_t isDirectory;
};

struct ListDirResponse {
    std::vector<FileEntry> entries;
};

struct DownloadStartRequest {
    std::string path;
};

struct DownloadStartResponse {
    uint32_t ok;
    uint64_t fileSize;
    std::string fileName;
    std::string errorMessage;
};

struct MouseMoveRequest {
    int32_t x;
    int32_t y;
};

struct MouseClickRequest {
    uint32_t button;
    uint32_t action;
};

struct MousePositionResponse {
    int32_t x;
    int32_t y;
};

struct KeyboardEventRequest {
    uint32_t virtualKey;
    uint32_t action;
};

struct MouseWheelRequest {
    int32_t delta;
};

struct SessionHelloRequest {
    uint32_t protocolVersion;
    uint32_t channel;
};

struct SessionHelloResponse {
    uint32_t ok;
    uint32_t protocolVersion;
    std::string errorMessage;
};

struct ScreenshotStartRequest {
    uint32_t quality;
};

struct ScreenshotStartResponse {
    uint32_t ok;
    uint64_t imageSize;
    uint32_t screenWidth;
    uint32_t screenHeight;
    std::string fileName;
    std::string imageFormat;
    std::string errorMessage;
};

struct ScreenStreamStartRequest {
    uint32_t quality;
    uint32_t intervalMs;
};

struct ScreenStreamRect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t imageSize;
};

struct ScreenStreamFrameHeader {
    uint64_t imageSize;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t captureWidth;
    uint32_t captureHeight;
    uint32_t frameType;
    uint64_t frameId;
    uint64_t baseFrameId;
    uint32_t rectX;
    uint32_t rectY;
    uint32_t rectWidth;
    uint32_t rectHeight;
    uint32_t rectCount;
    uint64_t estimatedFullImageSize;
    uint32_t captureMs;
    uint32_t bltMs;
    uint32_t copyMs;
    uint32_t compareMs;
    uint32_t encodeMs;
    uint32_t sendMs;
    uint32_t ackWaitMs;
    uint32_t fallbackToKeyFrame;
    std::vector<ScreenStreamRect> rects;
    std::string imageFormat;
};

struct ScreenStreamFrameAck {
    uint64_t frameId;
    uint32_t ok;
};

ByteBuffer serializeDriveListResponse(const DriveListResponse& response);
bool deserializeDriveListResponse(const ByteBuffer& payload, DriveListResponse& outResponse);

ByteBuffer serializeListDirRequest(const ListDirRequest& request);
bool deserializeListDirRequest(const ByteBuffer& payload, ListDirRequest& outRequest);

ByteBuffer serializeListDirResponse(const ListDirResponse& response);
bool deserializeListDirResponse(const ByteBuffer& payload, ListDirResponse& outResponse);

ByteBuffer serializeDownloadStartRequest(const DownloadStartRequest& request);
bool deserializeDownloadStartRequest(const ByteBuffer& payload, DownloadStartRequest& outRequest);

ByteBuffer serializeDownloadStartResponse(const DownloadStartResponse& response);
bool deserializeDownloadStartResponse(const ByteBuffer& payload, DownloadStartResponse& outResponse);

ByteBuffer serializeMouseMoveRequest(const MouseMoveRequest& request);
bool deserializeMouseMoveRequest(const ByteBuffer& payload, MouseMoveRequest& outRequest);

ByteBuffer serializeMouseClickRequest(const MouseClickRequest& request);
bool deserializeMouseClickRequest(const ByteBuffer& payload, MouseClickRequest& outRequest);

ByteBuffer serializeMousePositionResponse(const MousePositionResponse& response);
bool deserializeMousePositionResponse(const ByteBuffer& payload, MousePositionResponse& outResponse);

ByteBuffer serializeKeyboardEventRequest(const KeyboardEventRequest& request);
bool deserializeKeyboardEventRequest(const ByteBuffer& payload, KeyboardEventRequest& outRequest);

ByteBuffer serializeMouseWheelRequest(const MouseWheelRequest& request);
bool deserializeMouseWheelRequest(const ByteBuffer& payload, MouseWheelRequest& outRequest);

ByteBuffer serializeSessionHelloRequest(const SessionHelloRequest& request);
bool deserializeSessionHelloRequest(const ByteBuffer& payload, SessionHelloRequest& outRequest);

ByteBuffer serializeSessionHelloResponse(const SessionHelloResponse& response);
bool deserializeSessionHelloResponse(const ByteBuffer& payload, SessionHelloResponse& outResponse);

ByteBuffer serializeScreenshotStartRequest(const ScreenshotStartRequest& request);
bool deserializeScreenshotStartRequest(const ByteBuffer& payload, ScreenshotStartRequest& outRequest);

ByteBuffer serializeScreenshotStartResponse(const ScreenshotStartResponse& response);
bool deserializeScreenshotStartResponse(const ByteBuffer& payload, ScreenshotStartResponse& outResponse);

ByteBuffer serializeScreenStreamStartRequest(const ScreenStreamStartRequest& request);
bool deserializeScreenStreamStartRequest(const ByteBuffer& payload, ScreenStreamStartRequest& outRequest);

ByteBuffer serializeScreenStreamFrameHeader(const ScreenStreamFrameHeader& header);
bool deserializeScreenStreamFrameHeader(const ByteBuffer& payload, ScreenStreamFrameHeader& outHeader);

ByteBuffer serializeScreenStreamFrameAck(const ScreenStreamFrameAck& ack);
bool deserializeScreenStreamFrameAck(const ByteBuffer& payload, ScreenStreamFrameAck& outAck);
