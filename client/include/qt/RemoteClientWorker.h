#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/RemoteClientCore.h"

class RemoteClientWorker : public QObject {
    Q_OBJECT

public:
    explicit RemoteClientWorker(QObject* parent = nullptr);
    ~RemoteClientWorker() override;

public slots:
    void connectToServer(QString host, int port);
    void disconnectFromServer();
    void listDrives();
    void listDirectory(QString path);
    void downloadFile(QString remotePath, QString localDir);
    void clickMouseAt(int x, int y, int button);
    void mouseDownAt(int x, int y, int button);
    void mouseMoveAt(int x, int y);
    void mouseUpAt(int x, int y, int button);
    void mouseWheelAt(int x, int y, int delta);
    void probeMousePosition(int expectedX, int expectedY);
    void keyDown(int virtualKey);
    void keyUp(int virtualKey);
    void sendHeartbeat();

signals:
    void connected();
    void disconnected(QString reason);
    void logMessage(QString message);
    void driveListReceived(QStringList drives);
    void directoryListReceived(QStringList entries);
    void downloadFinished(QString localPath);
    void mousePositionProbed(int expectedX, int expectedY, int actualX, int actualY);
    void requestFinished();

private:
    bool isConnected() const;

    RemoteClientCore client_;
};
