#include "ui/mpv_process_backend.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace billfish::trim {

namespace {

constexpr int kMaximumQueuedCommands = 128;
constexpr int kMaximumProtocolBufferBytes = 1024 * 1024;
constexpr int kMaximumCapturedProcessOutputBytes = 4096;
constexpr qint64 kUnknownTimeMs = -1;
constexpr int kPositionObservationId = 1;
constexpr int kDurationObservationId = 2;
constexpr int kPauseObservationId = 3;

QString executableCandidate(QString path) {
    path = path.trimmed();
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) &&
        path.endsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.size() - 2);
    }
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    if (!info.isFile()) {
        return {};
    }
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

double millisecondsToSeconds(const qint64 value) {
    return static_cast<double>(value) / 1000.0;
}

qint64 secondsToMilliseconds(const double value) {
    if (!std::isfinite(value) || value < 0.0) {
        return kUnknownTimeMs;
    }
    return qRound64(value * 1000.0);
}

QString processErrorText(const QProcess::ProcessError error) {
    switch (error) {
    case QProcess::FailedToStart:
        return QStringLiteral("无法启动 mpv");
    case QProcess::Crashed:
        return QStringLiteral("mpv 进程崩溃");
    case QProcess::Timedout:
        return QStringLiteral("mpv 进程操作超时");
    case QProcess::WriteError:
        return QStringLiteral("写入 mpv 进程失败");
    case QProcess::ReadError:
        return QStringLiteral("读取 mpv 进程失败");
    case QProcess::UnknownError:
        return QStringLiteral("mpv 进程发生未知错误");
    }
    return QStringLiteral("mpv 进程错误");
}

}  // namespace

MpvProcessBackend::MpvProcessBackend(QWidget* videoHost, QObject* parent)
    : QObject(parent), videoHost_(videoHost) {
    process_ = new QProcess(this);
    socket_ = new QLocalSocket(this);
    connectRetryTimer_ = new QTimer(this);
    startupTimer_ = new QTimer(this);
    shutdownTimer_ = new QTimer(this);

    connectRetryTimer_->setSingleShot(true);
    startupTimer_->setSingleShot(true);
    shutdownTimer_->setSingleShot(true);

    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::started, this, &MpvProcessBackend::scheduleIpcConnect);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        handleProcessError();
    });
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
                handleProcessFinished(exitCode, static_cast<int>(exitStatus));
            });
    connect(process_, &QProcess::readyRead, this, [this] {
        processOutput_.append(process_->readAll());
        if (processOutput_.size() > kMaximumCapturedProcessOutputBytes) {
            processOutput_ = processOutput_.right(kMaximumCapturedProcessOutputBytes);
        }
    });

    connect(socket_, &QLocalSocket::connected, this, &MpvProcessBackend::handleIpcConnected);
    connect(socket_, &QLocalSocket::disconnected, this,
            &MpvProcessBackend::handleIpcDisconnected);
    connect(socket_, &QLocalSocket::readyRead, this, &MpvProcessBackend::handleIpcReadyRead);
    connect(socket_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) { handleIpcError(); });

    connect(connectRetryTimer_, &QTimer::timeout, this, &MpvProcessBackend::attemptIpcConnect);
    connect(startupTimer_, &QTimer::timeout, this, &MpvProcessBackend::handleStartupTimeout);
    connect(shutdownTimer_, &QTimer::timeout, this, &MpvProcessBackend::handleShutdownTimeout);
}

MpvProcessBackend::~MpvProcessBackend() {
    forceStopForDestruction();
}

