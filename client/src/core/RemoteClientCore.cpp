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

bool RemoteClientCore::connectToServer(
    const std::string& host,
    uint16_t port,
    std::string& errorMessage,
    uint32_t channel
)
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

    SessionHelloRequest hello{};
    hello.protocolVersion = PROTOCOL_VERSION;
    hello.channel = channel;
    if (!sendPacket(clientSock, CMD::CMD_SESSION_HELLO, serializeSessionHelloRequest(hello))) {
        errorMessage = "Failed to send session hello";
        disconnect();
        return false;
    }

    ParsedPacket helloPacket{};
    if (!receiveExpected(CMD::CMD_SESSION_HELLO, helloPacket, errorMessage)) {
        disconnect();
        return false;
    }

    SessionHelloResponse helloResponse{};
    if (!deserializeSessionHelloResponse(helloPacket.payload, helloResponse)) {
        errorMessage = "Failed to parse session hello response";
        disconnect();
        return false;
    }

    if (!helloResponse.ok) {
        errorMessage = "Session hello rejected: " + helloResponse.errorMessage;
        disconnect();
        return false;
    }

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

bool RemoteClientCore::sendHeartbeat(std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    if (!sendPacket(toSocket(socket_), CMD::CMD_SESSION_HEARTBEAT)) {
        errorMessage = "Failed to send heartbeat";
        return false;
    }

    ParsedPacket response{};
    return receiveExpected(CMD::CMD_SESSION_HEARTBEAT, response, errorMessage);
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

bool RemoteClientCore::requestScreenshot(ScreenshotStartResponse& outResponse, std::string& errorMessage, uint32_t quality)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    ScreenshotStartRequest request{};
    request.quality = quality;

    if (!sendPacket(toSocket(socket_), CMD::CMD_SCREENSHOT_START, serializeScreenshotStartRequest(request))) {
        errorMessage = "Failed to send screenshot request";
        return false;
    }

    ParsedPacket response{};
    if (!receiveExpected(CMD::CMD_SCREENSHOT_START, response, errorMessage)) {
        return false;
    }

    if (!deserializeScreenshotStartResponse(response.payload, outResponse)) {
        errorMessage = "Failed to parse screenshot response";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool RemoteClientCore::receiveScreenshotChunks(const DownloadChunkHandler& onChunk, std::string& errorMessage)
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

        if (chunkPacket.header.command == CMD::CMD_SCREENSHOT_CHUNK) {
            if (onChunk && !onChunk(chunkPacket.payload, errorMessage)) {
                return false;
            }
            continue;
        }

        if (chunkPacket.header.command == CMD::CMD_SCREENSHOT_END) {
            errorMessage.clear();
            return true;
        }

        if (chunkPacket.header.command == CMD::CMD_ERROR) {
            errorMessage = "Server error during screenshot: " + PacketCodec::bytesToString(chunkPacket.payload);
            return false;
        }

        errorMessage = "Unexpected packet during screenshot: " + std::to_string(chunkPacket.header.command);
        return false;
    }
}

