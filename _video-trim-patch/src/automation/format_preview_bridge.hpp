#pragma once

#include <QObject>
#include <QJsonObject>
#include <memory>

namespace billfish::automation {
class NativeAdapter;
class PluginManager;
class FormatPreviewBridgePrivate;

// Routes authenticated native material events to enabled format extensions and
// publishes generated PNGs through the native custom-cover action.
class FormatPreviewBridge final : public QObject {
    Q_OBJECT
public:
    FormatPreviewBridge(NativeAdapter* native, PluginManager* plugins, QObject* parent = nullptr);
    ~FormatPreviewBridge() override;
    void request(const QString& id, const QString& kind, const QString& itemId,
                 const QString& pluginId, const QString& libraryRoot, bool background = false, const QString& extension = {});
    void publishViewerCover(const QString& id, const QJsonObject& capture, const QString& libraryRoot);
    void regenerateThumbnail(const QString& id, const QJsonObject& capture, const QString& libraryRoot);
    void setAnimationVisibility(const QString& id, const QJsonObject& arguments, const QString& libraryRoot);
    void libraryChanged(const QString& root);
    void shutdown();

signals:
    void finished(QString id, QJsonObject data);
    void failed(QString id, QString code, QString message, QJsonObject details = {});
    void progress(QString id, int completed, int total, QString message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class FormatPreviewBridgePrivate;
    std::unique_ptr<FormatPreviewBridgePrivate> d_;
};
}