QString MpvProcessBackend::locateMpvExecutable(const QString& billfishRoot) {
    const QString environmentPath = qEnvironmentVariable("BILLFISH_TRIM_MPV");
    if (const QString candidate = executableCandidate(environmentPath); !candidate.isEmpty()) {
        return candidate;
    }

    const QString root = billfishRoot.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath()
        : QFileInfo(billfishRoot).absoluteFilePath();
    if (!root.isEmpty()) {
        const QString bundled = QDir(root).filePath(
            QStringLiteral("_hdr-preview-patch/tools/mpv/mpv.exe"));
        if (const QString candidate = executableCandidate(bundled); !candidate.isEmpty()) {
            return candidate;
        }
    }

    QString pathCandidate = QStandardPaths::findExecutable(QStringLiteral("mpv.exe"));
    if (pathCandidate.isEmpty()) {
        pathCandidate = QStandardPaths::findExecutable(QStringLiteral("mpv"));
    }
    return executableCandidate(pathCandidate);
}

QStringList MpvProcessBackend::defaultArguments(const quintptr windowId,
                                                const QString& ipcPipePath) {
    return {
        QStringLiteral("--no-config"),
        QStringLiteral("--idle=yes"),
        QStringLiteral("--force-window=yes"),
        QStringLiteral("--terminal=no"),
        QStringLiteral("--input-default-bindings=no"),
        QStringLiteral("--input-cursor=no"),
        QStringLiteral("--osc=no"),
        QStringLiteral("--border=no"),
        QStringLiteral("--show-in-taskbar=no"),
        QStringLiteral("--window-dragging=no"),
        QStringLiteral("--keep-open=yes"),
        QStringLiteral("--save-position-on-quit=no"),
        QStringLiteral("--wid=%1").arg(static_cast<qulonglong>(windowId)),
        QStringLiteral("--input-ipc-server=%1").arg(ipcPipePath),
        QStringLiteral("--vo=gpu-next"),
        QStringLiteral("--gpu-api=d3d11"),
        QStringLiteral("--hwdec=auto-safe"),
        QStringLiteral("--target-colorspace-hint=auto"),
        QStringLiteral("--target-trc=bt.1886"),
        QStringLiteral("--target-prim=bt.709"),
        QStringLiteral("--target-gamut=bt.709"),
        QStringLiteral("--tone-mapping=bt.2446a"),
        QStringLiteral("--gamut-mapping-mode=perceptual"),
        QStringLiteral("--hdr-compute-peak=auto")};
}

bool MpvProcessBackend::start(const QString& billfishRoot) {
    if (isRunning()) {
        reportError(QStringLiteral("mpv 已在运行"));
        return false;
    }
    if (videoHost_ == nullptr) {
        reportError(QStringLiteral("mpv 视频宿主窗口已销毁"));
        return false;
    }

    const QString resolvedRoot = billfishRoot.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath()
        : QFileInfo(billfishRoot).absoluteFilePath();
    mpvExecutable_ = locateMpvExecutable(resolvedRoot);
    if (mpvExecutable_.isEmpty()) {
        reportError(QStringLiteral(
            "未找到 mpv: 请设置 BILLFISH_TRIM_MPV，或部署 _hdr-preview-patch/tools/mpv/mpv.exe"));
        return false;
    }

    videoHost_->setAttribute(Qt::WA_NativeWindow, true);
    const quintptr windowId = static_cast<quintptr>(videoHost_->winId());
    if (windowId == 0) {
        reportError(QStringLiteral("无法创建 mpv 视频宿主 HWND"));
        return false;
    }

    resetSessionState();
    const QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces)
                              .remove(QLatin1Char('-'));
    ipcPipeName_ = QStringLiteral("billfish-trim-mpv-%1-%2")
                       .arg(QCoreApplication::applicationPid())
                       .arg(nonce);
    ipcPipePath_ = QStringLiteral("\\\\.\\pipe\\") + ipcPipeName_;

    process_->setProgram(mpvExecutable_);
    process_->setArguments(defaultArguments(windowId, ipcPipePath_));
    process_->setWorkingDirectory(QFileInfo(resolvedRoot).isDir()
                                      ? resolvedRoot
                                      : QFileInfo(mpvExecutable_).absolutePath());
    sessionActive_ = true;
    stoppedEmitted_ = false;
    process_->start(QIODevice::ReadOnly);
    startupTimer_->start(kStartupTimeoutMs);
    return true;
}

