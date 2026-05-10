#include "qt/ScreenClientWorker.h"

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

ScreenClientWorker::ScreenClientWorker(QObject* parent)
    : QObject(parent)
{
}

ScreenClientWorker::~ScreenClientWorker()
{
    disconnectFromServer();
}

void ScreenClientWorker::connectToServer(QString host, int port)
{
    disconnectFromServer();

    emit logMessage("Connecting screen channel");

    std::string errorMessage;
    if (!client_.connectToServer(toStdString(host), static_cast<uint16_t>(port), errorMessage)) {
        emit logMessage(toQString(errorMessage));
        emit requestFinished();
        return;
    }

    emit connected();
    emit logMessage("Screen channel connected");
    emit requestFinished();
}

void ScreenClientWorker::disconnectFromServer()
{
    const bool wasConnected = isConnected();

    client_.disconnect();

    if (wasConnected) {
        emit disconnected("Screen channel disconnected");
    }
}

void ScreenClientWorker::takeScreenshot(int quality, int scalePercent)
{
    if (!isConnected()) {
        emit logMessage("Screen channel is not connected");
        emit requestFinished();
        return;
    }

    std::string errorMessage;
    ScreenshotStartResponse response{};
    if (!client_.requestScreenshot(
            response,
            errorMessage,
            static_cast<uint32_t>(quality),
            static_cast<uint32_t>(scalePercent)
        )) {
        emit logMessage(toQString(errorMessage));
        client_.disconnect();
        emit disconnected("Screen channel disconnected: " + toQString(errorMessage));
        emit requestFinished();
        return;
    }

    if (!response.ok) {
        emit logMessage("Screenshot rejected: " + toQString(response.errorMessage));
        emit requestFinished();
        return;
    }

    QByteArray imageData;
    imageData.reserve(static_cast<qsizetype>(response.imageSize));

    quint64 totalReceived = 0;
    const bool ok = client_.receiveScreenshotChunks(
        [&](const ByteBuffer& chunk, std::string& chunkError) {
            imageData.append(reinterpret_cast<const char*>(chunk.data()), static_cast<qsizetype>(chunk.size()));
            totalReceived += chunk.size();
            return true;
        },
        errorMessage
    );

    if (!ok) {
        emit logMessage(toQString(errorMessage));
        client_.disconnect();
        emit disconnected("Screen channel disconnected: " + toQString(errorMessage));
        emit requestFinished();
        return;
    }

    const QString imageFormat = response.imageFormat.empty() ? "BMP" : toQString(response.imageFormat);
    emit logMessage("Screenshot received: " + QString::number(totalReceived) + " bytes");
    emit screenshotReceived(
        imageData,
        imageFormat,
        static_cast<int>(response.screenWidth),
        static_cast<int>(response.screenHeight)
    );
    emit requestFinished();
}

bool ScreenClientWorker::isConnected() const
{
    return client_.isConnected();
}
