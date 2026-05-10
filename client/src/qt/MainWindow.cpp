#include "qt/MainWindow.h"

#include <QFormLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSize>
#include <QSpinBox>
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
    resize(900, 640);

    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(12, 12, 12, 8);
    mainLayout->setSpacing(8);

    hostInput_ = new QLineEdit(this);
    hostInput_->setText("127.0.0.1");

    portInput_ = new QSpinBox(this);
    portInput_->setRange(1, 65535);
    portInput_->setValue(12345);

    qualityInput_ = new QSpinBox(this);
    qualityInput_->setRange(10, 95);
    qualityInput_->setValue(70);

    scaleInput_ = new QSpinBox(this);
    scaleInput_->setRange(25, 100);
    scaleInput_->setSingleStep(25);
    scaleInput_->setValue(100);

    refreshIntervalInput_ = new QSpinBox(this);
    refreshIntervalInput_->setRange(200, 5000);
    refreshIntervalInput_->setSingleStep(100);
    refreshIntervalInput_->setValue(1000);

    pathInput_ = new QLineEdit(this);
    pathInput_->setPlaceholderText("C:\\");

    QFormLayout* formLayout = new QFormLayout;
    formLayout->addRow("Host", hostInput_);
    formLayout->addRow("Port", portInput_);
    formLayout->addRow("JPEG Quality", qualityInput_);
    formLayout->addRow("Scale %", scaleInput_);
    formLayout->addRow("Refresh ms", refreshIntervalInput_);
    formLayout->addRow("Path", pathInput_);

    connectButton_ = new QPushButton("Connect", this);
    disconnectButton_ = new QPushButton("Disconnect", this);
    listDrivesButton_ = new QPushButton("List Drives", this);
    listDirButton_ = new QPushButton("List Directory", this);
    downloadButton_ = new QPushButton("Download File", this);
    screenshotButton_ = new QPushButton("Screenshot", this);
    autoRefreshButton_ = new QPushButton("Auto Refresh", this);

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(connectButton_);
    buttonLayout->addWidget(disconnectButton_);
    buttonLayout->addWidget(listDrivesButton_);
    buttonLayout->addWidget(listDirButton_);
    buttonLayout->addWidget(downloadButton_);
    buttonLayout->addWidget(screenshotButton_);
    buttonLayout->addWidget(autoRefreshButton_);

    statusLabel_ = new QLabel("Disconnected", this);
    frameStatsLabel_ = new QLabel("Frame: -", this);
    streamStateLabel_ = new QLabel("Screen stream: idle", this);

    resultList_ = new QListWidget(this);
    resultList_->setMaximumHeight(100);

    screenshotLabel_ = new QLabel("No screenshot", this);
    screenshotLabel_->setAlignment(Qt::AlignCenter);
    screenshotLabel_->setMinimumHeight(300);
    screenshotLabel_->setCursor(Qt::CrossCursor);
    screenshotLabel_->setFocusPolicy(Qt::StrongFocus);
    screenshotLabel_->installEventFilter(this);
    screenshotLabel_->setStyleSheet("QLabel { background: #202020; color: #dddddd; border: 1px solid #555555; }");

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumHeight(120);

    mainLayout->addLayout(formLayout);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(statusLabel_);
    mainLayout->addWidget(frameStatsLabel_);
    mainLayout->addWidget(streamStateLabel_);
    mainLayout->addWidget(screenshotLabel_, 1);
    mainLayout->addWidget(resultList_);
    mainLayout->addWidget(logView_);

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
        toggleAutoRefresh();
    });

    autoRefreshTimer_ = new QTimer(this);
    autoRefreshTimer_->setInterval(refreshIntervalInput_->value());
    connect(autoRefreshTimer_, &QTimer::timeout, this, [this]() {
        requestScreenFrame();
    });
    connect(refreshIntervalInput_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (autoRefreshTimer_) {
            autoRefreshTimer_->setInterval(value);
        }
    });

    inputRefreshTimer_ = new QTimer(this);
    inputRefreshTimer_->setSingleShot(true);
    inputRefreshTimer_->setInterval(120);
    connect(inputRefreshTimer_, &QTimer::timeout, this, [this]() {
        requestScreenFrame();
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
    connect(screenWorker_, &ScreenClientWorker::connected, this, [this]() {
        screenConnected_ = true;
        if (controlConnected_) {
            setBusy(false);
            setConnected(true);
        }
    });
    connect(screenWorker_, &ScreenClientWorker::disconnected, this, [this](const QString& reason) {
        screenConnected_ = false;
        screenBusy_ = false;
        pendingScreenFrame_ = false;
        if (reason.contains(":")) {
            ++screenFailureCount_;
        }
        setConnected(false);
        appendLog(reason);
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
    connect(screenWorker_, &ScreenClientWorker::requestFinished, this, [this]() {
        screenBusy_ = false;
        if (connected_) {
            setConnected(true);
        }
        updateScreenStreamStatus();
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
    screenFailureCount_ = 0;
    lastFrameBytes_ = 0;
    lastFrameElapsedMs_ = 0;
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
    const int scalePercent = scaleInput_->value();
    QMetaObject::invokeMethod(screenWorker_, [worker = screenWorker_, quality, scalePercent]() {
        worker->takeScreenshot(quality, scalePercent);
    }, Qt::QueuedConnection);
}

void MainWindow::toggleAutoRefresh()
{
    if (!autoRefreshTimer_ || !connected_) {
        return;
    }

    if (autoRefreshTimer_->isActive()) {
        autoRefreshTimer_->stop();
        autoRefreshButton_->setText("Auto Refresh");
        appendLog("Auto refresh stopped");
        return;
    }

    autoRefreshTimer_->start();
    autoRefreshButton_->setText("Stop Refresh");
    appendLog("Auto refresh started");

    requestScreenFrame();
}

void MainWindow::scheduleInputRefresh()
{
    if (!inputRefreshTimer_ || !connected_) {
        return;
    }

    inputRefreshTimer_->start();
}

void MainWindow::setConnected(bool connected)
{
    if (!connected && autoRefreshTimer_ && autoRefreshTimer_->isActive()) {
        autoRefreshTimer_->stop();
        if (autoRefreshButton_) {
            autoRefreshButton_->setText("Auto Refresh");
        }
    }
    if (!connected) {
        controlConnected_ = false;
        screenConnected_ = false;
        screenBusy_ = false;
        pendingScreenFrame_ = false;
        lastFrameBytes_ = 0;
        lastFrameElapsedMs_ = 0;
        if (inputRefreshTimer_) {
            inputRefreshTimer_->stop();
        }
    }

    connected_ = connected;

    connectButton_->setEnabled(!connected && !busy_);
    disconnectButton_->setEnabled(connected && !busy_);
    listDrivesButton_->setEnabled(connected && !busy_);
    listDirButton_->setEnabled(connected && !busy_);
    downloadButton_->setEnabled(connected && !busy_);
    screenshotButton_->setEnabled(connected && !screenBusy_);
    autoRefreshButton_->setEnabled(connected);
    hostInput_->setEnabled(!connected);
    portInput_->setEnabled(!connected);
    qualityInput_->setEnabled(connected);
    scaleInput_->setEnabled(connected);
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
    screenshotButton_->setEnabled(connected_ && !screenBusy_);
    autoRefreshButton_->setEnabled(connected_);

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

    const qint64 elapsedMs = screenshotRequestTimer_.isValid() ? screenshotRequestTimer_.elapsed() : 0;
    lastFrameBytes_ = imageData.size();
    lastFrameElapsedMs_ = elapsedMs;
    updateFrameStats(imageData.size(), elapsedMs);
    updateScreenStreamStatus();

    QStringList results;
    results.push_back("Screenshot loaded");
    results.push_back(imageFormat + ", " + QString::number(imageData.size()) + " bytes");
    results.push_back("Remote screen: " + QString::number(remoteScreenWidth_) + " x " + QString::number(remoteScreenHeight_));
    showResults(results);
}

void MainWindow::updateScreenshotView()
{
    if (!screenshotLabel_ || currentScreenshot_.isNull()) {
        return;
    }

    screenshotLabel_->setPixmap(currentScreenshot_.scaled(
        screenshotLabel_->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
}

void MainWindow::updateScreenStreamStatus()
{
    if (!streamStateLabel_) {
        return;
    }

    QString state = "idle";
    if (!connected_) {
        state = "disconnected";
    } else if (screenBusy_) {
        state = "pulling frame";
    } else if (pendingScreenFrame_) {
        state = "pending";
    }

    QString text = "Screen stream: " + state;
    text += ", pending=" + QString(pendingScreenFrame_ ? "yes" : "no");
    text += ", quality=" + QString::number(qualityInput_ ? qualityInput_->value() : 0);
    text += ", scale=" + QString::number(scaleInput_ ? scaleInput_->value() : 0) + "%";
    text += ", interval=" + QString::number(refreshIntervalInput_ ? refreshIntervalInput_->value() : 0) + "ms";

    if (lastFrameBytes_ > 0) {
        text += ", last=" + QString::number(lastFrameBytes_ / 1024.0, 'f', 1) + "KB";
        if (lastFrameElapsedMs_ > 0) {
            text += "/" + QString::number(lastFrameElapsedMs_) + "ms";
        }
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
    QString text = "Frame: " + QString::number(kb, 'f', 1) + " KB";
    if (elapsedMs > 0) {
        text += ", " + QString::number(elapsedMs) + " ms";
    }
    if (fps > 0.0) {
        text += ", " + QString::number(fps, 'f', 1) + " FPS";
    }

    frameStatsLabel_->setText(text);
}

bool MainWindow::mapScreenshotPoint(const QPoint& labelPoint, int& outRemoteX, int& outRemoteY) const
{
    if (currentScreenshot_.isNull()) {
        return false;
    }

    const QSize labelSize = screenshotLabel_->size();
    const QSize imageSize = currentScreenshot_.size();
    const QSize scaledSize = imageSize.scaled(labelSize, Qt::KeepAspectRatio);

    const int offsetX = (labelSize.width() - scaledSize.width()) / 2;
    const int offsetY = (labelSize.height() - scaledSize.height()) / 2;
    const int imageX = labelPoint.x() - offsetX;
    const int imageY = labelPoint.y() - offsetY;

    if (imageX < 0 || imageY < 0 || imageX >= scaledSize.width() || imageY >= scaledSize.height()) {
        return false;
    }

    const int remoteWidth = remoteScreenWidth_ > 0 ? remoteScreenWidth_ : imageSize.width();
    const int remoteHeight = remoteScreenHeight_ > 0 ? remoteScreenHeight_ : imageSize.height();

    outRemoteX = imageX * remoteWidth / scaledSize.width();
    outRemoteY = imageY * remoteHeight / scaledSize.height();
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
