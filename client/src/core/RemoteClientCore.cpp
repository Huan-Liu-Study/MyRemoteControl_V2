#include "core/RemoteClientCore.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <utility>

#include "net/SocketHelpers.h"
#include "protocol/Command.h"

namespace {

constexpr SOCKET invalidSocket = INVALID_SOCKET;

std::string wsaErrorText(const std::string& action)
{
    const int error = WSAGetLastError();
    if (error == 0) {
        return action + " failed: server closed connection or sent no response";
    }

    return action + " failed, WSA error: " + std::to_string(error);
}

SOCKET toSocket(uintptr_t socket)
{
    return static_cast<SOCKET>(socket);
}

} // namespace

RemoteClientCore::~RemoteClientCore()
{
    disconnect();
}

bool RemoteClientCore::connectToServer(const std::string& host, uint16_t port, std::string& errorMessage)
{
    disconnect();

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        errorMessage = "WSAStartup failed";
        return false;
    }
    wsaStarted_ = true;

    SOCKET clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSock == invalidSocket) {
        errorMessage = wsaErrorText("socket");
        disconnect();
        return false;
    }

    DWORD timeoutMs = 5000;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1) {
        errorMessage = "Invalid host: " + host;
        closesocket(clientSock);
        disconnect();
        return false;
    }

    if (::connect(clientSock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        errorMessage = wsaErrorText("connect");
        closesocket(clientSock);
        disconnect();
        return false;
    }

    socket_ = static_cast<uintptr_t>(clientSock);
    errorMessage.clear();
    return true;
}

void RemoteClientCore::disconnect()
{
    if (socket_ != 0) {
        closesocket(toSocket(socket_));
        socket_ = 0;
    }

    if (wsaStarted_) {
        WSACleanup();
        wsaStarted_ = false;
    }
}

bool RemoteClientCore::isConnected() const
{
    return socket_ != 0;
}

bool RemoteClientCore::listDrives(std::vector<std::string>& outDrives, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    if (!sendPacket(toSocket(socket_), CMD::CMD_LIST_DRIVES)) {
        errorMessage = "Failed to send list drives request";
        return false;
    }

    ParsedPacket response{};
    if (!receiveExpected(CMD::CMD_LIST_DRIVES, response, errorMessage)) {
        return false;
    }

    DriveListResponse decoded{};
    if (!deserializeDriveListResponse(response.payload, decoded)) {
        errorMessage = "Failed to parse drive list response";
        return false;
    }

    outDrives = std::move(decoded.drives);
    errorMessage.clear();
    return true;
}

bool RemoteClientCore::listDirectory(const std::string& path, std::vector<FileEntry>& outEntries, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    ListDirRequest request{};
    request.path = path;

    if (!sendPacket(toSocket(socket_), CMD::CMD_LIST_DIR, serializeListDirRequest(request))) {
        errorMessage = "Failed to send list directory request";
        return false;
    }

    ParsedPacket response{};
    if (!receiveExpected(CMD::CMD_LIST_DIR, response, errorMessage)) {
        return false;
    }

    ListDirResponse decoded{};
    if (!deserializeListDirResponse(response.payload, decoded)) {
        errorMessage = "Failed to parse directory response";
        return false;
    }

    outEntries = std::move(decoded.entries);
    errorMessage.clear();
    return true;
}

bool RemoteClientCore::requestDownload(const std::string& remotePath, DownloadStartResponse& outResponse, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    DownloadStartRequest request{};
    request.path = remotePath;

    if (!sendPacket(toSocket(socket_), CMD::CMD_DOWNLOAD_START, serializeDownloadStartRequest(request))) {
        errorMessage = "Failed to send download request";
        return false;
    }

    ParsedPacket response{};
    if (!receiveExpected(CMD::CMD_DOWNLOAD_START, response, errorMessage)) {
        return false;
    }

    if (!deserializeDownloadStartResponse(response.payload, outResponse)) {
        errorMessage = "Failed to parse download start response";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool RemoteClientCore::receiveDownloadChunks(const DownloadChunkHandler& onChunk, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    while (true) {
        ParsedPacket chunkPacket{};
        if (!recvPacket(toSocket(socket_), chunkPacket)) {
            errorMessage = wsaErrorText("recvPacket");
            return false;
        }

        if (chunkPacket.header.command == CMD::CMD_DOWNLOAD_CHUNK) {
            if (onChunk && !onChunk(chunkPacket.payload, errorMessage)) {
                return false;
            }
            continue;
        }

        if (chunkPacket.header.command == CMD::CMD_DOWNLOAD_END) {
            errorMessage.clear();
            return true;
        }

        if (chunkPacket.header.command == CMD::CMD_ERROR) {
            errorMessage = "Server error during download: " + PacketCodec::bytesToString(chunkPacket.payload);
            return false;
        }

        errorMessage = "Unexpected packet during download: " + std::to_string(chunkPacket.header.command);
        return false;
    }
}

bool RemoteClientCore::moveMouse(int32_t x, int32_t y, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    MouseMoveRequest request{};
    request.x = x;
    request.y = y;

    if (!sendPacket(toSocket(socket_), CMD::CMD_MOUSE_MOVE, serializeMouseMoveRequest(request))) {
        errorMessage = "Failed to send mouse move request";
        return false;
    }

    ParsedPacket response{};
    return receiveExpected(CMD::CMD_MOUSE_MOVE, response, errorMessage);
}

bool RemoteClientCore::clickMouse(uint32_t button, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    MouseClickRequest request{};
    request.button = button;
    request.action = 3;

    if (!sendPacket(toSocket(socket_), CMD::CMD_MOUSE_CLICK, serializeMouseClickRequest(request))) {
        errorMessage = "Failed to send mouse click request";
        return false;
    }

    ParsedPacket response{};
    return receiveExpected(CMD::CMD_MOUSE_CLICK, response, errorMessage);
}

bool RemoteClientCore::getMousePosition(MousePositionResponse& outPosition, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    if (!sendPacket(toSocket(socket_), CMD::CMD_MOUSE_POSITION)) {
        errorMessage = "Failed to send mouse position request";
        return false;
    }

    ParsedPacket response{};
    if (!receiveExpected(CMD::CMD_MOUSE_POSITION, response, errorMessage)) {
        return false;
    }

    if (!deserializeMousePositionResponse(response.payload, outPosition)) {
        errorMessage = "Failed to parse mouse position response";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool RemoteClientCore::receiveExpected(CMD::Type expectedCommand, ParsedPacket& outPacket, std::string& errorMessage)
{
    if (!recvPacket(toSocket(socket_), outPacket)) {
        errorMessage = wsaErrorText("recvPacket");
        return false;
    }

    if (outPacket.header.command == CMD::CMD_ERROR) {
        errorMessage = "Server error: " + PacketCodec::bytesToString(outPacket.payload);
        return false;
    }

    if (outPacket.header.command != expectedCommand) {
        errorMessage = "Unexpected response command: " + std::to_string(outPacket.header.command);
        return false;
    }

    errorMessage.clear();
    return true;
}
