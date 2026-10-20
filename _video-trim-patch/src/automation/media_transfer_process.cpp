#include "automation/media_transfer_process.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace billfish::automation {
namespace {
constexpr int maximumLineBytes = 16 * 1024 * 1024;
constexpr int maximumRequestBytes = 1024 * 1024;
}

MediaTransferProcess::MediaTransferProcess(QObject* parent)
    : QObject(parent), process_(new QProcess(this)), timeout_(new QTimer(this)) {
    timeout_->setSingleShot(true);
    timeout_->setTimerType(Qt::PreciseTimer);
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, &MediaTransferProcess::readOutput);
    connect(process_, &QProcess::readyReadStandardError, this, [this] { process_->readAllStandardError(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (active_ && error == QProcess::FailedToStart) {
            if (stopCode_.isEmpty()) {
                stopCode_ = QStringLiteral("media_transfer_start_failed");
                stopMessage_ = process_->errorString();
            }
            settle(-1, false);
        }
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) { settle(exitCode, status == QProcess::NormalExit); });
    connect(timeout_, &QTimer::timeout, this, [this] {
        stop(QStringLiteral("media_transfer_timeout"), QStringLiteral("Media transfer exceeded its deadline."));
    });
}

MediaTransferProcess::~MediaTransferProcess() {
    if (process_->state() != QProcess::NotRunning) process_->kill();
}

bool MediaTransferProcess::start(const QJsonObject& request, int timeoutMs) {
    if (active_) return false;
    buffer_.clear(); terminal_ = QJsonObject(); stopCode_.clear(); stopMessage_.clear(); terminalSeen_ = false;
    active_ = true;
    const QByteArray input = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (timeoutMs <= 0 || input.size() > maximumRequestBytes) {
        stopCode_ = QStringLiteral("invalid_arguments");
        stopMessage_ = QStringLiteral("Media transfer requires a positive timeout and a request of at most 1 MiB.");
        QTimer::singleShot(0, this, [this] { settle(-1, false); });
        return true;
    }
    QString python = qEnvironmentVariable("BILLFISH_PYTHON");
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (python.isEmpty()) {
        stopCode_ = QStringLiteral("python_unavailable");
        stopMessage_ = QStringLiteral("Python is unavailable.");
        QTimer::singleShot(0, this, [this] { settle(-1, false); });
        return true;
    }
    disconnect(startedConnection_);
    startedConnection_ = connect(process_, &QProcess::started, this, [this, input] {
        disconnect(startedConnection_);
        process_->write(input);
        process_->closeWriteChannel();
    });
    const QString helper = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("automation/media_transfer.py"));
    process_->start(python, {QStringLiteral("-I"), helper});
    timeout_->start(timeoutMs);
    return true;
}

bool MediaTransferProcess::isRunning() const { return active_; }

void MediaTransferProcess::cancel() {
    stop(QStringLiteral("media_transfer_cancelled"), QStringLiteral("Media transfer was cancelled."));
}

void MediaTransferProcess::stop(const QString& code, const QString& message) {
    if (!active_ || !stopCode_.isEmpty()) return;
    stopCode_ = code; stopMessage_ = message;
    if (process_->state() == QProcess::NotRunning) settle(-1, false);
    else process_->kill();
}

void MediaTransferProcess::readOutput() {
    buffer_.append(process_->readAllStandardOutput());
    for (;;) {
        const int newline = buffer_.indexOf('\n');
        if (newline < 0) break;
        if (newline > maximumLineBytes) { stop(QStringLiteral("media_transfer_protocol_error"), QStringLiteral("Media helper line exceeds 16 MiB.")); return; }
        const QByteArray line = buffer_.left(newline);
        buffer_.remove(0, newline + 1);
        consumeLine(line);
        if (!stopCode_.isEmpty()) return;
    }
    if (buffer_.size() > maximumLineBytes)
        stop(QStringLiteral("media_transfer_protocol_error"), QStringLiteral("Media helper line exceeds 16 MiB."));
}

void MediaTransferProcess::consumeLine(const QByteArray& line) {
    if (line.trimmed().isEmpty()) return;
    if (terminalSeen_) { stop(QStringLiteral("media_transfer_protocol_error"), QStringLiteral("Media helper emitted output after its terminal event.")); return; }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    const QJsonObject event = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        stop(QStringLiteral("media_transfer_protocol_error"), QStringLiteral("Media helper emitted invalid JSON.")); return;
    }
    const QString kind = event.value(QStringLiteral("event")).toString();
    if (kind == QStringLiteral("progress")) { emit progress(event); return; }
    if (kind != QStringLiteral("result") || !event.value(QStringLiteral("ok")).isBool() ||
        (event.value(QStringLiteral("ok")).toBool() ? !event.value(QStringLiteral("result")).isObject() : !event.value(QStringLiteral("error")).isObject())) {
        stop(QStringLiteral("media_transfer_protocol_error"), QStringLiteral("Media helper emitted an invalid event.")); return;
    }
    terminalSeen_ = true;
    terminal_ = event;
}

void MediaTransferProcess::settle(int exitCode, bool normalExit) {
    if (!active_ || settling_) return;
    settling_ = true;
    timeout_->stop();
    disconnect(startedConnection_);
    readOutput();
    process_->readAllStandardError();
    if (!buffer_.trimmed().isEmpty() && stopCode_.isEmpty()) consumeLine(buffer_);
    buffer_.clear();
    const QString stoppedCode = stopCode_;
    const QString stoppedMessage = stopMessage_;
    const QJsonObject terminal = terminal_;
    const bool sawTerminal = terminalSeen_;
    active_ = false;
    settling_ = false;
    if (!stoppedCode.isEmpty()) {
        emit failed(stoppedCode, stoppedMessage, {{QStringLiteral("exit_code"), exitCode}}); return;
    }
    if (!sawTerminal || (!normalExit && terminal.value(QStringLiteral("ok")).toBool())) {
        emit failed(QStringLiteral("media_transfer_failed"), QStringLiteral("Media helper exited without a valid terminal result."),
            {{QStringLiteral("exit_code"), exitCode}}); return;
    }
    if (!terminal.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject error = terminal.value(QStringLiteral("error")).toObject();
        emit failed(error.value(QStringLiteral("code")).toString(QStringLiteral("media_transfer_failed")),
            error.value(QStringLiteral("message")).toString(QStringLiteral("Media transfer failed.")), error.value(QStringLiteral("details")).toObject());
    } else if (exitCode != 0) {
        emit failed(QStringLiteral("media_transfer_failed"), QStringLiteral("Media helper exited unsuccessfully."), {{QStringLiteral("exit_code"), exitCode}});
    } else emit finished(terminal.value(QStringLiteral("result")).toObject());
}

} // namespace billfish::automation
