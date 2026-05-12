#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPoint>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QSpinBox;
class QSplitter;
class QThread;
class QTimer;
class RemoteClientWorker;
class ScreenClientWorker;

class RemoteScreenView : public QWidget {
public:
    explicit RemoteScreenView(QWidget* parent = nullptr);

    void setImage(const QPixmap& image, Qt::TransformationMode mode);
    QRect imageRect() const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap image_;
    Qt::TransformationMode transformMode_ = Qt::SmoothTransformation;
};

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
    void toggleScreenStream();
    void scheduleScreenReconnect();
    void reconnectScreenChannel();
    void scheduleInputRefresh();
    void setConnected(bool connected);
    void setBusy(bool busy);
    void appendLog(const QString& text);
    void showResults(const QStringList& titleAndItems);
    void showScreenshot(const QByteArray& imageData, const QString& imageFormat, int screenWidth, int screenHeight);
    void showScreenStreamFrame(
        const QByteArray& imageData,
        const QString& imageFormat,
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
        const QVector<int>& rectXs,
        const QVector<int>& rectYs,
        const QVector<int>& rectWidths,
        const QVector<int>& rectHeights,
        const QVector<qint64>& rectImageSizes,
        qint64 estimatedFullImageSize,
        int captureMs,
        int bltMs,
        int copyMs,
        int compareMs,
        int encodeMs,
        int previousSendMs,
        int fallbackToKeyFrame
    );
    void updateScreenshotView();
    void updateScreenStreamStatus();
    void updateFrameStats(qint64 bytes, qint64 elapsedMs);
    QString currentFrameTypeText() const;
    bool mapScreenshotPoint(const QPoint& labelPoint, int& outRemoteX, int& outRemoteY) const;
    void clickScreenshotAt(const QPoint& labelPoint, int button);
    void sendRemoteMouseDown(const QPoint& labelPoint, int button);
    void sendRemoteMouseMove(const QPoint& labelPoint);
    void sendRemoteMouseUp(const QPoint& labelPoint, int button);
    void sendRemoteMouseWheel(const QPoint& labelPoint, int delta);

    QLineEdit* hostInput_ = nullptr;
    QSpinBox* portInput_ = nullptr;
    QSpinBox* qualityInput_ = nullptr;
    QSpinBox* refreshIntervalInput_ = nullptr;
    QLineEdit* pathInput_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* frameStatsLabel_ = nullptr;
    QLabel* streamStateLabel_ = nullptr;
    QListWidget* resultList_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    RemoteScreenView* screenshotLabel_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QPushButton* listDrivesButton_ = nullptr;
    QPushButton* listDirButton_ = nullptr;
    QPushButton* downloadButton_ = nullptr;
    QPushButton* screenshotButton_ = nullptr;
    QPushButton* autoRefreshButton_ = nullptr;

    QThread* networkThread_ = nullptr;
    QThread* screenThread_ = nullptr;
    QTimer* inputRefreshTimer_ = nullptr;
    QTimer* screenReconnectTimer_ = nullptr;
    RemoteClientWorker* worker_ = nullptr;
    ScreenClientWorker* screenWorker_ = nullptr;
    QPixmap currentScreenshot_;
    QElapsedTimer screenshotRequestTimer_;
    QElapsedTimer frameRateTimer_;
    qint64 lastFrameBytes_ = 0;
    qint64 lastFrameElapsedMs_ = 0;
    qint64 lastEstimatedFullFrameBytes_ = 0;
    double lastDeltaSavePercent_ = 0.0;
    quint64 currentStreamFrameId_ = 0;
    quint64 currentStreamBaseFrameId_ = 0;
    quint64 skippedDeltaFrameCount_ = 0;
    quint64 requestedKeyFrameCount_ = 0;
    int screenFailureCount_ = 0;
    int remoteScreenWidth_ = 0;
    int remoteScreenHeight_ = 0;
    int framesSinceRateUpdate_ = 0;
    int lastStreamFrameType_ = 0;
    int lastStreamRectX_ = 0;
    int lastStreamRectY_ = 0;
    int lastStreamRectWidth_ = 0;
    int lastStreamRectHeight_ = 0;
    int lastStreamRectCount_ = 0;
    int lastCaptureMs_ = 0;
    int lastBltMs_ = 0;
    int lastCopyMs_ = 0;
    int lastCompareMs_ = 0;
    int lastEncodeMs_ = 0;
    int lastPreviousSendMs_ = 0;
    int lastFallbackToKeyFrame_ = 0;
    int screenReconnectAttempts_ = 0;
    int screenshotPressButton_ = 0;
    int lastRemoteMoveX_ = -1;
    int lastRemoteMoveY_ = -1;
    QElapsedTimer lastMoveSentAt_;
    bool hasScreenshotPress_ = false;
    bool controlConnected_ = false;
    bool screenConnected_ = false;
    bool pendingScreenFrame_ = false;
    bool screenStreamActive_ = false;
    bool reconnectScreenAfterStreamStop_ = false;
    bool restartScreenStreamAfterReconnect_ = false;
    bool disconnectRequested_ = false;
    bool screenReconnectPending_ = false;
    bool keyFrameRequestPending_ = false;
    bool connected_ = false;
    bool busy_ = false;
    bool screenBusy_ = false;
};
