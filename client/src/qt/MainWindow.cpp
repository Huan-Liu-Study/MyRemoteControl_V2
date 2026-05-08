#include "qt/MainWindow.h"

#include <QFormLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "qt/RemoteClientWorker.h"

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
    resize(760, 360);

    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(12, 12, 12, 8);
    mainLayout->setSpacing(8);

    hostInput_ = new QLineEdit(this);
    hostInput_->setText("127.0.0.1");

    portInput_ = new QSpinBox(this);
    portInput_->setRange(1, 65535);
    portInput_->setValue(12345);

    pathInput_ = new QLineEdit(this);
    pathInput_->setPlaceholderText("C:\\");

    QFormLayout* formLayout = new QFormLayout;
    formLayout->addRow("Host", hostInput_);
    formLayout->addRow("Port", portInput_);
    formLayout->addRow("Path", pathInput_);

    connectButton_ = new QPushButton("Connect", this);
    disconnectButton_ = new QPushButton("Disconnect", this);
    listDrivesButton_ = new QPushButton("List Drives", this);
    listDirButton_ = new QPushButton("List Directory", this);
    downloadButton_ = new QPushButton("Download File", this);

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(connectButton_);
    buttonLayout->addWidget(disconnectButton_);
    buttonLayout->addWidget(listDrivesButton_);
    buttonLayout->addWidget(listDirButton_);
    buttonLayout->addWidget(downloadButton_);

    statusLabel_ = new QLabel("Disconnected", this);

    resultList_ = new QListWidget(this);
    resultList_->setMaximumHeight(120);

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumHeight(130);

    mainLayout->addLayout(formLayout);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(statusLabel_);
    mainLayout->addWidget(resultList_);
    mainLayout->addWidget(logView_);
    mainLayout->addStretch(1);

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
}

void MainWindow::startNetworkThread()
{
    networkThread_ = new QThread(this);
    worker_ = new RemoteClientWorker;
    worker_->moveToThread(networkThread_);

    connect(networkThread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(worker_, &RemoteClientWorker::connected, this, [this]() {
        setBusy(false);
        setConnected(true);
    });
    connect(worker_, &RemoteClientWorker::disconnected, this, [this](const QString& reason) {
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
    connect(worker_, &RemoteClientWorker::requestFinished, this, [this]() {
        setBusy(false);
    });

    networkThread_->start();
}

void MainWindow::stopNetworkThread()
{
    if (!networkThread_) {
        return;
    }

    if (worker_) {
        QMetaObject::invokeMethod(worker_, "disconnectFromServer", Qt::BlockingQueuedConnection);
    }

    networkThread_->quit();
    networkThread_->wait();
    networkThread_ = nullptr;
    worker_ = nullptr;
}

void MainWindow::connectToServer()
{
    if (!worker_ || busy_) {
        return;
    }

    setBusy(true);
    statusBar()->showMessage("Connecting");

    const QString host = hostInput_->text();
    const int port = portInput_->value();

    QMetaObject::invokeMethod(worker_, [worker = worker_, host, port]() {
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

void MainWindow::setConnected(bool connected)
{
    connected_ = connected;

    connectButton_->setEnabled(!connected && !busy_);
    disconnectButton_->setEnabled(connected && !busy_);
    listDrivesButton_->setEnabled(connected && !busy_);
    listDirButton_->setEnabled(connected && !busy_);
    downloadButton_->setEnabled(connected && !busy_);
    hostInput_->setEnabled(!connected);
    portInput_->setEnabled(!connected);
    pathInput_->setEnabled(connected);

    statusLabel_->setText(connected ? "Connected" : "Disconnected");
    statusBar()->showMessage(connected ? "Connected" : "Ready");
}

void MainWindow::setBusy(bool busy)
{
    busy_ = busy;

    connectButton_->setEnabled(!connected_ && !busy_);
    disconnectButton_->setEnabled(connected_ && !busy_);
    listDrivesButton_->setEnabled(connected_ && !busy_);
    listDirButton_->setEnabled(connected_ && !busy_);
    downloadButton_->setEnabled(connected_ && !busy_);

    if (!busy_) {
        statusBar()->showMessage(connected_ ? "Connected" : "Ready");
    }
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
