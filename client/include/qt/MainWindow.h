#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QThread;
class RemoteClientWorker;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void buildUi();
    void startNetworkThread();
    void stopNetworkThread();
    void connectToServer();
    void disconnectFromServer();
    void listDrives();
    void listDirectory();
    void downloadFile();
    void setConnected(bool connected);
    void setBusy(bool busy);
    void appendLog(const QString& text);
    void showResults(const QStringList& titleAndItems);

    QLineEdit* hostInput_ = nullptr;
    QSpinBox* portInput_ = nullptr;
    QLineEdit* pathInput_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QListWidget* resultList_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QPushButton* listDrivesButton_ = nullptr;
    QPushButton* listDirButton_ = nullptr;
    QPushButton* downloadButton_ = nullptr;

    QThread* networkThread_ = nullptr;
    RemoteClientWorker* worker_ = nullptr;
    bool connected_ = false;
    bool busy_ = false;
};
