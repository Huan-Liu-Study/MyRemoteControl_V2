#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "qt/RemoteClientWorker.h"

#include <QDir>
#include <QFile>

#include "net/SocketHelpers.h"
#include "protocol/Messages.h"
#include "protocol/PacketCodec.h"

namespace {

constexpr SOCKET invalidSocket = INVALID_SOCKET;

QString socketErrorText(const QString& action)
{
    const int error = WSAGetLastError();
    if (error == 0) {
        return action + " failed: server closed connection or sent no response";
    }

    return action + " failed, WSA error: " + QString::number(error);
}

QString toQString(const std::string& text)
{
    return QString::fromLocal8Bit(text.data(), static_cast<int>(text.size()));
}

} // namespace

RemoteClientWorker::RemoteClientWorker(QObject* parent)
    : QObject(parent)
{
}

RemoteClientWorker::~RemoteClientWorker()
{
    disconnectFromServer();
}

void RemoteClientWorker::connectToServer(QString host, int port)
{
    disconnectFromServer();

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        emit logMessage("WSAStartup failed");
        emit requestFinished();
        return;
    }
    wsaStarted_ = true;

    SOCKET clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSock == invalidSocket) {
        emit logMessage(socketErrorText("socket"));
        disconnectFromServer();
        emit requestFinished();
        return;
    }

    DWORD timeoutMs = 5000;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(port));

    const QByteArray hostBytes = host.toLocal8Bit();
    if (inet_pton(AF_INET, hostBytes.constData(), &serverAddr.sin_addr) != 1) {
        emit logMessage("Invalid host: " + host);
        closesocket(clientSock);
        disconnectFromServer();
        emit requestFinished();
        return;
    }

    emit logMessage("Connecting to " + host + ":" + QString::number(port));

    if (::connect(clientSock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        emit logMessage(socketErrorText("connect"));
        closesocket(clientSock);
        disconnectFromServer();
        emit requestFinished();
        return;
    }

    socket_ = static_cast<RemoteSocket>(clientSock);
    emit connected();
    emit logMessage("Connected");
    emit requestFinished();
}

void RemoteClientWorker::disconnectFromServer()
{
    const bool wasConnected = isConnected();

    if (socket_ != 0) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = 0;
    }

    if (wsaStarted_) {
        WSACleanup();
        wsaStarted_ = false;
    }

    if (wasConnected) {
        emit disconnected("Disconnected");
    }
}

void RemoteClientWorker::listDrives()
{
    if (!isConnected()) {
        emit logMessage("Not connected");
        emit requestFinished();
        return;
    }

    emit logMessage("Request: list drives");
    if (!sendPacket(static_cast<SOCKET>(socket_), CMD::CMD_LIST_DRIVES)) {
        emit logMessage("Failed to send list drives request");
        emit requestFinished();
        return;
    }

    receiveAndEmitResponse();
    emit requestFinished();
}

void RemoteClientWorker::listDirectory(QString path)
{
    if (!isConnected()) {
        emit logMessage("Not connected");
        emit requestFinished();
        return;
    }

    path = path.trimmed();
    if (path.isEmpty()) {
        emit logMessage("Path is empty");
        emit requestFinished();
        return;
    }

    ListDirRequest request{};
    request.path = path.toLocal8Bit().constData();

    emit logMessage("Request: list directory " + path);
    if (!sendPacket(static_cast<SOCKET>(socket_), CMD::CMD_LIST_DIR, serializeListDirRequest(request))) {
        emit logMessage("Failed to send list directory request");
        emit requestFinished();
        return;
    }

    receiveAndEmitResponse();
    emit requestFinished();
}

