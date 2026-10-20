#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <functional>

class QProcess;

namespace billfish::automation {

// Trusted local JavaScript plugins run in an independent Electron process.
class PluginManager final : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(QString installRoot, QObject* parent = nullptr);
    ~PluginManager() override;
    QJsonObject listPlugins() const;
    void setWindowOwnerProvider(std::function<quintptr()> provider);
    void installPluginAsync(const QString& requestId, const QString& sourcePath);
    QJsonObject setEnabled(const QString& id, bool enabled);
    void setLibraryRoot(const QString& libraryRoot);
    void run(const QString& requestId, const QString& pluginId, const QString& command,
             const QJsonObject& args, const QString& libraryRoot);
    QString previewPlugin(const QString& path, const QString& kind, const QString& requestedId = {}) const;
    QString previewStagingRoot(const QString& pluginId) const;
    bool previewEnabled(const QString& pluginId) const;
    QJsonArray previewDeclarations() const;
    void setNativeFormatStatus(const QString& pluginId, const QJsonObject& status);
    void preview(const QString& requestId, const QString& pluginId, const QString& kind,
                 const QJsonObject& item, const QString& libraryRoot, bool background = false, const QString& viewerIntent = {});
    void admitViewerIntent(const QString& intentId, const QJsonObject& feedback = {});
    void failViewerIntent(const QString& intentId, const QString& code, const QString& message);
    void cancelViewerPreview(const QString& requestId);
    void completeHostCall(const QString& id, const QJsonObject& data);
    void failHostCall(const QString& id, const QString& code, const QString& message, const QJsonObject& details);
    QString pluginsDirectory() const;
    void shutdown();

signals:
    void changed();
    void finished(QString requestId, QJsonObject data);
    void failed(QString requestId, QString code, QString message);
    void progress(QString requestId, int completed, int total, QString message);
    void hostCall(QString internalId, QString operation, QJsonObject arguments, QString libraryRoot);
    void builtinSessionStopped();

private:
    struct Plugin;
    struct HostRequest { Plugin* plugin; QString childId; QString operation; QString replyType; };
    QJsonObject describe(const Plugin* plugin) const;
    void start(Plugin* plugin);
    void stop(Plugin* plugin);
    void consume(Plugin* plugin);
    void receive(Plugin* plugin, const QJsonObject& message);
    void send(Plugin* plugin, const QJsonObject& message);
    void processStopped(Plugin* plugin, const QString& reason);
    bool saveState(QString* error);
    QJsonObject finalizeInstall(const QString& staging);
    void clearHostRequests(Plugin* plugin);
    void closeChannel(Plugin* plugin);
    QString installRoot_;
    QString directory_;
    QString statePath_;
    QString libraryRoot_;
    bool libraryInitialized_ = false;
    QHash<QString, Plugin*> plugins_;
    QHash<QString, HostRequest> hostRequests_;
    bool shuttingDown_ = false;
    QHash<QString, QProcess*> installers_;
    std::function<quintptr()> windowOwnerProvider_;
};

} // namespace billfish::automation
