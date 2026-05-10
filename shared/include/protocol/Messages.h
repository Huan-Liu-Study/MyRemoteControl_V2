#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "protocol/PacketCodec.h"

// Protocol strings are encoded as UTF-8 byte strings.

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

struct ScreenshotStartRequest {
    uint32_t quality;
    uint32_t scalePercent;
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

ByteBuffer serializeScreenshotStartRequest(const ScreenshotStartRequest& request);
bool deserializeScreenshotStartRequest(const ByteBuffer& payload, ScreenshotStartRequest& outRequest);

ByteBuffer serializeScreenshotStartResponse(const ScreenshotStartResponse& response);
bool deserializeScreenshotStartResponse(const ByteBuffer& payload, ScreenshotStartResponse& outResponse);