void RemoteClientWorker::downloadFile(QString remotePath, QString localDir)
{
    if (!isConnected()) {
        emit logMessage("Not connected");
        emit requestFinished();
        return;
    }

    remotePath = remotePath.trimmed();
    if (remotePath.isEmpty()) {
        emit logMessage("Remote file path is empty");
        emit requestFinished();
        return;
    }

    if (localDir.trimmed().isEmpty()) {
        emit logMessage("Local save directory is empty");
        emit requestFinished();
        return;
    }

    DownloadStartRequest request{};
    request.path = remotePath.toLocal8Bit().constData();

    emit logMessage("Request: download " + remotePath);
    if (!sendPacket(static_cast<SOCKET>(socket_), CMD::CMD_DOWNLOAD_START, serializeDownloadStartRequest(request))) {
        emit logMessage("Failed to send download request");
        emit requestFinished();
        return;
    }

    ParsedPacket startPacket{};
    if (!recvPacket(static_cast<SOCKET>(socket_), startPacket)) {
        emit logMessage(socketErrorText("recvPacket"));
        emit requestFinished();
        return;
    }

    if (startPacket.header.command == CMD::CMD_ERROR) {
        emit logMessage("Server error: " + toQString(PacketCodec::bytesToString(startPacket.payload)));
        emit requestFinished();
        return;
    }

    if (startPacket.header.command != CMD::CMD_DOWNLOAD_START) {
        emit logMessage("Unexpected download response command: " + QString::number(startPacket.header.command));
        emit requestFinished();
        return;
    }

    DownloadStartResponse response{};
    if (!deserializeDownloadStartResponse(startPacket.payload, response)) {
        emit logMessage("Failed to parse download start response");
        emit requestFinished();
        return;
    }

    if (!response.ok) {
        emit logMessage("Download rejected: " + toQString(response.errorMessage));
        emit requestFinished();
        return;
    }

    const QString fileName = response.fileName.empty() ? "download.bin" : toQString(response.fileName);
    const QString localPath = QDir(localDir).filePath(fileName);

    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit logMessage("Failed to create local file: " + localPath);
        emit requestFinished();
        return;
    }

    emit logMessage("Downloading to " + localPath);

    quint64 totalReceived = 0;
    while (true) {
        ParsedPacket chunkPacket{};
        if (!recvPacket(static_cast<SOCKET>(socket_), chunkPacket)) {
            emit logMessage(socketErrorText("recvPacket"));
            emit requestFinished();
            return;
        }

        if (chunkPacket.header.command == CMD::CMD_DOWNLOAD_CHUNK) {
            file.write(reinterpret_cast<const char*>(chunkPacket.payload.data()),
                       static_cast<qint64>(chunkPacket.payload.size()));
            totalReceived += chunkPacket.payload.size();
            emit logMessage("Progress: " + QString::number(totalReceived) + " / "
                            + QString::number(response.fileSize) + " bytes");
            continue;
        }

        if (chunkPacket.header.command == CMD::CMD_DOWNLOAD_END) {
            file.close();
            emit logMessage("Download complete");
            emit downloadFinished(localPath);
            emit requestFinished();
            return;
        }

        if (chunkPacket.header.command == CMD::CMD_ERROR) {
            emit logMessage("Server error during download: " + toQString(PacketCodec::bytesToString(chunkPacket.payload)));
            emit requestFinished();
            return;
        }

        emit logMessage("Unexpected packet during download: " + QString::number(chunkPacket.header.command));
        emit requestFinished();
        return;
    }
}

bool RemoteClientWorker::receiveAndEmitResponse()
{
    ParsedPacket response{};
    if (!recvPacket(static_cast<SOCKET>(socket_), response)) {
        emit logMessage(socketErrorText("recvPacket"));
        return false;
    }

    if (response.header.command == CMD::CMD_LIST_DRIVES) {
        DriveListResponse decoded{};
        if (!deserializeDriveListResponse(response.payload, decoded)) {
            emit logMessage("Failed to parse drive list response");
            return false;
        }

        QStringList drives;
        for (const std::string& drive : decoded.drives) {
            drives.push_back(toQString(drive));
        }

        emit driveListReceived(drives);
        return true;
    }

    if (response.header.command == CMD::CMD_LIST_DIR) {
        ListDirResponse decoded{};
        if (!deserializeListDirResponse(response.payload, decoded)) {
            emit logMessage("Failed to parse directory response");
            return false;
        }

        QStringList entries;
        for (const FileEntry& entry : decoded.entries) {
            const QString prefix = entry.isDirectory ? "[DIR]  " : "[FILE] ";
            entries.push_back(prefix + toQString(entry.name));
        }

        emit directoryListReceived(entries);
        return true;
    }

    if (response.header.command == CMD::CMD_ERROR) {
        emit logMessage("Server error: " + toQString(PacketCodec::bytesToString(response.payload)));
        return false;
    }

    emit logMessage("Unknown response command: " + QString::number(response.header.command));
    return false;
}

bool RemoteClientWorker::isConnected() const
{
    return socket_ != 0;
}
