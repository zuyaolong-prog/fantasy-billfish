#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>

class QWidget;

namespace billfish::automation {

// Owns the user-confirmed video acquisition workflow. The host supplies media
// transfer and native import, so files remain leased until import completes.
class VideoCaptureController final : public QObject {
    Q_OBJECT
public:
    explicit VideoCaptureController(QObject* parent = nullptr);
    ~VideoCaptureController() override;
    void setMainWindow(QWidget* window);
    void start(const QString& id, const QJsonObject& args, const QString& libraryRoot);
    bool has(const QString& id) const;
    void libraryChanged(const QString& libraryRoot);
    void shutdown();

public slots:
    void transferFinished(QString id, QJsonObject data);
    void transferFailed(QString id, QString code, QString message, QJsonObject details = QJsonObject());
    void transferProgress(QString id, int completed, int total, QString message);
    void finishImport(QString id, QJsonObject data);
    void failImport(QString id, QString code, QString message, QJsonObject details = QJsonObject());

signals:
    void transferRequested(QString id, QJsonObject request);
    void transferCancelRequested(QString id);
    void progress(QString id, int completed, int total, QString message);
    void importRequested(QString id, QString path, QString name, QString origin, QString libraryRoot);
    void finished(QString id, QJsonObject data);
    void failed(QString id, QString code, QString message, QJsonObject details);

private:
    struct Capture;
    void confirm(const QString& id);
    void cancel(const QString& id, const QString& code, const QString& message);
    void finish(const QString& id, bool success, QJsonObject data,
                const QString& code = QString(), const QString& message = QString(),
                QJsonObject details = QJsonObject());
    QPointer<QWidget> mainWindow_;
    QHash<QString, QSharedPointer<Capture>> captures_;
    bool stopping_ = false;
};

} // namespace billfish::automation
