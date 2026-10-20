#pragma once

#include <QJsonObject>
#include <QObject>

class QProcess;
class QTimer;

namespace billfish::automation {

// One helper request per process. A terminal signal is emitted only after the
// helper has exited and both output channels have been drained.
class MediaTransferProcess final : public QObject {
    Q_OBJECT
public:
    explicit MediaTransferProcess(QObject* parent = nullptr);
    ~MediaTransferProcess() override;
    bool start(const QJsonObject& request, int timeoutMs);
    bool isRunning() const;
    void cancel();

signals:
    void progress(const QJsonObject& event);
    void finished(const QJsonObject& result);
    void failed(const QString& code, const QString& message, const QJsonObject& details);

private:
    void readOutput();
    void consumeLine(const QByteArray& line);
    void stop(const QString& code, const QString& message);
    void settle(int exitCode, bool normalExit);

    QProcess* process_;
    QTimer* timeout_;
    QByteArray buffer_;
    QJsonObject terminal_;
    QString stopCode_;
    QString stopMessage_;
    bool active_ = false;
    bool terminalSeen_ = false;
    bool settling_ = false;
    QMetaObject::Connection startedConnection_;
};

} // namespace billfish::automation
