#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

using RemoteSocket = unsigned long long;

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

signals:
    void connected();
    void disconnected(QString reason);
    void logMessage(QString message);
    void driveListReceived(QStringList drives);
    void directoryListReceived(QStringList entries);
    void downloadFinished(QString localPath);
    void requestFinished();

private:
    bool receiveAndEmitResponse();
    bool isConnected() const;

    RemoteSocket socket_ = 0;
    bool wsaStarted_ = false;
};