bool MpvProcessBackend::load(const QString& absolutePath) {
    const QFileInfo input(absolutePath);
    if (!input.isAbsolute() || !input.isFile()) {
        reportError(QStringLiteral("mpv 输入文件不存在或不是绝对路径: %1").arg(absolutePath));
        return false;
    }
    setPositionMs(kUnknownTimeMs);
    setDurationMs(kUnknownTimeMs);
    return queueCommand(QStringLiteral("loadfile"),
                        QJsonArray{QStringLiteral("loadfile"), input.absoluteFilePath(),
                                   QStringLiteral("replace")});
}

bool MpvProcessBackend::play() {
    return queueCommand(QStringLiteral("play"),
                        QJsonArray{QStringLiteral("set_property"), QStringLiteral("pause"), false});
}

bool MpvProcessBackend::pause() {
    return queueCommand(QStringLiteral("pause"),
                        QJsonArray{QStringLiteral("set_property"), QStringLiteral("pause"), true});
}

bool MpvProcessBackend::seek(const qint64 positionMs) {
    if (positionMs < 0) {
        reportError(QStringLiteral("mpv seek 位置不能为负数"));
        return false;
    }
    return queueCommand(QStringLiteral("seek"),
                        QJsonArray{QStringLiteral("seek"), millisecondsToSeconds(positionMs),
                                   QStringLiteral("absolute+exact")});
}

bool MpvProcessBackend::setAbLoop(const qint64 startMs, const qint64 endMs) {
    if (startMs < 0 || endMs <= startMs) {
        reportError(QStringLiteral("mpv A/B 循环范围无效: %1-%2 ms").arg(startMs).arg(endMs));
        return false;
    }
    const bool startQueued = queueCommand(
        QStringLiteral("set ab-loop-a"),
        QJsonArray{QStringLiteral("set_property"), QStringLiteral("ab-loop-a"),
                   millisecondsToSeconds(startMs)});
    const bool endQueued = queueCommand(
        QStringLiteral("set ab-loop-b"),
        QJsonArray{QStringLiteral("set_property"), QStringLiteral("ab-loop-b"),
                   millisecondsToSeconds(endMs)});
    return startQueued && endQueued;
}

bool MpvProcessBackend::clearAbLoop() {
    const bool startQueued = queueCommand(
        QStringLiteral("clear ab-loop-a"),
        QJsonArray{QStringLiteral("set_property"), QStringLiteral("ab-loop-a"),
                   QStringLiteral("no")});
    const bool endQueued = queueCommand(
        QStringLiteral("clear ab-loop-b"),
        QJsonArray{QStringLiteral("set_property"), QStringLiteral("ab-loop-b"),
                   QStringLiteral("no")});
    return startQueued && endQueued;
}

void MpvProcessBackend::shutdown() {
    if (!sessionActive_ || process_->state() == QProcess::NotRunning) {
        ready_ = false;
        stopping_ = false;
        socket_->abort();
        emitStoppedOnce();
        return;
    }
    if (stopping_) {
        return;
    }

    stopping_ = true;
    ready_ = false;
    connectRetryTimer_->stop();
    startupTimer_->stop();
    shutdownPhase_ = 0;
    if (socket_->state() == QLocalSocket::ConnectedState) {
        (void)writeCommand(QStringLiteral("quit"), QJsonArray{QStringLiteral("quit")});
        (void)socket_->flush();
    }
    shutdownTimer_->start(kGracefulShutdownTimeoutMs);
}

bool MpvProcessBackend::isReady() const noexcept {
    return ready_;
}

bool MpvProcessBackend::isRunning() const noexcept {
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}

qint64 MpvProcessBackend::positionMs() const noexcept {
    return positionMs_;
}

qint64 MpvProcessBackend::durationMs() const noexcept {
    return durationMs_;
}

bool MpvProcessBackend::isPaused() const noexcept {
    return paused_;
}

QString MpvProcessBackend::lastError() const {
    return lastError_;
}

