#include "qt/RemoteClientWorker.h"

#include <QDir>
#include <QFile>

#include <vector>

namespace {

QString toQString(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

std::string toStdString(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
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

    emit logMessage("Connecting to " + host + ":" + QString::number(port));

    std::string errorMessage;
    if (!client_.connectToServer(toStdString(host), static_cast<uint16_t>(port), errorMessage)) {
        emit logMessage(toQString(errorMessage));
        emit requestFinished();
        return;
    }

    emit connected();
    emit logMessage("Control channel connected");
    emit requestFinished();
}

void RemoteClientWorker::disconnectFromServer()
{
    const bool wasConnected = isConnected();

    client_.disconnect();

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

    std::string errorMessage;
    std::vector<std::string> decodedDrives;
    if (!client_.listDrives(decodedDrives, errorMessage)) {
        emit logMessage(toQString(errorMessage));
        emit requestFinished();
        return;
    }

    QStringList drives;
    for (const std::string& drive : decodedDrives) {
        drives.push_back(toQString(drive));
    }

    emit driveListReceived(drives);
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

    emit logMessage("Request: list directory " + path);

    std::string errorMessage;
    std::vector<FileEntry> decodedEntries;
    if (!client_.listDirectory(toStdString(path), decodedEntries, errorMessage)) {
        emit logMessage(toQString(errorMessage));
        emit requestFinished();
        return;
    }

    QStringList entries;
    for (const FileEntry& entry : decodedEntries) {
        const QString prefix = entry.isDirectory ? "[DIR]  " : "[FILE] ";
        entries.push_back(prefix + toQString(entry.name));
    }

    emit directoryListReceived(entries);
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

    emit logMessage("Request: download " + remotePath);

    std::string errorMessage;
    DownloadStartResponse response{};
    if (!client_.requestDownload(toStdString(remotePath), response, errorMessage)) {
        emit logMessage(toQString(errorMessage));
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
    const bool ok = client_.receiveDownloadChunks(
        [&](const ByteBuffer& chunk, std::string& chunkError) {
            file.write(reinterpret_cast<const char*>(chunk.data()), static_cast<qint64>(chunk.size()));
            if (file.error() != QFile::NoError) {
                chunkError = "Failed to write local file: " + toStdString(file.errorString());
                return false;
            }

            totalReceived += chunk.size();
            emit logMessage("Progress: " + QString::number(totalReceived) + " / "
                            + QString::number(response.fileSize) + " bytes");
            return true;
        },
        errorMessage
    );

    if (!ok) {
        emit logMessage(toQString(errorMessage));
        emit requestFinished();
        return;
    }

    file.close();
    emit logMessage("Download complete");
    emit downloadFinished(localPath);
    emit requestFinished();
}

void RemoteClientWorker::clickMouseAt(int x, int y, int button)
{
    if (!isConnected()) {
        emit logMessage("Not connected");
        emit requestFinished();
        return;
    }

    emit logMessage("Remote click: " + QString::number(x) + ", " + QString::number(y));

    std::string errorMessage;
    if (!client_.moveMouse(x, y, errorMessage)) {
        emit logMessage(toQString(errorMessage));
        emit requestFinished();
        return;
    }

    if (!client_.clickMouse(static_cast<uint32_t>(button), errorMessage)) {
        emit logMessage(toQString(errorMessage));
        emit requestFinished();
        return;
    }
}

void RemoteClientWorker::mouseDownAt(int x, int y, int button)
{
    if (!isConnected()) {
        return;
    }

    std::string errorMessage;
    if (!client_.moveMouse(x, y, errorMessage)) {
        emit logMessage(toQString(errorMessage));
        return;
    }

    if (!client_.sendMouseButton(static_cast<uint32_t>(button), 1, errorMessage)) {
        emit logMessage(toQString(errorMessage));
    }
}

void RemoteClientWorker::mouseMoveAt(int x, int y)
{
    if (!isConnected()) {
        return;
    }

    std::string errorMessage;
    if (!client_.moveMouse(x, y, errorMessage)) {
        emit logMessage(toQString(errorMessage));
    }
}

void RemoteClientWorker::mouseUpAt(int x, int y, int button)
{
    if (!isConnected()) {
        return;
    }

    std::string errorMessage;
    if (!client_.moveMouse(x, y, errorMessage)) {
        emit logMessage(toQString(errorMessage));
        return;
    }

    if (!client_.sendMouseButton(static_cast<uint32_t>(button), 2, errorMessage)) {
        emit logMessage(toQString(errorMessage));
        return;
    }
}

void RemoteClientWorker::mouseWheelAt(int x, int y, int delta)
{
    if (!isConnected()) {
        return;
    }

    std::string errorMessage;
    if (!client_.moveMouse(x, y, errorMessage)) {
        emit logMessage(toQString(errorMessage));
        return;
    }

    if (!client_.sendMouseWheel(delta, errorMessage)) {
        emit logMessage(toQString(errorMessage));
    }
}

void RemoteClientWorker::keyDown(int virtualKey)
{
    if (!isConnected()) {
        return;
    }

    std::string errorMessage;
    if (!client_.sendKeyboardEvent(static_cast<uint32_t>(virtualKey), 1, errorMessage)) {
        emit logMessage(toQString(errorMessage));
    }
}

void RemoteClientWorker::keyUp(int virtualKey)
{
    if (!isConnected()) {
        return;
    }

    std::string errorMessage;
    if (!client_.sendKeyboardEvent(static_cast<uint32_t>(virtualKey), 2, errorMessage)) {
        emit logMessage(toQString(errorMessage));
    }
}

bool RemoteClientWorker::isConnected() const
{
    return client_.isConnected();
}
