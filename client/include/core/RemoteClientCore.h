#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "protocol/Messages.h"
#include "protocol/PacketCodec.h"

class RemoteClientCore {
public:
    using DownloadChunkHandler = std::function<bool(const ByteBuffer& chunk, std::string& errorMessage)>;

    RemoteClientCore() = default;
    ~RemoteClientCore();

    RemoteClientCore(const RemoteClientCore&) = delete;
    RemoteClientCore& operator=(const RemoteClientCore&) = delete;

    bool connectToServer(const std::string& host, uint16_t port, std::string& errorMessage);
    void disconnect();
    bool isConnected() const;

    bool listDrives(std::vector<std::string>& outDrives, std::string& errorMessage);
    bool listDirectory(const std::string& path, std::vector<FileEntry>& outEntries, std::string& errorMessage);

    bool requestDownload(const std::string& remotePath, DownloadStartResponse& outResponse, std::string& errorMessage);
    bool receiveDownloadChunks(const DownloadChunkHandler& onChunk, std::string& errorMessage);
    bool requestScreenshot(ScreenshotStartResponse& outResponse, std::string& errorMessage, uint32_t quality = 70, uint32_t scalePercent = 100);
    bool receiveScreenshotChunks(const DownloadChunkHandler& onChunk, std::string& errorMessage);
    bool startScreenStream(uint32_t quality, uint32_t scalePercent, uint32_t intervalMs, std::string& errorMessage);
    bool stopScreenStream(std::string& errorMessage);
    bool requestScreenStreamKeyFrame(std::string& errorMessage);
    bool receiveNextScreenStreamFrame(ScreenStreamFrameHeader& outHeader, ByteBuffer& outImage, std::string& errorMessage);
    bool moveMouse(int32_t x, int32_t y, std::string& errorMessage);
    bool sendMouseButton(uint32_t button, uint32_t action, std::string& errorMessage);
    bool clickMouse(uint32_t button, std::string& errorMessage);
    bool dragMouse(uint32_t button, int32_t fromX, int32_t fromY, int32_t toX, int32_t toY, std::string& errorMessage);
    bool smoothDragMouse(uint32_t button, int32_t fromX, int32_t fromY, int32_t toX, int32_t toY, uint32_t steps, std::string& errorMessage);
    bool getMousePosition(MousePositionResponse& outPosition, std::string& errorMessage);
    bool sendKeyboardEvent(uint32_t virtualKey, uint32_t action, std::string& errorMessage);
    bool sendMouseWheel(int32_t delta, std::string& errorMessage);

private:
    bool receiveExpected(CMD::Type expectedCommand, ParsedPacket& outPacket, std::string& errorMessage);

    uintptr_t socket_ = 0;
    bool wsaStarted_ = false;
};
