#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

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

signals:
    void connected();
    void disconnected(QString reason);
    void logMessage(QString message);
    void screenshotReceived(QByteArray imageData, QString imageFormat, int screenWidth, int screenHeight);
    void requestFinished();

private:
    bool isConnected() const;

    RemoteClientCore client_;
};
