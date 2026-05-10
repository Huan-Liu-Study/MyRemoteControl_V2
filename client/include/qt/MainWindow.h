#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPoint>
#include <QPixmap>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QSpinBox;
class QThread;
class QTimer;
class RemoteClientWorker;
class ScreenClientWorker;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void startNetworkThread();
    void stopNetworkThread();
    void connectToServer();
    void disconnectFromServer();
    void listDrives();
    void listDirectory();
    void downloadFile();
    void requestScreenFrame();
    void toggleAutoRefresh();
    void scheduleInputRefresh();
    void setConnected(bool connected);
    void setBusy(bool busy);
    void appendLog(const QString& text);
    void showResults(const QStringList& titleAndItems);
    void showScreenshot(const QByteArray& imageData, const QString& imageFormat, int screenWidth, int screenHeight);
    void updateScreenshotView();
    void updateScreenStreamStatus();
    void updateFrameStats(qint64 bytes, qint64 elapsedMs);
    bool mapScreenshotPoint(const QPoint& labelPoint, int& outRemoteX, int& outRemoteY) const;
    void clickScreenshotAt(const QPoint& labelPoint, int button);
    void sendRemoteMouseDown(const QPoint& labelPoint, int button);
    void sendRemoteMouseMove(const QPoint& labelPoint);
    void sendRemoteMouseUp(const QPoint& labelPoint, int button);
    void sendRemoteMouseWheel(const QPoint& labelPoint, int delta);

    QLineEdit* hostInput_ = nullptr;
    QSpinBox* portInput_ = nullptr;
    QSpinBox* qualityInput_ = nullptr;
    QSpinBox* scaleInput_ = nullptr;
    QSpinBox* refreshIntervalInput_ = nullptr;
    QLineEdit* pathInput_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* frameStatsLabel_ = nullptr;
    QLabel* streamStateLabel_ = nullptr;
    QListWidget* resultList_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QLabel* screenshotLabel_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QPushButton* listDrivesButton_ = nullptr;
    QPushButton* listDirButton_ = nullptr;
    QPushButton* downloadButton_ = nullptr;
    QPushButton* screenshotButton_ = nullptr;
    QPushButton* autoRefreshButton_ = nullptr;

    QThread* networkThread_ = nullptr;
    QThread* screenThread_ = nullptr;
    QTimer* autoRefreshTimer_ = nullptr;
    QTimer* inputRefreshTimer_ = nullptr;
    RemoteClientWorker* worker_ = nullptr;
    ScreenClientWorker* screenWorker_ = nullptr;
    QPixmap currentScreenshot_;
    QElapsedTimer screenshotRequestTimer_;
    QElapsedTimer frameRateTimer_;
    qint64 lastFrameBytes_ = 0;
    qint64 lastFrameElapsedMs_ = 0;
    int screenFailureCount_ = 0;
    int remoteScreenWidth_ = 0;
    int remoteScreenHeight_ = 0;
    int framesSinceRateUpdate_ = 0;
    int screenshotPressButton_ = 0;
    int lastRemoteMoveX_ = -1;
    int lastRemoteMoveY_ = -1;
    QElapsedTimer lastMoveSentAt_;
    bool hasScreenshotPress_ = false;
    bool controlConnected_ = false;
    bool screenConnected_ = false;
    bool pendingScreenFrame_ = false;
    bool connected_ = false;
    bool busy_ = false;
    bool screenBusy_ = false;
};
