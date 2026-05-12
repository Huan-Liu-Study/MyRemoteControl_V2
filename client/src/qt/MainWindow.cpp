#include "qt/MainWindow.h"

#include <algorithm>

#include <QFormLayout>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QResizeEvent>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <windows.h>

#include "qt/RemoteClientWorker.h"
#include "qt/ScreenClientWorker.h"

namespace {

constexpr int SCREEN_STREAM_FRAME_KEY = 1;
constexpr int SCREEN_STREAM_FRAME_DELTA = 2;

int qtKeyToVirtualKey(int key)
{
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        return key;
    }

    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return VK_F1 + (key - Qt::Key_F1);
    }

    switch (key) {
    case Qt::Key_Backspace:
        return VK_BACK;
    case Qt::Key_Tab:
        return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VK_RETURN;
    case Qt::Key_Shift:
        return VK_SHIFT;
    case Qt::Key_Control:
        return VK_CONTROL;
    case Qt::Key_Alt:
        return VK_MENU;
    case Qt::Key_Escape:
        return VK_ESCAPE;
    case Qt::Key_Space:
        return VK_SPACE;
    case Qt::Key_Left:
        return VK_LEFT;
    case Qt::Key_Up:
        return VK_UP;
    case Qt::Key_Right:
        return VK_RIGHT;
    case Qt::Key_Down:
        return VK_DOWN;
    case Qt::Key_Insert:
        return VK_INSERT;
    case Qt::Key_Delete:
        return VK_DELETE;
    case Qt::Key_Home:
        return VK_HOME;
    case Qt::Key_End:
        return VK_END;
    case Qt::Key_PageUp:
        return VK_PRIOR;
    case Qt::Key_PageDown:
        return VK_NEXT;
    default:
        return 0;
    }
}

} // namespace

RemoteScreenView::RemoteScreenView(QWidget* parent)
    : QWidget(parent)
{
}

void RemoteScreenView::setImage(const QPixmap& image, Qt::TransformationMode mode)
{
    image_ = image;
    transformMode_ = mode;
    update();
}

QRect RemoteScreenView::imageRect() const
{
    if (image_.isNull() || width() <= 0 || height() <= 0) {
        return {};
    }

    QSize scaledSize = image_.size();
    if (scaledSize.width() > width() || scaledSize.height() > height()) {
        scaledSize.scale(size(), Qt::KeepAspectRatio);
    }
    const int x = (width() - scaledSize.width()) / 2;
    const int y = (height() - scaledSize.height()) / 2;
    return QRect(QPoint(x, y), scaledSize);
}

void RemoteScreenView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

void RemoteScreenView::paintEvent(QPaintEvent* event)
{
    (void)event;
    QPainter painter(this);
    painter.fillRect(rect(), QColor(32, 32, 32));

    if (image_.isNull()) {
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect(), Qt::AlignCenter, "No screenshot");
        return;
    }

    const QRect target = imageRect();
    if (target.size() == image_.size()) {
        painter.drawPixmap(target.topLeft(), image_);
        return;
    }

    painter.drawPixmap(target, image_);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
    startNetworkThread();
    setConnected(false);
}

MainWindow::~MainWindow()
{
    stopNetworkThread();
}