bool RemoteClientCore::startScreenStream(uint32_t quality, uint32_t intervalMs, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    ScreenStreamStartRequest request{};
    request.quality = quality;
    request.intervalMs = intervalMs;

    if (!sendPacket(toSocket(socket_), CMD::CMD_SCREEN_STREAM_START, serializeScreenStreamStartRequest(request))) {
        errorMessage = "Failed to send screen stream start request";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool RemoteClientCore::stopScreenStream(std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    if (!sendPacket(toSocket(socket_), CMD::CMD_SCREEN_STREAM_STOP)) {
        errorMessage = "Failed to send screen stream stop request";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool RemoteClientCore::requestScreenStreamKeyFrame(std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    if (!sendPacket(toSocket(socket_), CMD::CMD_SCREEN_STREAM_KEYFRAME_REQUEST)) {
        errorMessage = "Failed to send screen stream key frame request";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool RemoteClientCore::receiveNextScreenStreamFrame(ScreenStreamFrameHeader& outHeader, ByteBuffer& outImage, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    ParsedPacket startPacket{};
    if (!recvPacket(toSocket(socket_), startPacket)) {
        errorMessage = wsaErrorText("recvPacket");
        return false;
    }

    if (startPacket.header.command == CMD::CMD_ERROR) {
        errorMessage = "Server error during screen stream: " + PacketCodec::bytesToString(startPacket.payload);
        return false;
    }

    if (startPacket.header.command != CMD::CMD_SCREEN_STREAM_FRAME_START) {
        errorMessage = "Unexpected screen stream packet: " + std::to_string(startPacket.header.command);
        return false;
    }

    ScreenStreamFrameHeader header{};
    if (!deserializeScreenStreamFrameHeader(startPacket.payload, header)) {
        errorMessage = "Failed to parse screen stream frame header";
        return false;
    }

    ByteBuffer image;
    image.reserve(static_cast<size_t>(header.imageSize));

    while (true) {
        ParsedPacket chunkPacket{};
        if (!recvPacket(toSocket(socket_), chunkPacket)) {
            errorMessage = wsaErrorText("recvPacket");
            return false;
        }

        if (chunkPacket.header.command == CMD::CMD_SCREEN_STREAM_FRAME_CHUNK) {
            image.insert(image.end(), chunkPacket.payload.begin(), chunkPacket.payload.end());
            continue;
        }

        if (chunkPacket.header.command == CMD::CMD_SCREEN_STREAM_FRAME_END) {
            outHeader = std::move(header);
            outImage = std::move(image);
            errorMessage.clear();
            return true;
        }

        if (chunkPacket.header.command == CMD::CMD_ERROR) {
            errorMessage = "Server error during screen stream frame: " + PacketCodec::bytesToString(chunkPacket.payload);
            return false;
        }

        errorMessage = "Unexpected screen stream frame packet: " + std::to_string(chunkPacket.header.command);
        return false;
    }
}

bool RemoteClientCore::acknowledgeScreenStreamFrame(uint64_t frameId, bool ok, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    ScreenStreamFrameAck ack{};
    ack.frameId = frameId;
    ack.ok = ok ? 1u : 0u;

    if (!sendPacket(toSocket(socket_), CMD::CMD_SCREEN_STREAM_FRAME_ACK, serializeScreenStreamFrameAck(ack))) {
        errorMessage = "Failed to send screen stream frame ack";
        return false;
    }

    errorMessage.clear();
    return true;
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
    return sendMouseButton(button, 3, errorMessage);
}

bool RemoteClientCore::sendMouseButton(uint32_t button, uint32_t action, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    MouseClickRequest request{};
    request.button = button;
    request.action = action;

    if (!sendPacket(toSocket(socket_), CMD::CMD_MOUSE_CLICK, serializeMouseClickRequest(request))) {
        errorMessage = "Failed to send mouse button request";
        return false;
    }

    ParsedPacket response{};
    return receiveExpected(CMD::CMD_MOUSE_CLICK, response, errorMessage);
}

bool RemoteClientCore::dragMouse(
    uint32_t button,
    int32_t fromX,
    int32_t fromY,
    int32_t toX,
    int32_t toY,
    std::string& errorMessage
)
{
    if (!moveMouse(fromX, fromY, errorMessage)) {
        return false;
    }

    if (!sendMouseButton(button, 1, errorMessage)) {
        return false;
    }

    if (!moveMouse(toX, toY, errorMessage)) {
        return false;
    }

    return sendMouseButton(button, 2, errorMessage);
}

bool RemoteClientCore::smoothDragMouse(
    uint32_t button,
    int32_t fromX,
    int32_t fromY,
    int32_t toX,
    int32_t toY,
    uint32_t steps,
    std::string& errorMessage
)
{
    if (steps == 0) {
        return dragMouse(button, fromX, fromY, toX, toY, errorMessage);
    }

    if (!moveMouse(fromX, fromY, errorMessage)) {
        return false;
    }

    if (!sendMouseButton(button, 1, errorMessage)) {
        return false;
    }

    for (uint32_t i = 1; i <= steps; ++i) {
        const int32_t x = fromX + static_cast<int32_t>((static_cast<int64_t>(toX - fromX) * i) / steps);
        const int32_t y = fromY + static_cast<int32_t>((static_cast<int64_t>(toY - fromY) * i) / steps);

        if (!moveMouse(x, y, errorMessage)) {
            return false;
        }
    }

    return sendMouseButton(button, 2, errorMessage);
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

bool RemoteClientCore::sendKeyboardEvent(uint32_t virtualKey, uint32_t action, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    KeyboardEventRequest request{};
    request.virtualKey = virtualKey;
    request.action = action;

    if (!sendPacket(toSocket(socket_), CMD::CMD_KEYBOARD_EVENT, serializeKeyboardEventRequest(request))) {
        errorMessage = "Failed to send keyboard event";
        return false;
    }

    ParsedPacket response{};
    return receiveExpected(CMD::CMD_KEYBOARD_EVENT, response, errorMessage);
}

bool RemoteClientCore::sendMouseWheel(int32_t delta, std::string& errorMessage)
{
    if (!isConnected()) {
        errorMessage = "Not connected";
        return false;
    }

    MouseWheelRequest request{};
    request.delta = delta;

    if (!sendPacket(toSocket(socket_), CMD::CMD_MOUSE_WHEEL, serializeMouseWheelRequest(request))) {
        errorMessage = "Failed to send mouse wheel event";
        return false;
    }

    ParsedPacket response{};
    return receiveExpected(CMD::CMD_MOUSE_WHEEL, response, errorMessage);
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
