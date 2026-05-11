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
    streaming_ = false;

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

    streaming_ = false;
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

void ScreenClientWorker::startScreenStream(int quality, int scalePercent, int intervalMs)
{
    if (!isConnected()) {
        emit logMessage("Screen channel is not connected");
        emit requestFinished();
        return;
    }

    if (streaming_) {
        emit logMessage("Screen stream is already running");
        emit requestFinished();
        return;
    }

    std::string errorMessage;
    if (!client_.startScreenStream(
            static_cast<uint32_t>(quality),
            static_cast<uint32_t>(scalePercent),
            static_cast<uint32_t>(intervalMs),
            errorMessage
        )) {
        emit logMessage(toQString(errorMessage));
        client_.disconnect();
        emit disconnected("Screen channel disconnected: " + toQString(errorMessage));
        emit requestFinished();
        return;
    }

    streaming_ = true;
    emit logMessage("Screen stream started");

    while (streaming_ && isConnected()) {
        ScreenStreamFrameHeader header{};
        ByteBuffer image;
        if (!client_.receiveNextScreenStreamFrame(header, image, errorMessage)) {
            if (streaming_) {
                emit logMessage(toQString(errorMessage));
                client_.disconnect();
                emit disconnected("Screen channel disconnected: " + toQString(errorMessage));
            }
            streaming_ = false;
            emit requestFinished();
            return;
        }

        QByteArray imageData;
        imageData.append(reinterpret_cast<const char*>(image.data()), static_cast<qsizetype>(image.size()));

        const QString imageFormat = header.imageFormat.empty() ? "JPG" : toQString(header.imageFormat);
        QVector<int> rectXs;
        QVector<int> rectYs;
        QVector<int> rectWidths;
        QVector<int> rectHeights;
        QVector<qint64> rectImageSizes;
        rectXs.reserve(static_cast<qsizetype>(header.rects.size()));
        rectYs.reserve(static_cast<qsizetype>(header.rects.size()));
        rectWidths.reserve(static_cast<qsizetype>(header.rects.size()));
        rectHeights.reserve(static_cast<qsizetype>(header.rects.size()));
        rectImageSizes.reserve(static_cast<qsizetype>(header.rects.size()));
        for (const ScreenStreamRect& rect : header.rects) {
            rectXs.push_back(static_cast<int>(rect.x));
            rectYs.push_back(static_cast<int>(rect.y));
            rectWidths.push_back(static_cast<int>(rect.width));
            rectHeights.push_back(static_cast<int>(rect.height));
            rectImageSizes.push_back(static_cast<qint64>(rect.imageSize));
        }

        emit screenFrameReceived(
            imageData,
            imageFormat,
            static_cast<int>(header.screenWidth),
            static_cast<int>(header.screenHeight),
            static_cast<int>(header.captureWidth),
            static_cast<int>(header.captureHeight),
            static_cast<int>(header.frameType),
            static_cast<quint64>(header.frameId),
            static_cast<quint64>(header.baseFrameId),
            static_cast<int>(header.rectX),
            static_cast<int>(header.rectY),
            static_cast<int>(header.rectWidth),
            static_cast<int>(header.rectHeight),
            rectXs,
            rectYs,
            rectWidths,
            rectHeights,
            rectImageSizes,
            static_cast<qint64>(header.estimatedFullImageSize),
            static_cast<int>(header.captureMs),
            static_cast<int>(header.compareMs),
            static_cast<int>(header.encodeMs),
            static_cast<int>(header.sendMs),
            static_cast<int>(header.fallbackToKeyFrame)
        );
    }

    streaming_ = false;
    emit logMessage("Screen stream stopped");
    emit requestFinished();
}

void ScreenClientWorker::stopScreenStream()
{
    if (!streaming_) {
        return;
    }

    streaming_ = false;
    std::string errorMessage;
    client_.stopScreenStream(errorMessage);
    client_.disconnect();
}

void ScreenClientWorker::requestKeyFrame()
{
    if (!streaming_ || !isConnected()) {
        return;
    }

    std::string errorMessage;
    if (!client_.requestScreenStreamKeyFrame(errorMessage)) {
        emit logMessage(toQString(errorMessage));
        return;
    }

    emit logMessage("Key frame requested");
}

bool ScreenClientWorker::isConnected() const
{
    return client_.isConnected();
}
