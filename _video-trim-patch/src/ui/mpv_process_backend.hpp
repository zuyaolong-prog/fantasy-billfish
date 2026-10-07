#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>

class QLocalSocket;
class QProcess;
class QTimer;
class QWidget;

namespace billfish::trim {

class MpvProcessBackend final : public QObject {
    Q_OBJECT

public:
    static constexpr int kStartupTimeoutMs = 8'000;
    static constexpr int kConnectRetryMs = 50;
    static constexpr int kGracefulShutdownTimeoutMs = 1'500;
    static constexpr int kTerminateTimeoutMs = 500;

    explicit MpvProcessBackend(QWidget* videoHost, QObject* parent = nullptr);
    ~MpvProcessBackend() override;

    [[nodiscard]] static QString locateMpvExecutable(const QString& billfishRoot = {});
    [[nodiscard]] static QStringList defaultArguments(quintptr windowId,
                                                      const QString& ipcPipePath);

    bool start(const QString& billfishRoot = {});
    bool load(const QString& absolutePath);
    bool play();
    bool pause();
    bool seek(qint64 positionMs);
    bool setAbLoop(qint64 startMs, qint64 endMs);
    bool clearAbLoop();
    void shutdown();

    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] qint64 positionMs() const noexcept;
    [[nodiscard]] qint64 durationMs() const noexcept;
    [[nodiscard]] bool isPaused() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString mpvExecutable() const;
    [[nodiscard]] QString ipcPipeName() const;

signals:
    void ready();
    void fileLoaded();
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void pausedChanged(bool paused);
    void errorOccurred(const QString& error);
    void stopped();

private:
    bool queueCommand(const QString& description, const QJsonArray& command);
    bool writeCommand(const QString& description, const QJsonArray& command);
    void writePayload(const QByteArray& payload);
    void flushQueuedCommands();
    void installPropertyObservers();
    void scheduleIpcConnect();
    void attemptIpcConnect();
    void handleIpcConnected();
    void handleIpcDisconnected();
    void handleIpcError();
    void handleIpcReadyRead();
    void handleProtocolLine(const QByteArray& line);
    void handleProcessError();
    void handleProcessFinished(int exitCode, int exitStatusValue);
    void handleStartupTimeout();
    void handleShutdownTimeout();
    void setPositionMs(qint64 value);
    void setDurationMs(qint64 value);
    void setPaused(bool value);
    void reportError(const QString& error);
    void resetSessionState();
    void emitStoppedOnce();
    void forceStopForDestruction();

    QPointer<QWidget> videoHost_;
    QProcess* process_{nullptr};
    QLocalSocket* socket_{nullptr};
    QTimer* connectRetryTimer_{nullptr};
    QTimer* startupTimer_{nullptr};
    QTimer* shutdownTimer_{nullptr};
    QByteArray protocolBuffer_;
    QByteArray processOutput_;
    QList<QPair<QString, QJsonArray>> queuedCommands_;
    QHash<quint64, QString> pendingRequests_;
    QString mpvExecutable_;
    QString ipcPipeName_;
    QString ipcPipePath_;
    QString lastError_;
    qint64 positionMs_{-1};
    qint64 durationMs_{-1};
    quint64 nextRequestId_{1};
    bool paused_{true};
    bool ready_{false};
    bool sessionActive_{false};
    bool stoppedEmitted_{true};
    bool stopping_{false};
    bool terminalProcessErrorReported_{false};
    int shutdownPhase_{0};
};

}  // namespace billfish::trim
