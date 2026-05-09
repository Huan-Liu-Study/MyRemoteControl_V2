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
    bool moveMouse(int32_t x, int32_t y, std::string& errorMessage);
    bool clickMouse(uint32_t button, std::string& errorMessage);
    bool getMousePosition(MousePositionResponse& outPosition, std::string& errorMessage);

private:
    bool receiveExpected(CMD::Type expectedCommand, ParsedPacket& outPacket, std::string& errorMessage);

    uintptr_t socket_ = 0;
    bool wsaStarted_ = false;
};
