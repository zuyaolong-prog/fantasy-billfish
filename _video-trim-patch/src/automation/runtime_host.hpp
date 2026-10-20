#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <functional>

class QLocalServer;
class QLocalSocket;

namespace billfish::automation {

class AppControl;
class RuntimeHost final : public QObject {
    Q_OBJECT
public:
    explicit RuntimeHost(QObject* pluginInstance, QObject* parent = nullptr);
    ~RuntimeHost() override;
    static RuntimeHost* instance();
    bool start();
    void setInfoProvider(std::function<QJsonObject()> provider);
    void refreshObjects();
    QList<QObject*> observedObjects() const;
    QObject* findObjectByClass(const QByteArray& className);

public slots:
    void replySuccess(const QString& requestId, const QJsonValue& data);
    void replyError(const QString& requestId, const QString& code, const QString& message,
                    const QJsonObject& details = QJsonObject());

signals:
    void requestReceived(const QString& requestId, const QJsonObject& request);

private:
    void acceptClients();
    void consume(QLocalSocket* socket);
    void dispatch(QLocalSocket* socket, const QByteArray& line);
    void sendReply(QLocalSocket* socket, const QJsonObject& reply);
    QJsonObject objectMetadata(const QObject* object, bool includeValues = false) const;
    void removeDescriptor();

    QLocalServer* server_ = nullptr;
    QPointer<QObject> pluginInstance_;
    QList<QPointer<QObject>> objects_;
    QHash<QLocalSocket*, QByteArray> buffers_;
    QHash<QString, QPointer<QLocalSocket>> pending_;
    QString descriptorPath_;
    QString pipeName_;
    std::function<QJsonObject()> infoProvider_;
    AppControl* appControl_ = nullptr;
};

void installAutomationInjection(QObject* pluginInstance);

} // namespace billfish::automation