QString MpvProcessBackend::mpvExecutable() const {
    return mpvExecutable_;
}

QString MpvProcessBackend::ipcPipeName() const {
    return ipcPipeName_;
}

bool MpvProcessBackend::queueCommand(const QString& description, const QJsonArray& command) {
    if (!sessionActive_ || process_->state() == QProcess::NotRunning || stopping_) {
        reportError(QStringLiteral("mpv 尚未运行，无法执行 %1").arg(description));
        return false;
    }
    if (ready_ && socket_->state() == QLocalSocket::ConnectedState) {
        return writeCommand(description, command);
    }
    if (queuedCommands_.size() >= kMaximumQueuedCommands) {
        reportError(QStringLiteral("mpv 启动期间命令队列已满"));
        return false;
    }
    queuedCommands_.append(qMakePair(description, command));
    return true;
}

bool MpvProcessBackend::writeCommand(const QString& description, const QJsonArray& command) {
    if (socket_->state() != QLocalSocket::ConnectedState) {
        reportError(QStringLiteral("mpv IPC 未连接，无法执行 %1").arg(description));
        return false;
    }
    const quint64 requestId = nextRequestId_++;
    QJsonObject request;
    request.insert(QStringLiteral("command"), command);
    request.insert(QStringLiteral("request_id"), static_cast<double>(requestId));
    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    payload.append('\n');
    pendingRequests_.insert(requestId, description);
    const qint64 written = socket_->write(payload);
    if (written != payload.size()) {
        pendingRequests_.remove(requestId);
        reportError(QStringLiteral("写入 mpv IPC 失败: %1").arg(description));
        return false;
    }
    return true;
}

void MpvProcessBackend::writePayload(const QByteArray& payload) {
    if (socket_->state() == QLocalSocket::ConnectedState && socket_->write(payload) < 0) {
        reportError(QStringLiteral("写入 mpv IPC 失败"));
    }
}

void MpvProcessBackend::flushQueuedCommands() {
    const auto commands = queuedCommands_;
    queuedCommands_.clear();
    for (const auto& queued : commands) {
        if (!writeCommand(queued.first, queued.second)) {
            break;
        }
    }
}

void MpvProcessBackend::installPropertyObservers() {
    (void)writeCommand(
        QStringLiteral("observe time-pos"),
        QJsonArray{QStringLiteral("observe_property"), kPositionObservationId,
                   QStringLiteral("time-pos")});
    (void)writeCommand(
        QStringLiteral("observe duration"),
        QJsonArray{QStringLiteral("observe_property"), kDurationObservationId,
                   QStringLiteral("duration")});
    (void)writeCommand(
        QStringLiteral("observe pause"),
        QJsonArray{QStringLiteral("observe_property"), kPauseObservationId,
                   QStringLiteral("pause")});
}

void MpvProcessBackend::scheduleIpcConnect() {
    if (!stopping_) {
        connectRetryTimer_->start(0);
    }
}

void MpvProcessBackend::attemptIpcConnect() {
    if (stopping_ || process_->state() == QProcess::NotRunning || ready_) {
        return;
    }
    socket_->abort();
    socket_->connectToServer(ipcPipeName_, QIODevice::ReadWrite);
}

void MpvProcessBackend::handleIpcConnected() {
    if (stopping_) {
        socket_->abort();
        return;
    }
    startupTimer_->stop();
    connectRetryTimer_->stop();
    ready_ = true;
    installPropertyObservers();
    flushQueuedCommands();
    emit ready();
}

void MpvProcessBackend::handleIpcDisconnected() {
    const bool wasReady = ready_;
    ready_ = false;
    if (stopping_ || terminalProcessErrorReported_ || !sessionActive_ ||
        process_->state() == QProcess::NotRunning) {
        return;
    }
    if (wasReady) {
        reportError(QStringLiteral("mpv IPC 连接意外断开"));
        shutdown();
        return;
    }
    connectRetryTimer_->start(kConnectRetryMs);
}

