#pragma once

#include <atomic>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include "core/RemoteClientCore.h"

class ScreenClientWorker : public QObject {
    Q_OBJECT

public:
    explicit ScreenClientWorker(QObject* parent = nullptr);
    ~ScreenClientWorker() override;

public slots:
    void connectToServer(QString host, int port);
    void disconnectFromServer();
    void takeScreenshot(int quality, int scalePercent);
    void startScreenStream(int quality, int scalePercent, int intervalMs);
    void stopScreenStream();
    void requestKeyFrame();

signals:
    void connected();
    void disconnected(QString reason);
    void logMessage(QString message);
    void screenshotReceived(QByteArray imageData, QString imageFormat, int screenWidth, int screenHeight);
    void screenFrameReceived(
        QByteArray imageData,
        QString imageFormat,
        int screenWidth,
        int screenHeight,
        int captureWidth,
        int captureHeight,
        int frameType,
        quint64 frameId,
        quint64 baseFrameId,
        int rectX,
        int rectY,
        int rectWidth,
        int rectHeight,
        QVector<int> rectXs,
        QVector<int> rectYs,
        QVector<int> rectWidths,
        QVector<int> rectHeights,
        QVector<qint64> rectImageSizes,
        qint64 estimatedFullImageSize,
        int captureMs,
        int compareMs,
        int encodeMs,
        int previousSendMs,
        int fallbackToKeyFrame
    );
    void requestFinished();

private:
    bool isConnected() const;

    RemoteClientCore client_;
    std::atomic_bool streaming_{false};
};