void MainWindow::buildUi()
{
    setWindowTitle("Remote Client");
    resize(1200, 760);

    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(8, 8, 8, 6);
    mainLayout->setSpacing(6);

    hostInput_ = new QLineEdit(this);
    hostInput_->setText("127.0.0.1");

    portInput_ = new QSpinBox(this);
    portInput_->setRange(1, 65535);
    portInput_->setValue(12345);

    qualityInput_ = new QSpinBox(this);
    qualityInput_->setRange(10, 95);
    qualityInput_->setValue(60);

    refreshIntervalInput_ = new QSpinBox(this);
    refreshIntervalInput_->setRange(100, 5000);
    refreshIntervalInput_->setSingleStep(50);
    refreshIntervalInput_->setValue(200);

    pathInput_ = new QLineEdit(this);
    pathInput_->setPlaceholderText("C:\\");

    connectButton_ = new QPushButton("Connect", this);
    disconnectButton_ = new QPushButton("Disconnect", this);
    listDrivesButton_ = new QPushButton("List Drives", this);
    listDirButton_ = new QPushButton("List Directory", this);
    downloadButton_ = new QPushButton("Download File", this);
    screenshotButton_ = new QPushButton("Screenshot", this);
    autoRefreshButton_ = new QPushButton("Start Stream", this);

    statusLabel_ = new QLabel("Disconnected", this);
    frameStatsLabel_ = new QLabel("Frame: -", this);
    streamStateLabel_ = new QLabel("Screen stream: idle", this);
    frameStatsLabel_->setWordWrap(true);
    streamStateLabel_->setWordWrap(true);

    resultList_ = new QListWidget(this);
    resultList_->setMinimumHeight(110);

    screenshotLabel_ = new RemoteScreenView(this);
    screenshotLabel_->setMinimumSize(320, 200);
    screenshotLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    screenshotLabel_->setCursor(Qt::CrossCursor);
    screenshotLabel_->setFocusPolicy(Qt::StrongFocus);
    screenshotLabel_->installEventFilter(this);
    screenshotLabel_->setStyleSheet("QLabel { background: #202020; color: #dddddd; border: 1px solid #555555; }");

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMinimumHeight(130);

    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    mainSplitter_->setChildrenCollapsible(false);

    QWidget* screenPanel = new QWidget(mainSplitter_);
    QVBoxLayout* screenLayout = new QVBoxLayout(screenPanel);
    screenLayout->setContentsMargins(0, 0, 0, 0);
    screenLayout->setSpacing(6);
    screenLayout->addWidget(screenshotLabel_, 1);
    screenLayout->addWidget(frameStatsLabel_);
    mainSplitter_->addWidget(screenPanel);

    QWidget* sidePanel = new QWidget(mainSplitter_);
    sidePanel->setMinimumWidth(260);
    sidePanel->setMaximumWidth(340);
    QVBoxLayout* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(8, 0, 0, 0);
    sideLayout->setSpacing(8);

    QGroupBox* connectionBox = new QGroupBox("Connection", sidePanel);
    QVBoxLayout* connectionLayout = new QVBoxLayout(connectionBox);
    QFormLayout* connectionForm = new QFormLayout;
    connectionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    connectionForm->addRow("Host", hostInput_);
    connectionForm->addRow("Port", portInput_);
    QHBoxLayout* connectionButtons = new QHBoxLayout;
    connectionButtons->addWidget(connectButton_);
    connectionButtons->addWidget(disconnectButton_);
    connectionLayout->addLayout(connectionForm);
    connectionLayout->addLayout(connectionButtons);

    QGroupBox* screenBox = new QGroupBox("Screen", sidePanel);
    QVBoxLayout* screenBoxLayout = new QVBoxLayout(screenBox);
    QFormLayout* screenForm = new QFormLayout;
    screenForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    screenForm->addRow("JPEG Quality", qualityInput_);
    screenForm->addRow("Refresh ms", refreshIntervalInput_);
    QHBoxLayout* screenButtons = new QHBoxLayout;
    screenButtons->addWidget(screenshotButton_);
    screenButtons->addWidget(autoRefreshButton_);
    screenBoxLayout->addLayout(screenForm);
    screenBoxLayout->addLayout(screenButtons);

    QGroupBox* filesBox = new QGroupBox("Files", sidePanel);
    QVBoxLayout* filesLayout = new QVBoxLayout(filesBox);
    QFormLayout* filesForm = new QFormLayout;
    filesForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    filesForm->addRow("Path", pathInput_);
    QHBoxLayout* fileButtons = new QHBoxLayout;
    fileButtons->addWidget(listDrivesButton_);
    fileButtons->addWidget(listDirButton_);
    fileButtons->addWidget(downloadButton_);
    filesLayout->addLayout(filesForm);
    filesLayout->addLayout(fileButtons);

    QGroupBox* statusBox = new QGroupBox("Status", sidePanel);
    QVBoxLayout* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->addWidget(statusLabel_);
    statusLayout->addWidget(streamStateLabel_);

    sideLayout->addWidget(connectionBox);
    sideLayout->addWidget(screenBox);
    sideLayout->addWidget(filesBox);
    sideLayout->addWidget(statusBox);
    sideLayout->addWidget(resultList_, 1);
    sideLayout->addWidget(logView_, 1);
    mainSplitter_->addWidget(sidePanel);

    mainSplitter_->setStretchFactor(0, 1);
    mainSplitter_->setStretchFactor(1, 0);
    mainSplitter_->setSizes({920, 280});
    mainLayout->addWidget(mainSplitter_, 1);

    setCentralWidget(central);
    statusBar()->showMessage("Ready");

    connect(connectButton_, &QPushButton::clicked, this, [this]() {
        connectToServer();
    });
    connect(disconnectButton_, &QPushButton::clicked, this, [this]() {
        disconnectFromServer();
    });
    connect(listDrivesButton_, &QPushButton::clicked, this, [this]() {
        listDrives();
    });
    connect(listDirButton_, &QPushButton::clicked, this, [this]() {
        listDirectory();
    });
    connect(downloadButton_, &QPushButton::clicked, this, [this]() {
        downloadFile();
    });
    connect(screenshotButton_, &QPushButton::clicked, this, [this]() {
        requestScreenFrame();
    });
    connect(autoRefreshButton_, &QPushButton::clicked, this, [this]() {
        toggleScreenStream();
    });

    inputRefreshTimer_ = new QTimer(this);
    inputRefreshTimer_->setSingleShot(true);
    inputRefreshTimer_->setInterval(120);
    connect(inputRefreshTimer_, &QTimer::timeout, this, [this]() {
        requestScreenFrame();
    });

    screenReconnectTimer_ = new QTimer(this);
    screenReconnectTimer_->setSingleShot(true);
    screenReconnectTimer_->setInterval(1000);
    connect(screenReconnectTimer_, &QTimer::timeout, this, [this]() {
        reconnectScreenChannel();
    });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == screenshotLabel_ && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat()) {
            const int virtualKey = qtKeyToVirtualKey(keyEvent->key());
            if (virtualKey != 0 && worker_ && connected_) {
                QMetaObject::invokeMethod(worker_, [worker = worker_, virtualKey]() {
                    worker->keyDown(virtualKey);
                }, Qt::QueuedConnection);
                scheduleInputRefresh();
                return true;
            }
        }
    }

    if (watched == screenshotLabel_ && event->type() == QEvent::KeyRelease) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat()) {
            const int virtualKey = qtKeyToVirtualKey(keyEvent->key());
            if (virtualKey != 0 && worker_ && connected_) {
                QMetaObject::invokeMethod(worker_, [worker = worker_, virtualKey]() {
                    worker->keyUp(virtualKey);
                }, Qt::QueuedConnection);
                scheduleInputRefresh();
                return true;
            }
        }
    }

    if (watched == screenshotLabel_ && event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        int button = 0;
        if (mouseEvent->button() == Qt::LeftButton) {
            button = 1;
        } else if (mouseEvent->button() == Qt::RightButton) {
            button = 2;
        } else if (mouseEvent->button() == Qt::MiddleButton) {
            button = 3;
        }

        if (button != 0) {
            screenshotLabel_->setFocus();
            hasScreenshotPress_ = false;
            screenshotPressButton_ = 0;
            clickScreenshotAt(mouseEvent->pos(), button);
            return true;
        }
    }

    if (watched == screenshotLabel_ && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        int button = 0;
        if (mouseEvent->button() == Qt::LeftButton) {
            button = 1;
        } else if (mouseEvent->button() == Qt::RightButton) {
            button = 2;
        } else if (mouseEvent->button() == Qt::MiddleButton) {
            button = 3;
        }

        if (button != 0) {
            screenshotLabel_->setFocus();
            screenshotPressButton_ = button;
            hasScreenshotPress_ = true;
            sendRemoteMouseDown(mouseEvent->pos(), button);
            return true;
        }
    }

    if (watched == screenshotLabel_ && event->type() == QEvent::MouseMove) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (hasScreenshotPress_) {
            sendRemoteMouseMove(mouseEvent->pos());
            return true;
        }
    }

    if (watched == screenshotLabel_ && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (hasScreenshotPress_) {
            const int button = screenshotPressButton_;

            hasScreenshotPress_ = false;
            screenshotPressButton_ = 0;
            lastRemoteMoveX_ = -1;
            lastRemoteMoveY_ = -1;
            sendRemoteMouseUp(mouseEvent->pos(), button);
            return true;
        }
    }

    if (watched == screenshotLabel_ && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        const int delta = wheelEvent->angleDelta().y();
        if (delta != 0) {
            sendRemoteMouseWheel(wheelEvent->position().toPoint(), delta);
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateScreenshotView();
}

void MainWindow::startNetworkThread()
{
    networkThread_ = new QThread(this);
    worker_ = new RemoteClientWorker;
    worker_->moveToThread(networkThread_);

    screenThread_ = new QThread(this);
    screenWorker_ = new ScreenClientWorker;
    screenWorker_->moveToThread(screenThread_);

    connect(networkThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(screenThread_, &QThread::finished, screenWorker_, &QObject::deleteLater);

    connect(worker_, &RemoteClientWorker::connected, this, [this]() {
        controlConnected_ = true;
        if (screenConnected_) {
            setBusy(false);
            setConnected(true);
        }
    });
    connect(worker_, &RemoteClientWorker::disconnected, this, [this](const QString& reason) {
        controlConnected_ = false;
        setBusy(false);
        setConnected(false);
        appendLog(reason);
    });
    connect(worker_, &RemoteClientWorker::logMessage, this, [this](const QString& message) {
        appendLog(message);
    });
    connect(worker_, &RemoteClientWorker::driveListReceived, this, [this](const QStringList& drives) {
        appendLog("Drives:");
        QStringList results;
        results.push_back("Drives");
        for (const QString& drive : drives) {
            appendLog("  " + drive);
            results.push_back(drive);
        }
        showResults(results);
    });
    connect(worker_, &RemoteClientWorker::directoryListReceived, this, [this](const QStringList& entries) {
        appendLog("Directory entries:");
        QStringList results;
        results.push_back("Directory entries");
        for (const QString& entry : entries) {
            appendLog("  " + entry);
            results.push_back(entry);
        }
        showResults(results);
    });
    connect(worker_, &RemoteClientWorker::downloadFinished, this, [this](const QString& localPath) {
        QStringList results;
        results.push_back("Download complete");
        results.push_back(localPath);
        showResults(results);
    });
    connect(worker_, &RemoteClientWorker::mousePositionProbed, this, [this](
        int expectedX,
        int expectedY,
        int actualX,
        int actualY
    ) {
        const int deltaX = actualX - expectedX;
        const int deltaY = actualY - expectedY;
        appendLog("Mouse probe: expected "
                  + QString::number(expectedX) + ", " + QString::number(expectedY)
                  + " actual " + QString::number(actualX) + ", " + QString::number(actualY)
                  + " delta " + QString::number(deltaX) + ", " + QString::number(deltaY));
    });
    connect(screenWorker_, &ScreenClientWorker::connected, this, [this]() {
        screenConnected_ = true;
        screenReconnectPending_ = false;
        screenReconnectAttempts_ = 0;
        if (controlConnected_) {
            setBusy(false);
            setConnected(true);
        }

        if (restartScreenStreamAfterReconnect_) {
            restartScreenStreamAfterReconnect_ = false;
            screenStreamActive_ = true;
            screenBusy_ = true;
            pendingScreenFrame_ = false;
            autoRefreshButton_->setText("Stop Stream");
            appendLog("Screen stream restarting after reconnect");

            const int quality = qualityInput_->value();
            const int intervalMs = refreshIntervalInput_->value();
            QMetaObject::invokeMethod(screenWorker_, [worker = screenWorker_, quality, intervalMs]() {
                worker->startScreenStream(quality, intervalMs);
            }, Qt::QueuedConnection);
            updateScreenStreamStatus();
        }
    });
    connect(screenWorker_, &ScreenClientWorker::disconnected, this, [this](const QString& reason) {
        const bool shouldReconnect = controlConnected_ && !disconnectRequested_ && !reconnectScreenAfterStreamStop_;
        const bool shouldRestartStream = screenStreamActive_ && shouldReconnect;
        screenConnected_ = false;
        screenBusy_ = false;
        pendingScreenFrame_ = false;
        screenStreamActive_ = false;
        restartScreenStreamAfterReconnect_ = shouldRestartStream;
        if (reason.contains(":")) {
            ++screenFailureCount_;
        }
        appendLog(reason);
        if (shouldReconnect) {
            setConnected(true);
            scheduleScreenReconnect();
            return;
        }

        reconnectScreenAfterStreamStop_ = false;
        restartScreenStreamAfterReconnect_ = false;
        if (!controlConnected_) {
            setConnected(false);
        } else {
            setConnected(true);
        }
        updateScreenStreamStatus();
    });
    connect(screenWorker_, &ScreenClientWorker::logMessage, this, [this](const QString& message) {
        appendLog(message);
    });
    connect(screenWorker_, &ScreenClientWorker::screenshotReceived, this, [this](
        const QByteArray& imageData,
        const QString& imageFormat,
        int screenWidth,
        int screenHeight
    ) {
        showScreenshot(imageData, imageFormat, screenWidth, screenHeight);
    });
    connect(screenWorker_, &ScreenClientWorker::screenFrameReceived, this, [this](
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
        QVector<int> rectXs,
        QVector<int> rectYs,
        QVector<int> rectWidths,
        QVector<int> rectHeights,
        QVector<qint64> rectImageSizes,
        qint64 estimatedFullImageSize,
        int captureMs,
        int bltMs,
        int copyMs,
        int compareMs,
        int encodeMs,
        int previousSendMs,
        int fallbackToKeyFrame
    ) {
        showScreenStreamFrame(
            imageData,
            imageFormat,
            screenWidth,
            screenHeight,
            captureWidth,
            captureHeight,
            frameType,
            frameId,
            baseFrameId,
            rectX,
            rectY,
            rectWidth,
            rectHeight,
            rectXs,
            rectYs,
            rectWidths,
            rectHeights,
            rectImageSizes,
            estimatedFullImageSize,
            captureMs,
            bltMs,
            copyMs,
            compareMs,
            encodeMs,
            previousSendMs,
            fallbackToKeyFrame
        );
    });
    connect(screenWorker_, &ScreenClientWorker::requestFinished, this, [this]() {
        screenBusy_ = false;
        if (reconnectScreenAfterStreamStop_) {
            reconnectScreenAfterStreamStop_ = false;
            screenConnected_ = false;
            reconnectScreenChannel();
            updateScreenStreamStatus();
            return;
        }
        if (connected_) {
            setConnected(true);
        }
        updateScreenStreamStatus();
        if (controlConnected_ && !screenConnected_ && !disconnectRequested_) {
            scheduleScreenReconnect();
            return;
        }
        if (pendingScreenFrame_ && connected_) {
            pendingScreenFrame_ = false;
            requestScreenFrame();
        }
    });
    connect(worker_, &RemoteClientWorker::requestFinished, this, [this]() {
        setBusy(false);
    });

    networkThread_->start();
    screenThread_->start();
}

void MainWindow::stopNetworkThread()
{
    disconnectRequested_ = true;
    if (screenReconnectTimer_) {
        screenReconnectTimer_->stop();
    }

    if (screenWorker_ && screenStreamActive_) {
        screenWorker_->stopScreenStream();
    }

    if (worker_) {
        QMetaObject::invokeMethod(worker_, "disconnectFromServer", Qt::BlockingQueuedConnection);
    }
    if (screenWorker_) {
        QMetaObject::invokeMethod(screenWorker_, "disconnectFromServer", Qt::BlockingQueuedConnection);
    }

    if (networkThread_) {
        networkThread_->quit();
        networkThread_->wait();
    }
    if (screenThread_) {
        screenThread_->quit();
        screenThread_->wait();
    }

    networkThread_ = nullptr;
    screenThread_ = nullptr;
    worker_ = nullptr;
    screenWorker_ = nullptr;
}

void MainWindow::connectToServer()
{
    if (!worker_ || !screenWorker_ || busy_) {
        return;
    }

    setBusy(true);
    statusBar()->showMessage("Connecting");
    controlConnected_ = false;
    screenConnected_ = false;
    pendingScreenFrame_ = false;
    screenStreamActive_ = false;
    reconnectScreenAfterStreamStop_ = false;
    restartScreenStreamAfterReconnect_ = false;
    disconnectRequested_ = false;
    screenReconnectPending_ = false;
    screenReconnectAttempts_ = 0;
    screenFailureCount_ = 0;
    lastFrameBytes_ = 0;
    lastFrameElapsedMs_ = 0;
    lastEstimatedFullFrameBytes_ = 0;
    lastDeltaSavePercent_ = 0.0;
    currentStreamFrameId_ = 0;
    currentStreamBaseFrameId_ = 0;
    skippedDeltaFrameCount_ = 0;
    requestedKeyFrameCount_ = 0;
    lastStreamFrameType_ = 0;
    lastStreamRectX_ = 0;
    lastStreamRectY_ = 0;
    lastStreamRectWidth_ = 0;
    lastStreamRectHeight_ = 0;
    lastStreamRectCount_ = 0;
    lastCaptureMs_ = 0;
    lastCompareMs_ = 0;
    lastEncodeMs_ = 0;
    lastPreviousSendMs_ = 0;
    lastFallbackToKeyFrame_ = 0;
    keyFrameRequestPending_ = false;
    updateScreenStreamStatus();

    const QString host = hostInput_->text();
    const int port = portInput_->value();

    QMetaObject::invokeMethod(worker_, [worker = worker_, host, port]() {
        worker->connectToServer(host, port);
    }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(screenWorker_, [worker = screenWorker_, host, port]() {
        worker->connectToServer(host, port);
    }, Qt::QueuedConnection);
}

void MainWindow::disconnectFromServer()
{
    if (!worker_ || busy_) {
        return;
    }

    setBusy(true);
    disconnectRequested_ = true;
    screenReconnectPending_ = false;
    restartScreenStreamAfterReconnect_ = false;
    if (screenReconnectTimer_) {
        screenReconnectTimer_->stop();
    }
    if (screenWorker_ && screenStreamActive_) {
        screenWorker_->stopScreenStream();
        screenStreamActive_ = false;
        reconnectScreenAfterStreamStop_ = false;
    }

    QMetaObject::invokeMethod(worker_, [worker = worker_]() {
        worker->disconnectFromServer();
    }, Qt::QueuedConnection);
    if (screenWorker_) {
        QMetaObject::invokeMethod(screenWorker_, [worker = screenWorker_]() {
            worker->disconnectFromServer();
        }, Qt::QueuedConnection);
    }
}

void MainWindow::listDrives()
{
    if (!worker_ || busy_) {
        return;
    }

    setBusy(true);
    QMetaObject::invokeMethod(worker_, [worker = worker_]() {
        worker->listDrives();
    }, Qt::QueuedConnection);
}

void MainWindow::listDirectory()
{
    if (!worker_ || busy_) {
        return;
    }

    const QString path = pathInput_->text().trimmed();
    if (path.isEmpty()) {
        appendLog("Path is empty");
        return;
    }

    setBusy(true);
    QMetaObject::invokeMethod(worker_, [worker = worker_, path]() {
        worker->listDirectory(path);
    }, Qt::QueuedConnection);
}

void MainWindow::downloadFile()
{
    if (!worker_ || busy_) {
        return;
    }

    const QString remotePath = pathInput_->text().trimmed();
    if (remotePath.isEmpty()) {
        appendLog("Remote file path is empty");
        return;
    }

    const QString localDir = QFileDialog::getExistingDirectory(this, "Select save directory");
    if (localDir.isEmpty()) {
        return;
    }

    setBusy(true);
    QMetaObject::invokeMethod(worker_, [worker = worker_, remotePath, localDir]() {
        worker->downloadFile(remotePath, localDir);
    }, Qt::QueuedConnection);
}

void MainWindow::requestScreenFrame()
{
    if (!screenWorker_ || !connected_ || !screenConnected_) {
        return;
    }

    if (screenStreamActive_) {
        return;
    }

    if (screenBusy_) {
        pendingScreenFrame_ = true;
        updateScreenStreamStatus();
        return;
    }

    screenBusy_ = true;
    screenshotRequestTimer_.restart();
    setConnected(connected_);
    updateScreenStreamStatus();
    const int quality = qualityInput_->value();
    QMetaObject::invokeMethod(screenWorker_, [worker = screenWorker_, quality]() {
        worker->takeScreenshot(quality);
    }, Qt::QueuedConnection);
}

void MainWindow::toggleScreenStream()
{
    if (!screenWorker_ || !connected_ || !screenConnected_) {
        return;
    }

    if (screenStreamActive_) {
        screenStreamActive_ = false;
        reconnectScreenAfterStreamStop_ = true;
        autoRefreshButton_->setText("Start Stream");
        appendLog("Screen stream stopping");
        screenWorker_->stopScreenStream();
        updateScreenStreamStatus();
        return;
    }

    screenStreamActive_ = true;
    screenBusy_ = true;
    pendingScreenFrame_ = false;
    autoRefreshButton_->setText("Stop Stream");
    appendLog("Screen stream starting");
    updateScreenStreamStatus();

    const int quality = qualityInput_->value();
    const int intervalMs = refreshIntervalInput_->value();
    QMetaObject::invokeMethod(screenWorker_, [worker = screenWorker_, quality, intervalMs]() {
        worker->startScreenStream(quality, intervalMs);
    }, Qt::QueuedConnection);
}

void MainWindow::scheduleScreenReconnect()
{
    if (!screenReconnectTimer_ || !controlConnected_ || disconnectRequested_) {
        return;
    }

    if (screenReconnectPending_) {
        return;
    }

    screenReconnectPending_ = true;
    ++screenReconnectAttempts_;
    appendLog("Screen channel reconnect scheduled, attempt " + QString::number(screenReconnectAttempts_));
    screenReconnectTimer_->start();
    updateScreenStreamStatus();
}

void MainWindow::reconnectScreenChannel()
{
    screenReconnectPending_ = false;
    if (!screenWorker_ || !controlConnected_ || disconnectRequested_) {
        updateScreenStreamStatus();
        return;
    }

    const QString host = hostInput_->text();
    const int port = portInput_->value();
    appendLog("Reconnecting screen channel");
    QMetaObject::invokeMethod(screenWorker_, [worker = screenWorker_, host, port]() {
        worker->connectToServer(host, port);
    }, Qt::QueuedConnection);
    updateScreenStreamStatus();
}

void MainWindow::scheduleInputRefresh()
{
    if (!inputRefreshTimer_ || !connected_ || screenStreamActive_) {
        return;
    }

    inputRefreshTimer_->start();
}

void MainWindow::setConnected(bool connected)
{
    if (!connected) {
        controlConnected_ = false;
        screenConnected_ = false;
        screenBusy_ = false;
        pendingScreenFrame_ = false;
        screenStreamActive_ = false;
        reconnectScreenAfterStreamStop_ = false;
        restartScreenStreamAfterReconnect_ = false;
        screenReconnectPending_ = false;
        lastFrameBytes_ = 0;
        lastFrameElapsedMs_ = 0;
        lastEstimatedFullFrameBytes_ = 0;
        lastDeltaSavePercent_ = 0.0;
        currentStreamFrameId_ = 0;
        currentStreamBaseFrameId_ = 0;
        skippedDeltaFrameCount_ = 0;
        requestedKeyFrameCount_ = 0;
        lastStreamFrameType_ = 0;
        lastStreamRectX_ = 0;
        lastStreamRectY_ = 0;
        lastStreamRectWidth_ = 0;
        lastStreamRectHeight_ = 0;
        lastStreamRectCount_ = 0;
        lastCaptureMs_ = 0;
        lastCompareMs_ = 0;
        lastEncodeMs_ = 0;
        lastPreviousSendMs_ = 0;
        lastFallbackToKeyFrame_ = 0;
        remoteScreenWidth_ = 0;
        remoteScreenHeight_ = 0;
        keyFrameRequestPending_ = false;
        if (autoRefreshButton_) {
            autoRefreshButton_->setText("Start Stream");
        }
        if (inputRefreshTimer_) {
            inputRefreshTimer_->stop();
        }
        if (screenReconnectTimer_) {
            screenReconnectTimer_->stop();
        }
    }

    connected_ = connected;

    connectButton_->setEnabled(!connected && !busy_);
    disconnectButton_->setEnabled(connected && !busy_);
    listDrivesButton_->setEnabled(connected && !busy_);
    listDirButton_->setEnabled(connected && !busy_);
    downloadButton_->setEnabled(connected && !busy_);
    screenshotButton_->setEnabled(connected && screenConnected_ && !screenBusy_ && !screenStreamActive_);
    autoRefreshButton_->setEnabled(connected && screenConnected_);
    hostInput_->setEnabled(!connected);
    portInput_->setEnabled(!connected);
    qualityInput_->setEnabled(connected);
    refreshIntervalInput_->setEnabled(connected);
    pathInput_->setEnabled(connected);

    statusLabel_->setText(connected ? "Connected" : "Disconnected");
    statusBar()->showMessage(connected ? "Connected" : "Ready");
    updateScreenStreamStatus();
}

void MainWindow::setBusy(bool busy)
{
    busy_ = busy;

    connectButton_->setEnabled(!connected_ && !busy_);
    disconnectButton_->setEnabled(connected_ && !busy_);
    listDrivesButton_->setEnabled(connected_ && !busy_);
    listDirButton_->setEnabled(connected_ && !busy_);
    downloadButton_->setEnabled(connected_ && !busy_);
    screenshotButton_->setEnabled(connected_ && screenConnected_ && !screenBusy_ && !screenStreamActive_);
    autoRefreshButton_->setEnabled(connected_ && screenConnected_);

    if (!busy_) {
        statusBar()->showMessage(connected_ ? "Connected" : "Ready");
    }
    updateScreenStreamStatus();
}

void MainWindow::appendLog(const QString& text)
{
    logView_->appendPlainText(text);
}

void MainWindow::showResults(const QStringList& titleAndItems)
{
    resultList_->clear();

    for (const QString& item : titleAndItems) {
        resultList_->addItem(item);
    }
}

void MainWindow::showScreenshot(const QByteArray& imageData, const QString& imageFormat, int screenWidth, int screenHeight)
{
    QPixmap pixmap;
    const QByteArray formatBytes = imageFormat.toLatin1();
    if (!pixmap.loadFromData(imageData, formatBytes.constData())) {
        appendLog("Failed to load screenshot from memory");
        return;
    }

    currentScreenshot_ = pixmap;
    remoteScreenWidth_ = screenWidth > 0 ? screenWidth : currentScreenshot_.width();
    remoteScreenHeight_ = screenHeight > 0 ? screenHeight : currentScreenshot_.height();
    updateScreenshotView();

    const qint64 elapsedMs = screenStreamActive_ ? 0 : (screenshotRequestTimer_.isValid() ? screenshotRequestTimer_.elapsed() : 0);
    lastFrameBytes_ = imageData.size();
    lastFrameElapsedMs_ = elapsedMs;
    updateFrameStats(imageData.size(), elapsedMs);
    updateScreenStreamStatus();

    if (!screenStreamActive_) {
        QStringList results;
        results.push_back("Screenshot loaded");
        results.push_back(imageFormat + ", " + QString::number(imageData.size()) + " bytes");
        results.push_back("Remote screen: " + QString::number(remoteScreenWidth_) + " x " + QString::number(remoteScreenHeight_));
        showResults(results);
    }
}

void MainWindow::showScreenStreamFrame(
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
)
{
    if (frameType == SCREEN_STREAM_FRAME_KEY) {
        QPixmap pixmap;
        const QByteArray formatBytes = imageFormat.toLatin1();
        if (!pixmap.loadFromData(imageData, formatBytes.constData())) {
            appendLog("Failed to load key frame from memory");
            return;
        }

        currentScreenshot_ = pixmap;
        currentStreamFrameId_ = frameId;
        currentStreamBaseFrameId_ = 0;
        keyFrameRequestPending_ = false;
    } else if (frameType == SCREEN_STREAM_FRAME_DELTA) {
        if (currentScreenshot_.isNull() || currentStreamFrameId_ != baseFrameId) {
            ++skippedDeltaFrameCount_;
            appendLog("Delta frame skipped: frame="
                      + QString::number(frameId)
                      + " base=" + QString::number(baseFrameId)
                      + " current=" + QString::number(currentStreamFrameId_));
            if (!keyFrameRequestPending_ && screenWorker_) {
                keyFrameRequestPending_ = true;
                ++requestedKeyFrameCount_;
                screenWorker_->requestKeyFrame();
            }
            updateScreenStreamStatus();
            return;
        }

        qsizetype imageOffset = 0;
        const int rectCount = rectImageSizes.size();
        if (rectXs.size() != rectCount
            || rectYs.size() != rectCount
            || rectWidths.size() != rectCount
            || rectHeights.size() != rectCount) {
            appendLog("Delta frame skipped: invalid rect metadata");
            return;
        }

        for (int i = 0; i < rectCount; ++i) {
            const qint64 rectImageSize = rectImageSizes[i];
            if (rectImageSize <= 0) {
                continue;
            }

            if (imageOffset + rectImageSize > imageData.size()) {
                appendLog("Delta frame skipped: invalid rect image size");
                return;
            }

            const QByteArray rectData = imageData.mid(imageOffset, static_cast<qsizetype>(rectImageSize));
            imageOffset += static_cast<qsizetype>(rectImageSize);

            QPixmap rectPixmap;
            const QByteArray formatBytes = imageFormat.toLatin1();
            if (!rectPixmap.loadFromData(rectData, formatBytes.constData())) {
                appendLog("Failed to load delta rect from memory");
                return;
            }

            QPainter painter(&currentScreenshot_);
            painter.drawPixmap(rectXs[i], rectYs[i], rectWidths[i], rectHeights[i], rectPixmap);
        }

        currentStreamFrameId_ = frameId;
        currentStreamBaseFrameId_ = baseFrameId;
    } else {
        appendLog("Unknown screen frame type: " + QString::number(frameType));
        return;
    }

    lastStreamFrameType_ = frameType;
    lastStreamRectX_ = rectX;
    lastStreamRectY_ = rectY;
    lastStreamRectWidth_ = rectWidth;
    lastStreamRectHeight_ = rectHeight;
    lastStreamRectCount_ = rectImageSizes.size();
    lastEstimatedFullFrameBytes_ = estimatedFullImageSize;
    lastDeltaSavePercent_ = 0.0;
    if (estimatedFullImageSize > 0 && imageData.size() <= estimatedFullImageSize) {
        lastDeltaSavePercent_ = 100.0 * (1.0 - static_cast<double>(imageData.size()) / static_cast<double>(estimatedFullImageSize));
    }
    lastCaptureMs_ = captureMs;
    lastBltMs_ = bltMs;
    lastCopyMs_ = copyMs;
    lastCompareMs_ = compareMs;
    lastEncodeMs_ = encodeMs;
    lastPreviousSendMs_ = previousSendMs;
    lastFallbackToKeyFrame_ = fallbackToKeyFrame;

    remoteScreenWidth_ = screenWidth > 0 ? screenWidth : captureWidth;
    remoteScreenHeight_ = screenHeight > 0 ? screenHeight : captureHeight;
    updateScreenshotView();

    lastFrameBytes_ = imageData.size();
    lastFrameElapsedMs_ = 0;
    updateFrameStats(imageData.size(), 0);
    updateScreenStreamStatus();
}

void MainWindow::updateScreenshotView()
{
    if (!screenshotLabel_ || currentScreenshot_.isNull()) {
        return;
    }

    const Qt::TransformationMode transformMode = screenStreamActive_
        ? Qt::FastTransformation
        : Qt::SmoothTransformation;

    screenshotLabel_->setImage(currentScreenshot_, transformMode);
}

void MainWindow::updateScreenStreamStatus()
{
    if (!streamStateLabel_) {
        return;
    }

    QString state = "idle";
    if (!connected_) {
        state = "disconnected";
    } else if (screenStreamActive_) {
        state = "streaming";
    } else if (screenReconnectPending_) {
        state = "reconnecting screen";
    } else if (!screenConnected_ && controlConnected_) {
        state = "screen disconnected";
    } else if (screenBusy_) {
        state = "pulling frame";
    } else if (pendingScreenFrame_) {
        state = "pending";
    }

    QString text = "Screen stream: " + state;
    text += ", pending=" + QString(pendingScreenFrame_ ? "yes" : "no");
    text += ", quality=" + QString::number(qualityInput_ ? qualityInput_->value() : 0);
    text += ", interval=" + QString::number(refreshIntervalInput_ ? refreshIntervalInput_->value() : 0) + "ms";

    if (lastFrameBytes_ > 0) {
        text += ", last=" + QString::number(lastFrameBytes_ / 1024.0, 'f', 1) + "KB";
        if (lastFrameElapsedMs_ > 0) {
            text += "/" + QString::number(lastFrameElapsedMs_) + "ms";
        }
    }

    if (currentStreamFrameId_ > 0) {
        text += ", frame=" + currentFrameTypeText()
            + "#" + QString::number(currentStreamFrameId_);
        if (currentStreamBaseFrameId_ > 0) {
            text += "/base=" + QString::number(currentStreamBaseFrameId_);
        }
        text += ", rect=" + QString::number(lastStreamRectWidth_)
            + "x" + QString::number(lastStreamRectHeight_)
            + "@" + QString::number(lastStreamRectX_)
            + "," + QString::number(lastStreamRectY_);
        text += ", rectCount=" + QString::number(lastStreamRectCount_);
        if (lastEstimatedFullFrameBytes_ > 0) {
            text += ", full~=" + QString::number(lastEstimatedFullFrameBytes_ / 1024.0, 'f', 1) + "KB";
            text += ", save=" + QString::number(lastDeltaSavePercent_, 'f', 1) + "%";
        }
        text += ", ms=c" + QString::number(lastCaptureMs_)
            + "/blt" + QString::number(lastBltMs_)
            + "/copy" + QString::number(lastCopyMs_)
            + "/cmp" + QString::number(lastCompareMs_)
            + "/enc" + QString::number(lastEncodeMs_)
            + "/prevSend" + QString::number(lastPreviousSendMs_);
        if (lastFallbackToKeyFrame_ != 0) {
            text += ", fallback=KEY";
        }
    }

    if (skippedDeltaFrameCount_ > 0) {
        text += ", skippedDelta=" + QString::number(skippedDeltaFrameCount_);
    }

    if (requestedKeyFrameCount_ > 0) {
        text += ", keyReq=" + QString::number(requestedKeyFrameCount_);
    }

    if (screenFailureCount_ > 0) {
        text += ", failures=" + QString::number(screenFailureCount_);
    }

    streamStateLabel_->setText(text);
}

void MainWindow::updateFrameStats(qint64 bytes, qint64 elapsedMs)
{
    if (!frameRateTimer_.isValid()) {
        frameRateTimer_.start();
    }

    ++framesSinceRateUpdate_;

    double fps = 0.0;
    const qint64 rateElapsedMs = frameRateTimer_.elapsed();
    if (rateElapsedMs >= 1000) {
        fps = framesSinceRateUpdate_ * 1000.0 / static_cast<double>(rateElapsedMs);
        framesSinceRateUpdate_ = 0;
        frameRateTimer_.restart();
    }

    const double kb = bytes / 1024.0;
    QString text = "Frame: ";
    if (currentStreamFrameId_ > 0) {
        text += currentFrameTypeText()
            + " #" + QString::number(currentStreamFrameId_);
        if (currentStreamBaseFrameId_ > 0) {
            text += " base=" + QString::number(currentStreamBaseFrameId_);
        }
        text += " rect=" + QString::number(lastStreamRectWidth_)
            + "x" + QString::number(lastStreamRectHeight_);
        text += " rectCount=" + QString::number(lastStreamRectCount_);
        if (lastEstimatedFullFrameBytes_ > 0) {
            text += " save=" + QString::number(lastDeltaSavePercent_, 'f', 1) + "%";
        }
        text += " ms=c" + QString::number(lastCaptureMs_)
            + "/blt" + QString::number(lastBltMs_)
            + "/copy" + QString::number(lastCopyMs_)
            + "/cmp" + QString::number(lastCompareMs_)
            + "/enc" + QString::number(lastEncodeMs_)
            + "/prevSend" + QString::number(lastPreviousSendMs_);
        text += ", ";
    }

    text += QString::number(kb, 'f', 1) + " KB";
    if (elapsedMs > 0) {
        text += ", " + QString::number(elapsedMs) + " ms";
    }
    if (fps > 0.0) {
        text += ", " + QString::number(fps, 'f', 1) + " FPS";
    }

    frameStatsLabel_->setText(text);
}

QString MainWindow::currentFrameTypeText() const
{
    if (lastStreamFrameType_ == SCREEN_STREAM_FRAME_KEY) {
        return "KEY";
    }

    if (lastStreamFrameType_ == SCREEN_STREAM_FRAME_DELTA) {
        if (lastStreamRectWidth_ == 0 || lastStreamRectHeight_ == 0) {
            return "EMPTY_DELTA";
        }

        return "DELTA";
    }

    return "-";
}

bool MainWindow::mapScreenshotPoint(const QPoint& labelPoint, int& outRemoteX, int& outRemoteY) const
{
    if (!screenshotLabel_ || currentScreenshot_.isNull()) {
        return false;
    }

    const QRect imageRect = screenshotLabel_->imageRect();
    if (imageRect.isEmpty()) {
        return false;
    }

    if (!imageRect.contains(labelPoint)) {
        return false;
    }

    const int remoteWidth = remoteScreenWidth_ > 0 ? remoteScreenWidth_ : currentScreenshot_.width();
    const int remoteHeight = remoteScreenHeight_ > 0 ? remoteScreenHeight_ : currentScreenshot_.height();
    const int imageX = labelPoint.x() - imageRect.x();
    const int imageY = labelPoint.y() - imageRect.y();

    outRemoteX = static_cast<int>(
        (static_cast<int64_t>(imageX) * remoteWidth + imageRect.width() / 2)
        / imageRect.width()
    );
    outRemoteY = static_cast<int>(
        (static_cast<int64_t>(imageY) * remoteHeight + imageRect.height() / 2)
        / imageRect.height()
    );
    outRemoteX = (std::max)(0, (std::min)(outRemoteX, remoteWidth - 1));
    outRemoteY = (std::max)(0, (std::min)(outRemoteY, remoteHeight - 1));
    return true;
}

void MainWindow::clickScreenshotAt(const QPoint& labelPoint, int button)
{
    if (!worker_ || !connected_) {
        return;
    }

    int remoteX = 0;
    int remoteY = 0;
    if (!mapScreenshotPoint(labelPoint, remoteX, remoteY)) {
        return;
    }

    QMetaObject::invokeMethod(worker_, [worker = worker_, remoteX, remoteY, button]() {
        worker->clickMouseAt(remoteX, remoteY, button);
    }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(worker_, [worker = worker_, remoteX, remoteY]() {
        worker->probeMousePosition(remoteX, remoteY);
    }, Qt::QueuedConnection);
    scheduleInputRefresh();
}

void MainWindow::sendRemoteMouseDown(const QPoint& labelPoint, int button)
{
    if (!worker_ || !connected_) {
        return;
    }

    int remoteX = 0;
    int remoteY = 0;
    if (!mapScreenshotPoint(labelPoint, remoteX, remoteY)) {
        return;
    }

    lastRemoteMoveX_ = remoteX;
    lastRemoteMoveY_ = remoteY;
    lastMoveSentAt_.restart();
    QMetaObject::invokeMethod(worker_, [worker = worker_, remoteX, remoteY, button]() {
        worker->mouseDownAt(remoteX, remoteY, button);
    }, Qt::QueuedConnection);
    scheduleInputRefresh();
}

void MainWindow::sendRemoteMouseMove(const QPoint& labelPoint)
{
    if (!worker_ || !connected_) {
        return;
    }

    int remoteX = 0;
    int remoteY = 0;
    if (!mapScreenshotPoint(labelPoint, remoteX, remoteY)) {
        return;
    }

    if (remoteX == lastRemoteMoveX_ && remoteY == lastRemoteMoveY_) {
        return;
    }

    if (lastMoveSentAt_.isValid() && lastMoveSentAt_.elapsed() < 16) {
        return;
    }

    lastRemoteMoveX_ = remoteX;
    lastRemoteMoveY_ = remoteY;
    lastMoveSentAt_.restart();
    QMetaObject::invokeMethod(worker_, [worker = worker_, remoteX, remoteY]() {
        worker->mouseMoveAt(remoteX, remoteY);
    }, Qt::QueuedConnection);
}

void MainWindow::sendRemoteMouseUp(const QPoint& labelPoint, int button)
{
    if (!worker_ || !connected_) {
        return;
    }

    int remoteX = 0;
    int remoteY = 0;
    if (!mapScreenshotPoint(labelPoint, remoteX, remoteY)) {
        return;
    }

    QMetaObject::invokeMethod(worker_, [worker = worker_, remoteX, remoteY, button]() {
        worker->mouseUpAt(remoteX, remoteY, button);
    }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(worker_, [worker = worker_, remoteX, remoteY]() {
        worker->probeMousePosition(remoteX, remoteY);
    }, Qt::QueuedConnection);
    scheduleInputRefresh();
}

void MainWindow::sendRemoteMouseWheel(const QPoint& labelPoint, int delta)
{
    if (!worker_ || !connected_) {
        return;
    }

    int remoteX = 0;
    int remoteY = 0;
    if (!mapScreenshotPoint(labelPoint, remoteX, remoteY)) {
        return;
    }

    QMetaObject::invokeMethod(worker_, [worker = worker_, remoteX, remoteY, delta]() {
        worker->mouseWheelAt(remoteX, remoteY, delta);
    }, Qt::QueuedConnection);
    scheduleInputRefresh();
}