void MpvProcessBackend::handleIpcError() {
    if (stopping_ || process_->state() == QProcess::NotRunning || ready_) {
        return;
    }
    socket_->abort();
    connectRetryTimer_->start(kConnectRetryMs);
}

void MpvProcessBackend::handleIpcReadyRead() {
    protocolBuffer_.append(socket_->readAll());
    if (protocolBuffer_.size() > kMaximumProtocolBufferBytes) {
        protocolBuffer_.clear();
        reportError(QStringLiteral("mpv IPC 消息超过大小限制"));
        shutdown();
        return;
    }

    qsizetype newline = -1;
    while ((newline = protocolBuffer_.indexOf('\n')) >= 0) {
        QByteArray line = protocolBuffer_.left(newline);
        protocolBuffer_.remove(0, newline + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (!line.trimmed().isEmpty()) {
            handleProtocolLine(line);
        }
    }
}

void MpvProcessBackend::handleProtocolLine(const QByteArray& line) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        reportError(QStringLiteral("mpv IPC 返回无效 JSON: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject object = document.object();
    const QJsonValue requestValue = object.value(QStringLiteral("request_id"));
    if (requestValue.isDouble()) {
        const quint64 requestId = static_cast<quint64>(requestValue.toDouble());
        const QString description = pendingRequests_.take(requestId);
        const QString result = object.value(QStringLiteral("error")).toString();
        if (!result.isEmpty() && result != QStringLiteral("success")) {
            reportError(QStringLiteral("mpv 命令 %1 失败: %2")
                            .arg(description.isEmpty() ? QString::number(requestId) : description,
                                 result));
        }
    }

    const QString event = object.value(QStringLiteral("event")).toString();
    if (event == QStringLiteral("file-loaded")) {
        emit fileLoaded();
        return;
    }
    if (event == QStringLiteral("property-change")) {
        const QString name = object.value(QStringLiteral("name")).toString();
        const QJsonValue data = object.value(QStringLiteral("data"));
        if (name == QStringLiteral("time-pos") && (data.isDouble() || data.isNull())) {
            setPositionMs(data.isDouble() ? secondsToMilliseconds(data.toDouble())
                                          : kUnknownTimeMs);
        } else if (name == QStringLiteral("duration") &&
                   (data.isDouble() || data.isNull())) {
            setDurationMs(data.isDouble() ? secondsToMilliseconds(data.toDouble())
                                          : kUnknownTimeMs);
        } else if (name == QStringLiteral("pause") && data.isBool()) {
            setPaused(data.toBool());
        }
        return;
    }
    if (event == QStringLiteral("end-file") &&
        object.value(QStringLiteral("reason")).toString() == QStringLiteral("error")) {
        reportError(QStringLiteral("mpv 播放失败: %1")
                        .arg(object.value(QStringLiteral("file_error")).toString(
                            QStringLiteral("unknown"))));
    }
}

void MpvProcessBackend::handleProcessError() {
    if (!sessionActive_ || stopping_) {
        return;
    }
    const QProcess::ProcessError processError = process_->error();
    terminalProcessErrorReported_ = processError == QProcess::FailedToStart ||
                                    processError == QProcess::Crashed;
    QString error = processErrorText(processError);
    if (!process_->errorString().isEmpty()) {
        error += QStringLiteral(": ") + process_->errorString();
    }
    reportError(error);
    if (process_->state() == QProcess::NotRunning) {
        startupTimer_->stop();
        connectRetryTimer_->stop();
        emitStoppedOnce();
    }
}

void MpvProcessBackend::handleProcessFinished(const int exitCode, const int exitStatusValue) {
    const bool expected = stopping_;
    ready_ = false;
    stopping_ = false;
    shutdownPhase_ = 0;
    startupTimer_->stop();
    connectRetryTimer_->stop();
    shutdownTimer_->stop();
    socket_->abort();
    processOutput_.append(process_->readAll());
    if (processOutput_.size() > kMaximumCapturedProcessOutputBytes) {
        processOutput_ = processOutput_.right(kMaximumCapturedProcessOutputBytes);
    }
    if (!expected && sessionActive_ && !terminalProcessErrorReported_) {
        QString error = exitStatusValue == static_cast<int>(QProcess::CrashExit)
            ? QStringLiteral("mpv 进程崩溃")
            : QStringLiteral("mpv 进程意外退出，exitCode=%1").arg(exitCode);
        if (!processOutput_.trimmed().isEmpty()) {
            error += QStringLiteral(": ") + QString::fromLocal8Bit(processOutput_.trimmed());
        }
        reportError(error);
    }
    emitStoppedOnce();
}

void MpvProcessBackend::handleStartupTimeout() {
    if (ready_ || !sessionActive_ || stopping_) {
        return;
    }
    reportError(QStringLiteral("等待 mpv IPC 就绪超时"));
    stopping_ = true;
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
    } else {
        emitStoppedOnce();
    }
}

void MpvProcessBackend::handleShutdownTimeout() {
    if (process_->state() == QProcess::NotRunning) {
        emitStoppedOnce();
        return;
    }
    if (shutdownPhase_ == 0) {
        shutdownPhase_ = 1;
        process_->terminate();
        shutdownTimer_->start(kTerminateTimeoutMs);
        return;
    }
    process_->kill();
}

void MpvProcessBackend::setPositionMs(const qint64 value) {
    const qint64 normalized = value < 0 ? kUnknownTimeMs : value;
    if (normalized == positionMs_) {
        return;
    }
    positionMs_ = normalized;
    emit positionChanged(positionMs_);
}

void MpvProcessBackend::setDurationMs(const qint64 value) {
    const qint64 normalized = value < 0 ? kUnknownTimeMs : value;
    if (normalized == durationMs_) {
        return;
    }
    durationMs_ = normalized;
    emit durationChanged(durationMs_);
}

void MpvProcessBackend::setPaused(const bool value) {
    if (paused_ == value) {
        return;
    }
    paused_ = value;
    emit pausedChanged(paused_);
}

void MpvProcessBackend::reportError(const QString& error) {
    const QString normalized = error.trimmed();
    if (normalized.isEmpty()) {
        return;
    }
    lastError_ = normalized;
    emit errorOccurred(lastError_);
}

void MpvProcessBackend::resetSessionState() {
    socket_->abort();
    connectRetryTimer_->stop();
    startupTimer_->stop();
    shutdownTimer_->stop();
    protocolBuffer_.clear();
    processOutput_.clear();
    queuedCommands_.clear();
    pendingRequests_.clear();
    ipcPipeName_.clear();
    ipcPipePath_.clear();
    lastError_.clear();
    positionMs_ = kUnknownTimeMs;
    durationMs_ = kUnknownTimeMs;
    nextRequestId_ = 1;
    paused_ = true;
    ready_ = false;
    sessionActive_ = false;
    stoppedEmitted_ = true;
    stopping_ = false;
    terminalProcessErrorReported_ = false;
    shutdownPhase_ = 0;
}

void MpvProcessBackend::emitStoppedOnce() {
    if (!sessionActive_ || stoppedEmitted_) {
        return;
    }
    sessionActive_ = false;
    stoppedEmitted_ = true;
    emit stopped();
}

void MpvProcessBackend::forceStopForDestruction() {
    connectRetryTimer_->stop();
    startupTimer_->stop();
    shutdownTimer_->stop();
    if (process_->state() == QProcess::NotRunning) {
        return;
    }
    stopping_ = true;
    if (socket_->state() == QLocalSocket::ConnectedState) {
        QJsonObject request;
        request.insert(QStringLiteral("command"), QJsonArray{QStringLiteral("quit")});
        QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
        payload.append('\n');
        writePayload(payload);
        socket_->flush();
    }
    if (process_->waitForFinished(250)) {
        return;
    }
    process_->terminate();
    if (process_->waitForFinished(250)) {
        return;
    }
    process_->kill();
    (void)process_->waitForFinished(500);
}

}  // namespace billfish::trim
