#include "automation/format_preview_bridge.hpp"
#include "automation/app_input_scope.hpp"
#include "automation/native_adapter.hpp"
#include "automation/plugin_manager.hpp"
#include "automation/animation_visibility.hpp"
#include "automation/native_cover_geometry.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <functional>
#include <windows.h>

namespace billfish::automation {
namespace {
QString normalized(QString root) { return QDir::cleanPath(QDir::fromNativeSeparators(root)).toCaseFolded(); }
QString token(const char* prefix) { return QString::fromLatin1(prefix) + QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

class FormatPreviewBridgePrivate {
public:
    struct Work {
        QString id, kind, itemId, requestedPlugin, plugin, root, child, phase, sourcePath;
        bool background = false;
        bool automatic = false;
        quint64 viewerIntent = 0, viewerSession = 0;
        QPointer<QProcess> metadataHelper;
        QJsonObject intent, rendered, published, nativeResult, pose, sourceFingerprint, animationSource, feedback;
        QDeadlineTimer deadline{90000};
        QDeadlineTimer verification{5000};
    };
    struct PendingTypes {
        QString plugin, root;
        QByteArray identity;
        QJsonObject intent, nativeResult, syncResult;
        QPointer<QFileSystemWatcher> watcher;
        bool dirty = true, reading = false, verified = false, syncRequired = false, awaitingLibrarySync = false;
        int epoch = 0;
    };
    QHash<QString, QSharedPointer<PendingTypes>> pendingTypes;
    FormatPreviewBridge* q;
    NativeAdapter* native;
    PluginManager* plugins;
    AnimationVisibility* visibility;
    NativeCoverGeometry* coverGeometry;
    quint64 visibilityGeneration = 0;
    QHash<QString, QString> visibilityPaths;
    QSet<QString> visibilityIds;
    QJsonArray visibilityRequestedPaths;
    QPointer<QAbstractItemModel> visibilityModel;
    QPointer<QAbstractItemModel> visibilitySource;
    QPointer<QAbstractProxyModel> visibilityMappedProxy;
    QPointer<QAbstractItemModel> visibilityMappedSource;
    QByteArray visibilityMappedIdentity;
    bool visibilityDirty = false;
    QString visibilityRoot;
    QTimer* discovery;
    QTimer* pumpTimer;
    QPointer<QAbstractItemView> view;
    QPointer<QAbstractItemModel> model;
    QList<QMetaObject::Connection> modelConnections;
    QQueue<Work> queue;
    QHash<QString, Work> resolvingViewers;
    quint64 nextViewerIntent = 0, latestBuiltinViewerIntent = 0, builtinViewerSession = 0;
    std::unique_ptr<Work> active;
    QString root;
    QSet<QString> attempted;
    QHash<QString, QJsonObject> thumbnailErrors;
    QByteArray statusIdentity;
    QSet<QString> hydratedIds;
    bool scanQueued = false;
    bool scanRunning = false;
    bool stopped = false;
    int generation = 0;
    QString pluginSignature;
    QString typeSignature;

    FormatPreviewBridgePrivate(FormatPreviewBridge* object, NativeAdapter* adapter, PluginManager* manager)
        : q(object), native(adapter), plugins(manager) {
        visibility = new AnimationVisibility(native, q);
        coverGeometry = new NativeCoverGeometry(q);
        discovery = new QTimer(q); discovery->setInterval(750);
        QObject::connect(discovery, &QTimer::timeout, q, [this] { discover(); });
        discovery->start();
        QTimer::singleShot(0, q, [this] { discover(); });
        pumpTimer = new QTimer(q); pumpTimer->setInterval(150);
        QObject::connect(pumpTimer, &QTimer::timeout, q, [this] { pump(); });
    }

    ~FormatPreviewBridgePrivate() { coverGeometry->clear(); }

    void repaintPreviewStatus() {
        if (view && view->viewport()) view->viewport()->update();
    }
    void setPhase(const QString& phase) {
        if (!active || active->phase == phase) return;
        active->phase = phase; repaintPreviewStatus();
    }
    static bool coverWork(const Work& work) {
        return work.kind == QStringLiteral("thumbnail") || work.kind == QStringLiteral("cover") ||
            work.kind == QStringLiteral("regenerate");
    }
    QString previewCaption(const QModelIndex& index) {
        if (stopped || !view || index.model() != view->model()) return {};
        const auto item = native->itemForIndex(index);
        if (!item.value(QStringLiteral("native_unsupported_placeholder")).toBool()) return {};
        const auto id = item.value(QStringLiteral("item_id")).toString();
        const auto extension = item.value(QStringLiteral("extension")).toString();
        if (id.isEmpty() || plugins->previewPlugin(QStringLiteral("source") + extension, QStringLiteral("thumbnail")).isEmpty()) return {};
        if (active && coverWork(*active) && active->itemId == id) {
            if (active->phase == QStringLiteral("verifying")) return QStringLiteral("正在确认缩略图");
            if (active->phase == QStringLiteral("native") || active->phase == QStringLiteral("waiting_native") ||
                active->phase == QStringLiteral("publication_preflight")) return QStringLiteral("正在保存缩略图");
            return QStringLiteral("正在生成缩略图");
        }
        for (const auto& work : queue)
            if (coverWork(work) && work.itemId == id) return QStringLiteral("等待生成缩略图");
        if (thumbnailErrors.contains(id)) return QStringLiteral("缩略图生成失败");
        // A declared handler is not proof that this source has parsed or that a
        // job has been admitted. Keep this state distinct from queued/complete.
        return QStringLiteral("待生成缩略图");
    }
    void clearPreviewStatus() {
        coverGeometry->clearStatus(); statusIdentity.clear(); thumbnailErrors.clear();
        repaintPreviewStatus();
    }

    bool ownerMatches(const QString& expected) {
        const auto info = native->info();
        return !stopped && info.value(QStringLiteral("ready")).toBool() &&
            normalized(info.value(QStringLiteral("library_root")).toString()) == normalized(expected);
    }

    using Callback = std::function<void(const QJsonObject&, const QJsonObject&)>;
    void clearVisibility() {
        ++visibilityGeneration; visibility->clear(); visibilityPaths.clear(); visibilityIds.clear();
        visibilityRequestedPaths = {}; visibilityRoot.clear(); visibilityModel.clear(); visibilitySource.clear();
        visibilityMappedProxy.clear(); visibilityMappedSource.clear(); visibilityMappedIdentity.clear();
        visibilityDirty = false;
    }

    void visibilityError(const QString& id, const QString& code, const QString& message, const QJsonObject& details = {}) {
        q->setProperty("lastAnimationVisibilityError", QJsonDocument(QJsonObject{{QStringLiteral("code"), code},
            {QStringLiteral("message"), message}}).toJson(QJsonDocument::Compact));
        if (!id.isEmpty()) emit q->failed(id, code, message, details);
    }

    void updateVisibility(const QString& id, const QJsonArray& paths, const QString& library) {
        const auto visibilityEpoch = ++visibilityGeneration;
        visibilityDirty = false;
        visibilityRequestedPaths = paths; visibilityRoot = library;
        auto* current = native->currentMaterialView();
        auto* currentProxy = current ? qobject_cast<QAbstractProxyModel*>(current->model()) : nullptr;
        const auto identity = native->libraryIdentity();
        if ((!visibilityPaths.isEmpty() || !visibilityIds.isEmpty()) &&
            (!visibilityMappedProxy || !visibilityMappedSource || currentProxy != visibilityMappedProxy ||
             currentProxy->sourceModel() != visibilityMappedSource || identity != visibilityMappedIdentity)) {
            // Previously mapped IDs are trusted only in their original native
            // source/owner binding. A new source needs a fresh catalog read.
            visibility->clear(); visibilityPaths.clear(); visibilityIds.clear();
            visibilityMappedProxy.clear(); visibilityMappedSource.clear(); visibilityMappedIdentity.clear();
        }
        visibilityModel = current ? current->model() : nullptr;
        visibilitySource = currentProxy ? currentProxy->sourceModel() : nullptr;
        QSet<QString> requested;
        for (const auto& path : paths) requested.insert(normalized(path.toString()));
        // A watcher publishes a complete replacement. Remove invalidated paths
        // immediately, before any asynchronous catalog mapping or reparsing.
        auto retained = visibilityIds;
        for (auto it = visibilityPaths.begin(); it != visibilityPaths.end();) {
            if (!requested.contains(it.key())) { retained.remove(it.value()); it = visibilityPaths.erase(it); }
            else ++it;
        }
        if (retained != visibilityIds) {
            const auto result = visibility->replace(library, retained);
            if (result.value(QStringLiteral("error")).isObject()) {
                const auto error = result.value(QStringLiteral("error")).toObject(); clearVisibility();
                visibilityError(id, error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString()); return;
            }
            visibilityIds = retained;
        }
        if (paths.isEmpty()) {
            visibility->clear(); visibilityPaths.clear(); visibilityIds.clear(); visibilitySource.clear();
            visibilityMappedProxy.clear(); visibilityMappedSource.clear(); visibilityMappedIdentity.clear();
            if (!id.isEmpty()) emit q->finished(id, {{QStringLiteral("applied"), true}, {QStringLiteral("hidden_item_ids"), QJsonArray{}},
                {QStringLiteral("unresolved_paths"), QJsonArray{}}, {QStringLiteral("state"), QStringLiteral("visible")}});
            return;
        }
        if (!currentProxy || !currentProxy->sourceModel()) {
            visibility->clear(); visibilityPaths.clear(); visibilityIds.clear();
            visibilityError(id, QStringLiteral("native_visibility_unavailable"), QStringLiteral("The current native view has no authenticated material proxy.")); return;
        }
        const QPointer<QAbstractProxyModel> boundProxy = currentProxy;
        const QPointer<QAbstractItemModel> boundSource = currentProxy->sourceModel();
        helper(QStringLiteral("animation_visibility"), QStringLiteral("animation.visibility"),
            {{QStringLiteral("hidden_paths"), paths}}, library,
            [this, id, library, visibilityEpoch, requested, boundProxy, boundSource, identity](const QJsonObject& data, const QJsonObject& error) {
                if (visibilityEpoch != visibilityGeneration) {
                    if (!id.isEmpty()) emit q->failed(id, QStringLiteral("visibility_superseded"), QStringLiteral("A newer animation visibility snapshot replaced this request."), {});
                    return;
                }
                if (!ownerMatches(library) || !plugins->previewEnabled(QStringLiteral("billfish.animation-browser"))) {
                    clearVisibility(); visibilityError(id, QStringLiteral("library_changed"), QStringLiteral("The bound animation browser session or library stopped.")); return;
                }
                if (visibilityEpoch != visibilityGeneration) {
                    if (!id.isEmpty()) emit q->failed(id, QStringLiteral("visibility_superseded"), QStringLiteral("The native library context changed during visibility mapping."), {});
                    return;
                }
                auto* current = native->currentMaterialView();
                if (!boundProxy || !boundSource || !current || current->model() != boundProxy ||
                    boundProxy->sourceModel() != boundSource || native->libraryIdentity() != identity) {
                    // Keep the old observed source, so discovery schedules a
                    // fresh catalog mapping for the new native source.
                    visibility->clear(); visibilityPaths.clear(); visibilityIds.clear();
                    visibilityMappedProxy.clear(); visibilityMappedSource.clear(); visibilityMappedIdentity.clear();
                    visibilityError(id, QStringLiteral("visibility_context_changed"), QStringLiteral("The native material source changed while animation paths were being mapped.")); return;
                }
                if (!error.isEmpty()) {
                    clearVisibility(); visibilityError(id, error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString()); return;
                }
                QSet<QString> identifiers;
                const QJsonArray nativeIds = data.value(QLatin1String("hidden_item_ids")).toArray();
                for (const auto& item : nativeIds) identifiers.insert(item.toString());
                QHash<QString, QString> mappings;
                QSet<QString> mappedIds;
                const QJsonArray resolvedPaths = data.value(QLatin1String("resolved_paths")).toArray();
                for (const auto& value : resolvedPaths) {
                    const auto mapping = value.toObject(); const auto path = normalized(mapping.value(QStringLiteral("path")).toString());
                    const auto item = mapping.value(QStringLiteral("item_id")).toString();
                    if (!requested.contains(path) || !identifiers.contains(item)) {
                        clearVisibility(); visibilityError(id, QStringLiteral("invalid_visibility_mapping"), QStringLiteral("The catalog helper returned an unbound animation path or ID.")); return;
                    }
                    mappings.insert(path, item);
                    mappedIds.insert(item);
                }
                if (mappedIds != identifiers || normalized(data.value(QStringLiteral("library_root")).toString()) != normalized(library)) {
                    clearVisibility(); visibilityError(id, QStringLiteral("invalid_visibility_mapping"), QStringLiteral("The catalog helper returned an incomplete or foreign-library animation mapping.")); return;
                }
                const auto result = visibility->replace(library, identifiers);
                if (result.value(QStringLiteral("error")).isObject()) {
                    const auto failure = result.value(QStringLiteral("error")).toObject(); clearVisibility();
                    visibilityError(id, failure.value(QStringLiteral("code")).toString(), failure.value(QStringLiteral("message")).toString()); return;
                }
                visibilityPaths = mappings; visibilityIds = identifiers;
                visibilityMappedProxy = boundProxy; visibilityMappedSource = boundSource; visibilityMappedIdentity = identity;
                auto* view = native->currentMaterialView(); auto* proxy = view ? qobject_cast<QAbstractProxyModel*>(view->model()) : nullptr;
                visibilityModel = view ? view->model() : nullptr; visibilitySource = proxy ? proxy->sourceModel() : nullptr;
                auto reply = result.value(QStringLiteral("data")).toObject();
                q->setProperty("lastAnimationVisibilityError", QVariant{});
                reply.insert(QStringLiteral("unresolved_paths"), data.value(QStringLiteral("unresolved_paths")));
                if (!id.isEmpty()) emit q->finished(id, reply);
            });
    }
    QProcess* helper(const QString& mode, const QString& operation, const QJsonObject& arguments,
                const QString& library, Callback callback) {
        QString python = qEnvironmentVariable("BILLFISH_PYTHON");
        if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
        if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
        if (python.isEmpty()) {
            callback({}, {{QStringLiteral("code"), QStringLiteral("python_unavailable")}, {QStringLiteral("message"), QStringLiteral("Python is unavailable.")}}); return nullptr;
        }
        auto* process = new QProcess(q);
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
        const auto output = QSharedPointer<QByteArray>::create();
        const auto settled = QSharedPointer<bool>::create(false);
        auto* timer = new QTimer(process); timer->setSingleShot(true); timer->start(45000);
        QObject::connect(timer, &QTimer::timeout, q, [process] { process->kill(); });
        QObject::connect(process, &QProcess::readyReadStandardOutput, q, [process, output] {
            output->append(process->readAllStandardOutput());
            if (output->size() > 8 * 1024 * 1024) process->kill();
        });
        QObject::connect(process, &QProcess::readyReadStandardError, q, [process] { process->readAllStandardError(); });
        QObject::connect(process, &QProcess::errorOccurred, q, [process, settled, callback](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart || *settled) return;
            *settled = true;
            callback({}, {{QStringLiteral("code"), QStringLiteral("preview_helper_failed")}, {QStringLiteral("message"), process->errorString()}});
            process->deleteLater();
        });
        QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), q,
            [process, timer, output, settled, callback](int code, QProcess::ExitStatus status) {
                timer->stop(); output->append(process->readAllStandardOutput()); process->deleteLater();
                if (*settled) return; *settled = true;
                QJsonParseError parse;
                const auto document = QJsonDocument::fromJson(*output, &parse);
                if (status != QProcess::NormalExit || code != 0 || output->size() > 8 * 1024 * 1024 ||
                    parse.error != QJsonParseError::NoError || !document.isObject()) {
                    callback({}, {{QStringLiteral("code"), QStringLiteral("preview_helper_failed")},
                        {QStringLiteral("message"), QStringLiteral("Preview metadata helper did not return a valid result.")}}); return;
                }
                const auto response = document.object();
                callback(response.value(QStringLiteral("data")).toObject(), response.value(QStringLiteral("error")).toObject());
            });
        const auto input = QJsonDocument(QJsonObject{{QStringLiteral("operation"), operation}, {QStringLiteral("mode"), mode},
            {QStringLiteral("arguments"), arguments}, {QStringLiteral("library_root"), library}}).toJson(QJsonDocument::Compact);
        QObject::connect(process, &QProcess::started, q, [process, input] { process->write(input); process->closeWriteChannel(); });
        process->start(python, {QStringLiteral("-I"), QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("automation/billfish_host_helper.py"))});
        return process;
    }

    static bool builtinViewer(const Work& work) {
        return work.kind == QStringLiteral("viewer") &&
            (work.requestedPlugin == QStringLiteral("billfish.animation-browser") ||
             work.plugin == QStringLiteral("billfish.animation-browser"));
    }
    void cancelViewerWork(const Work& work, const QString& code, const QString& message, const QJsonObject& suppliedDetails = {}) {
        if (work.metadataHelper && work.metadataHelper->state() != QProcess::NotRunning) work.metadataHelper->kill();
        if (!work.child.isEmpty()) plugins->cancelViewerPreview(work.child);
        auto details = suppliedDetails;
        details.insert(QStringLiteral("item_id"), work.itemId);
        details.insert(QStringLiteral("kind"), work.kind);
        details.insert(QStringLiteral("phase"), work.phase);
        details.insert(QStringLiteral("library_root"), work.root);
        if (!work.automatic) emit q->failed(work.id, code, message, details);
    }
    void removeBuiltinViewers(const QString& code, const QString& message, quint64 beforeIntent = 0) {
        QList<Work> removed;
        for (int i = queue.size() - 1; i >= 0; --i)
            if (builtinViewer(queue.at(i)) && (!beforeIntent || queue.at(i).viewerIntent < beforeIntent))
                removed.prepend(queue.takeAt(i));
        if (active && builtinViewer(*active) && (!beforeIntent || active->viewerIntent < beforeIntent)) {
            removed.prepend(*active); active.reset();
        }
        for (const auto& work : removed) cancelViewerWork(work, code, message);
    }
    bool admitBuiltinViewer(const Work& work) {
        if (work.viewerSession != builtinViewerSession) {
            cancelViewerWork(work, QStringLiteral("plugin_disabled"), QStringLiteral("The built-in preview session stopped.")); return false;
        }
        if (work.viewerIntent < latestBuiltinViewerIntent) {
            cancelViewerWork(work, QStringLiteral("preview_superseded"), QStringLiteral("A newer model open request replaced this preview.")); return false;
        }
        if (work.viewerIntent > latestBuiltinViewerIntent) {
            latestBuiltinViewerIntent = work.viewerIntent;
            plugins->admitViewerIntent(work.id, work.feedback);
        }
        removeBuiltinViewers(QStringLiteral("preview_superseded"),
            QStringLiteral("A newer model open request replaced this preview."), work.viewerIntent);
        return true;
    }
    void enqueue(Work work) {
        if (builtinViewer(work) && !admitBuiltinViewer(work)) return;
        if (queue.size() + resolvingViewers.size() >= 256) {
            if (builtinViewer(work)) plugins->failViewerIntent(work.id, QStringLiteral("preview_busy"), QStringLiteral("The format preview queue is unavailable or full."));
            if (!work.automatic) emit q->failed(work.id, QStringLiteral("preview_busy"), QStringLiteral("The format preview queue is unavailable or full."),
                {{QStringLiteral("item_id"), work.itemId}, {QStringLiteral("kind"), work.kind}});
            return;
        }
        int position = 0;
        while (position < queue.size() && !queue.at(position).automatic) ++position;
        if (coverWork(work)) thumbnailErrors.remove(work.itemId);
        queue.insert(position, work); repaintPreviewStatus(); pump();
    }
    void resolveViewer(Work work) {
        if (queue.size() + resolvingViewers.size() >= 256) {
            emit q->failed(work.id, QStringLiteral("preview_busy"), QStringLiteral("The format preview queue is unavailable or full."),
                {{QStringLiteral("item_id"), work.itemId}, {QStringLiteral("kind"), work.kind}}); return;
        }
        const auto id = work.id;
        resolvingViewers.insert(id, work);
        auto* process = helper(QStringLiteral("preview_prepare"), QStringLiteral("plugins.preview"),
            {{QStringLiteral("item_id"), work.itemId}}, work.root,
            [this, id](const QJsonObject& data, const QJsonObject& error) {
                if (!resolvingViewers.contains(id)) return;
                auto work = resolvingViewers.take(id);
                if (stopped || !ownerMatches(work.root)) {
                    cancelViewerWork(work, QStringLiteral("library_changed"), QStringLiteral("The bound library changed during preview preparation.")); return;
                }
                if (!error.isEmpty()) {
                    cancelViewerWork(work, error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString(), error.value(QStringLiteral("details")).toObject()); return;
                }
                work.requestedPlugin = plugins->previewPlugin(data.value(QStringLiteral("item")).toObject().value(QStringLiteral("path")).toString(), QStringLiteral("viewer"));
                if (work.requestedPlugin.isEmpty()) {
                    cancelViewerWork(work, QStringLiteral("preview_not_supported"), QStringLiteral("No enabled preview plugin owns this format.")); return;
                }
                // The admission read resolves ownership only. The normal queue
                // still performs its fresh source/owner preflight before use.
                work.metadataHelper.clear(); enqueue(work);
            });
        if (resolvingViewers.contains(id)) resolvingViewers[id].metadataHelper = process;
    }
    void clearResolvingViewers(const QString& code, const QString& message) {
        const auto pending = resolvingViewers; resolvingViewers.clear();
        for (const auto& work : pending) cancelViewerWork(work, code, message);
    }

    void fail(const QString& code, const QString& message, const QJsonObject& details = {}) {
        if (!active) return;
        const auto work = *active; active.reset();
        if (builtinViewer(work)) {
            plugins->failViewerIntent(work.id, code, message);
            if (work.metadataHelper && work.metadataHelper->state() != QProcess::NotRunning) work.metadataHelper->kill();
            if (!work.child.isEmpty()) plugins->cancelViewerPreview(work.child);
        }
        if (coverWork(work) && code != QStringLiteral("library_changed") && code != QStringLiteral("plugin_disabled") &&
            code != QStringLiteral("application_quitting"))
            thumbnailErrors.insert(work.itemId, {{QStringLiteral("code"), code}, {QStringLiteral("message"), message}});
        repaintPreviewStatus();
        q->setProperty("lastPreviewError", QJsonDocument(QJsonObject{{QStringLiteral("code"), code},
            {QStringLiteral("message"), message}, {QStringLiteral("item_id"), work.itemId}}).toJson(QJsonDocument::Compact));
        if (work.kind == QStringLiteral("types")) {
            QJsonObject status{{QStringLiteral("state"), QStringLiteral("failed")}, {QStringLiteral("library_root"), work.root},
                {QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
            if (!details.isEmpty()) status.insert(code == QStringLiteral("native_types_failed") ?
                QStringLiteral("native_result") : QStringLiteral("details"), details);
            plugins->setNativeFormatStatus(work.plugin, status);
        }
        auto context = details;
        context.insert(QStringLiteral("item_id"), work.itemId);
        context.insert(QStringLiteral("kind"), work.kind);
        context.insert(QStringLiteral("phase"), work.phase);
        context.insert(QStringLiteral("library_root"), work.root);
        if (!work.automatic) emit q->failed(work.id, code, message, context);
        QTimer::singleShot(0, q, [this] { pump(); });
    }

    void finish(QJsonObject result) {
        if (!active) return;
        const auto work = *active; active.reset();
        if (coverWork(work)) thumbnailErrors.remove(work.itemId);
        repaintPreviewStatus();
        result.insert(QStringLiteral("item_id"), work.itemId);
        result.insert(QStringLiteral("plugin_id"), work.plugin);
        result.insert(QStringLiteral("library_root"), work.root);
        if (work.kind == QStringLiteral("types")) {
            result.insert(QStringLiteral("state"), QStringLiteral("ready"));
            plugins->setNativeFormatStatus(work.plugin, result);
        }
        if (!work.automatic) emit q->finished(work.id, result);
        QTimer::singleShot(0, q, [this] { pump(); });
    }

    void validateOwnerAndPlugin() {
        if (!active) return;
        if (!ownerMatches(active->root)) fail(QStringLiteral("library_changed"), QStringLiteral("The bound library closed or changed during preview."));
        else if (!active->plugin.isEmpty() && !plugins->previewEnabled(active->plugin))
            fail(QStringLiteral("plugin_disabled"), QStringLiteral("The preview plugin stopped. An already submitted native cover may have completed."));
        else if (active->deadline.hasExpired()) fail(QStringLiteral("preview_timeout"), QStringLiteral("The format preview exceeded its deadline."));
    }

    void pump() {
        if (stopped) return;
        validateOwnerAndPlugin();
        if (!active) {
            if (queue.isEmpty()) { pumpTimer->stop(); return; }
            active = std::make_unique<Work>(queue.dequeue());
            active->deadline = QDeadlineTimer(90000);
            if (!ownerMatches(active->root)) { fail(QStringLiteral("library_changed"), QStringLiteral("The requested library is no longer active.")); return; }
            if (!active->requestedPlugin.isEmpty() && !plugins->previewEnabled(active->requestedPlugin)) {
                fail(QStringLiteral("plugin_disabled"), QStringLiteral("The requested format plugin is no longer enabled and ready.")); return;
            }
            if (active->kind == QStringLiteral("types")) {
                setPhase(QStringLiteral("types_preparing"));
                readTypes(); pumpTimer->start(); return;
            }
            setPhase(QStringLiteral("preparing"));
            const auto id = active->id;
            QJsonObject prepare{{QStringLiteral("item_id"), active->itemId}};
            if (active->kind == QStringLiteral("cover") || active->kind == QStringLiteral("regenerate")) prepare.insert(QStringLiteral("expected_source"), active->sourceFingerprint);
            if (!active->animationSource.isEmpty()) prepare.insert(QStringLiteral("animation_source"), active->animationSource);
            auto* process = helper(QStringLiteral("preview_prepare"), active->kind == QStringLiteral("cover") ? QStringLiteral("plugins.set_cover") :
                    active->kind == QStringLiteral("regenerate") ? QStringLiteral("plugins.regenerate_thumbnail") :
                    active->kind == QStringLiteral("thumbnail") ? QStringLiteral("plugins.thumbnail") : QStringLiteral("plugins.preview"),
                prepare, active->root,
                [this, id](const QJsonObject& data, const QJsonObject& error) {
                    if (!active || active->id != id) return;
                    validateOwnerAndPlugin(); if (!active) return;
                    if (!error.isEmpty()) { fail(error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString(), error.value(QStringLiteral("details")).toObject()); return; }
                    active->intent = data;
                    if (data.value(QStringLiteral("skipped")).toBool()) {
                        finish({{QStringLiteral("completion"), QStringLiteral("existing_cover_preserved")}, {QStringLiteral("skipped"), true},
                            {QStringLiteral("cover_tid"), data.value(QStringLiteral("item")).toObject().value(QStringLiteral("cover_tid"))}}); return;
                    }
                    const auto actualPath = data.value(QStringLiteral("item")).toObject().value(QStringLiteral("path")).toString();
                    if ((active->kind == QStringLiteral("cover") || active->kind == QStringLiteral("regenerate")) && normalized(active->sourcePath) != normalized(actualPath)) {
                        fail(QStringLiteral("viewer_source_mismatch"), QStringLiteral("The viewer's original path no longer matches the bound native item.")); return;
                    }
                    active->plugin = plugins->previewPlugin(actualPath,
                        active->kind == QStringLiteral("cover") ? QStringLiteral("viewer") : active->kind == QStringLiteral("regenerate") ? QStringLiteral("thumbnail") : active->kind, active->requestedPlugin);
                    if (active->plugin.isEmpty()) { fail(QStringLiteral("preview_not_supported"), QStringLiteral("No enabled preview plugin owns this format.")); return; }
                    if (builtinViewer(*active) && !admitBuiltinViewer(*active)) {
                        active.reset(); QTimer::singleShot(0, q, [this] { pump(); }); return;
                    }
                    if (!active || active->id != id) return;
                    setPhase(active->kind == QStringLiteral("cover") ? QStringLiteral("waiting_native") : QStringLiteral("waiting_render"));
                    pump();
                });
            if (active && active->id == id) active->metadataHelper = process;
            pumpTimer->start(); return;
        }
        if (active->phase == QStringLiteral("waiting_type_native")) {
            if (native->info().value(QStringLiteral("busy")).toBool()) return;
            const auto readiness = native->fileTypeRegistrationReadiness(active->root);
            if (!readiness.value(QStringLiteral("ready")).toBool()) {
                if (readiness.value(QStringLiteral("code")).toString() == QStringLiteral("native_library_sync_required")) {
                    detachTypePersistence(true); return;
                }
                fail(readiness.value(QStringLiteral("code")).toString(), QStringLiteral("The native library synchronization prerequisite could not be verified."), readiness); return;
            }
            QStringList missing;
            const auto names = active->intent.value(QStringLiteral("missing")).toArray();
            for (const auto& name : names) missing.append(name.toString());
            setPhase(QStringLiteral("types_native")); active->child = token("format-types-action-");
            native->addFileTypes(active->child, active->root, missing);
        } else if (active->phase == QStringLiteral("waiting_render")) {
            const auto entries = plugins->listPlugins().value(QStringLiteral("plugins")).toArray();
            for (const auto& value : entries) {
                const auto plugin = value.toObject();
                if (plugin.value(QStringLiteral("id")).toString() == active->plugin && plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ready")) return;
            }
            active->child = token("format-render-"); setPhase(QStringLiteral("rendering"));
            emit q->progress(active->id, 0, 3, QStringLiteral("正在加载 3D 格式扩展"));
            plugins->preview(active->child, active->plugin, active->kind == QStringLiteral("regenerate") ? QStringLiteral("thumbnail") : active->kind,
                active->intent.value(QStringLiteral("item")).toObject(), active->root, active->background,
                builtinViewer(*active) ? active->id : QString());
        } else if (active->phase == QStringLiteral("waiting_native")) {
            if (native->info().value(QStringLiteral("busy")).toBool()) return;
            // Recheck source/cover after waiting for other native work. No cover
            // is submitted from the earlier renderer snapshot alone.
            setPhase(QStringLiteral("publication_preflight"));
            const auto id = active->id;
            helper(QStringLiteral("preview_publish"), active->kind == QStringLiteral("cover") ? QStringLiteral("plugins.set_cover") : active->kind == QStringLiteral("regenerate") ? QStringLiteral("plugins.regenerate_thumbnail") : QStringLiteral("plugins.thumbnail"),
                {{QStringLiteral("intent"), active->intent}, {QStringLiteral("dest"), active->rendered.value(QStringLiteral("dest"))},
                    {QStringLiteral("staging_root"), plugins->previewStagingRoot(active->plugin)}}, active->root,
                [this, id](const QJsonObject& data, const QJsonObject& error) {
                    if (!active || active->id != id) return;
                    validateOwnerAndPlugin(); if (!active) return;
                    if (!error.isEmpty()) { fail(error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString()); return; }
                    if (data.value(QStringLiteral("skipped")).toBool()) {
                        finish({{QStringLiteral("completion"), QStringLiteral("existing_cover_preserved")}, {QStringLiteral("skipped"), true}}); return;
                    }
                    if (native->info().value(QStringLiteral("busy")).toBool()) { setPhase(QStringLiteral("waiting_native")); return; }
                    active->published = data; setPhase(QStringLiteral("native")); active->child = token("format-cover-");
                    emit q->progress(active->id, 1, 3, QStringLiteral("正在写入 Billfish 原生封面"));
                    native->setFileCover(active->child, active->root, active->itemId, data.value(QStringLiteral("cover_path")).toString());
                });
        }
    }

    void rendered(const QString& child, const QJsonObject& result) {
        if (!active || active->child != child || active->phase != QStringLiteral("rendering")) return;
        validateOwnerAndPlugin(); if (!active) return;
        const auto expected = active->kind == QStringLiteral("viewer") ? QStringLiteral("plugin_viewer_rendered") : QStringLiteral("plugin_thumbnail_rendered");
        if (result.value(QStringLiteral("completion")).toString() != expected ||
            result.value(QStringLiteral("item_id")).toString() != active->itemId) {
            fail(QStringLiteral("preview_result_unverified"), QStringLiteral("The format renderer did not prove completion for the requested item."), result); return;
        }
        active->rendered = result;
        if (active->kind == QStringLiteral("viewer")) { finish(result); return; }
        setPhase(QStringLiteral("waiting_native")); pump();
    }

    void verifyCover() {
        if (!active) return;
        const auto id = active->id;
        helper(QStringLiteral("preview_verify"), QStringLiteral("plugins.thumbnail"), {{QStringLiteral("published"), active->published}}, active->root,
            [this, id](const QJsonObject& data, const QJsonObject& error) {
                if (!active || active->id != id) return;
                validateOwnerAndPlugin(); if (!active) return;
                if (!error.isEmpty()) { fail(error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString()); return; }
                if (!data.value(QStringLiteral("verified")).toBool()) {
                    if (active->verification.hasExpired()) { fail(QStringLiteral("native_cover_not_persisted"), QStringLiteral("The native action returned but the generated cover was not verified on disk and in a fresh catalog read."), data); return; }
                    QTimer::singleShot(150, q, [this, id] { if (active && active->id == id) verifyCover(); }); return;
                }
                auto result = data;
                result.insert(QStringLiteral("completion"), QStringLiteral("native_custom_cover_verified"));
                result.insert(QStringLiteral("native_result"), active->nativeResult);
                result.insert(QStringLiteral("image"), active->published.value(QStringLiteral("image")));
                if (active->kind == QStringLiteral("cover") || active->kind == QStringLiteral("regenerate")) result.insert(QStringLiteral("pose"), active->pose);
                if (!active->animationSource.isEmpty()) result.insert(QStringLiteral("animation_source"), active->animationSource);
                result.insert(QStringLiteral("refresh"), native->refreshCurrentSearch());
                finish(result);
            });
    }

    void nativeFinished(const QString& child, const QJsonObject& data) {
        if (!active || active->child != child ||
            (active->phase != QStringLiteral("native") && active->phase != QStringLiteral("types_native"))) return;
        validateOwnerAndPlugin(); if (!active) return;
        if (data.value(QStringLiteral("failed_steps")).toInt() || data.value(QStringLiteral("completion")).toString() != QStringLiteral("native_action_result")) {
            const auto results = data.value(QStringLiteral("results")).toArray();
            if (active->kind == QStringLiteral("types") && results.size() == 1 &&
                data.value(QStringLiteral("completion")).toString() == QStringLiteral("native_action_result") &&
                results.first().toObject().value(QStringLiteral("fields")).toObject().value(QStringLiteral("types")).toObject()
                    .value(QStringLiteral("native_code")).toInt() == 32105) {
                active->nativeResult = data; detachTypePersistence(true); return;
            }
            fail(active->kind == QStringLiteral("types") ? QStringLiteral("native_types_failed") : QStringLiteral("native_cover_failed"),
                QStringLiteral("The native action returned a failure result."), data); return;
        }
        active->nativeResult = data;
        if (active->kind == QStringLiteral("types")) {
            detachTypePersistence(); return;
        }
        active->verification = QDeadlineTimer(5000);
        setPhase(QStringLiteral("verifying"));
        verifyCover();
    }

    void readTypes() {
        if (!active) return;
        const auto id = active->id;
        helper(QStringLiteral("preview_types"), QStringLiteral("types.add"),
            {{QStringLiteral("extensions"), active->intent.value(QStringLiteral("extensions"))}}, active->root,
            [this, id](const QJsonObject& data, const QJsonObject& error) {
                if (!active || active->id != id) return;
                validateOwnerAndPlugin(); if (!active) return;
                if (!error.isEmpty()) { fail(error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString()); return; }
                active->intent = data;
                if (data.value(QStringLiteral("verified")).toBool()) {
                    auto result = data;
                    result.insert(QStringLiteral("completion"), QStringLiteral("native_file_types_verified"));
                    if (!active->nativeResult.isEmpty()) result.insert(QStringLiteral("native_result"), active->nativeResult);
                    finish(result); return;
                }
                setPhase(QStringLiteral("waiting_type_native")); pump();
            });
    }

    void clearPendingTypes() {
        for (const auto& pending : pendingTypes) if (pending->watcher) pending->watcher->deleteLater();
        pendingTypes.clear();
    }

    void watchTypeCatalog(const QSharedPointer<PendingTypes>& pending) {
        if (!pending->watcher) return;
        const QDir directory(QDir(pending->root).filePath(QStringLiteral(".bf")));
        QStringList paths{directory.absolutePath()};
        for (const auto& name : {QStringLiteral("billfish.db"), QStringLiteral("billfish.db-wal"), QStringLiteral("billfish.db-journal")}) {
            const auto path = directory.filePath(name);
            if (QFileInfo::exists(path)) paths.append(path);
        }
        paths.removeAll(QString());
        const auto watched = pending->watcher->files() + pending->watcher->directories();
        for (const auto& path : watched) paths.removeAll(path);
        if (!paths.isEmpty()) pending->watcher->addPaths(paths);
    }

    void publishPendingTypes(const QSharedPointer<PendingTypes>& pending, const QJsonObject& observed = {}) {
        QJsonObject result = observed.isEmpty() ? pending->intent : observed;
        result.insert(QStringLiteral("state"), pending->awaitingLibrarySync ? QStringLiteral("awaiting_library_sync") :
            pending->verified ? QStringLiteral("ready") : QStringLiteral("applied_pending_persistence"));
        result.insert(QStringLiteral("verified"), pending->verified);
        if (pending->awaitingLibrarySync) {
            result.insert(QStringLiteral("code"), QStringLiteral("native_library_sync_required"));
            result.insert(QStringLiteral("message"), QStringLiteral("Billfish requires a successful native library synchronization before format registration. Synchronize this library in Billfish to continue."));
        }
        if (pending->syncRequired && !pending->syncResult.isEmpty() && !pending->syncResult.value(QStringLiteral("requested")).toBool() &&
            !pending->syncResult.value(QStringLiteral("retryable")).toBool()) {
            result.insert(QStringLiteral("state"), QStringLiteral("failed"));
            result.insert(QStringLiteral("code"), QStringLiteral("native_library_sync_unavailable"));
        }
        result.insert(QStringLiteral("library_root"), pending->root);
        result.insert(QStringLiteral("native_result"), pending->nativeResult);
        result.insert(QStringLiteral("sync"), pending->syncResult);
        if (pending->verified) result.insert(QStringLiteral("completion"), QStringLiteral("native_file_types_verified"));
        plugins->setNativeFormatStatus(pending->plugin, result);
    }

    void detachTypePersistence(bool awaitingLibrarySync = false) {
        const auto work = *active;
        auto pending = QSharedPointer<PendingTypes>::create();
        pending->plugin = work.plugin; pending->root = work.root;
        pending->awaitingLibrarySync = awaitingLibrarySync;
        pending->syncRequired = !awaitingLibrarySync && work.plugin == QStringLiteral("billfish.animation-browser");
        pending->identity = native->libraryIdentity(); pending->epoch = generation;
        pending->intent = work.intent; pending->nativeResult = work.nativeResult;
        if (!awaitingLibrarySync) {
            pending->watcher = new QFileSystemWatcher(q);
            const auto dirty = [pending] { pending->dirty = true; };
            QObject::connect(pending->watcher, &QFileSystemWatcher::directoryChanged, q, dirty);
            QObject::connect(pending->watcher, &QFileSystemWatcher::fileChanged, q, dirty);
        }
        pendingTypes.insert(work.plugin, pending);
        // Install observation before the first fresh read so a concurrent commit
        // cannot fall between verification and watcher installation.
        watchTypeCatalog(pending);
        active.reset();
        publishPendingTypes(pending);
        maintainPendingTypes();
        QTimer::singleShot(0, q, [this] { pump(); });
    }

    void maintainPendingTypes() {
        const auto entries = pendingTypes;
        for (const auto& pending : entries) {
            const auto currentIdentity = native->libraryIdentity();
            if (pending->epoch != generation || !plugins->previewEnabled(pending->plugin) ||
                (!currentIdentity.isEmpty() && pending->identity != currentIdentity)) {
                if (pending->watcher) pending->watcher->deleteLater();
                pendingTypes.remove(pending->plugin); continue;
            }
            if (!ownerMatches(pending->root)) continue;
            if (pending->awaitingLibrarySync) {
                const auto readiness = native->fileTypeRegistrationReadiness(pending->root);
                if (!readiness.value(QStringLiteral("ready")).toBool()) {
                    if (readiness.value(QStringLiteral("code")).toString() != QStringLiteral("native_library_sync_required")) {
                        auto failure = readiness; failure.insert(QStringLiteral("state"), QStringLiteral("failed"));
                        failure.insert(QStringLiteral("library_root"), pending->root);
                        plugins->setNativeFormatStatus(pending->plugin, failure);
                        pendingTypes.remove(pending->plugin);
                    }
                    continue;
                }
                // Only a real ScanSync success releases this prerequisite. No
                // sync request or repeated SQL/native action runs while waiting.
                pendingTypes.remove(pending->plugin);
                Work work; work.id = token("format-types-"); work.kind = QStringLiteral("types"); work.automatic = true;
                work.plugin = pending->plugin; work.requestedPlugin = pending->plugin;
                work.root = pending->root; work.intent = {{QStringLiteral("extensions"), pending->intent.value(QStringLiteral("extensions"))}};
                queue.prepend(work);
                plugins->setNativeFormatStatus(work.plugin, {{QStringLiteral("state"), QStringLiteral("registering")},
                    {QStringLiteral("library_root"), work.root}, {QStringLiteral("extensions"), work.intent.value(QStringLiteral("extensions"))}});
                QTimer::singleShot(0, q, [this] { pump(); });
                continue;
            }
            if (pending->syncRequired && !pending->syncResult.value(QStringLiteral("requested")).toBool() &&
                (pending->syncResult.isEmpty() || pending->syncResult.value(QStringLiteral("retryable")).toBool())) {
                pending->syncResult = native->syncLibraryAfterTypeRegistration(pending->root);
                publishPendingTypes(pending);
            }
            if (pending->verified) {
                if (!pending->syncRequired || pending->syncResult.value(QStringLiteral("requested")).toBool() ||
                    !pending->syncResult.value(QStringLiteral("retryable")).toBool()) {
                    if (pending->watcher) pending->watcher->deleteLater();
                    pendingTypes.remove(pending->plugin);
                }
                continue;
            }
            if (!pending->dirty || pending->reading) continue;
            watchTypeCatalog(pending); pending->dirty = false; pending->reading = true;
            helper(QStringLiteral("preview_types"), QStringLiteral("types.add"),
                {{QStringLiteral("extensions"), pending->intent.value(QStringLiteral("extensions"))}}, pending->root,
                [this, pending](const QJsonObject& data, const QJsonObject& error) {
                    if (stopped || pending->epoch != generation || pendingTypes.value(pending->plugin) != pending ||
                        !plugins->previewEnabled(pending->plugin)) return;
                    pending->reading = false;
                    const auto currentIdentity = native->libraryIdentity();
                    if (currentIdentity.isEmpty()) { pending->dirty = true; return; }
                    if (pending->identity != currentIdentity) return;
                    if (!error.isEmpty()) {
                        if (error.value(QStringLiteral("code")).toString() == QStringLiteral("unsupported_schema")) {
                            auto failure = error; failure.insert(QStringLiteral("state"), QStringLiteral("failed"));
                            failure.insert(QStringLiteral("library_root"), pending->root);
                            plugins->setNativeFormatStatus(pending->plugin, failure);
                            if (pending->watcher) pending->watcher->deleteLater();
                            pendingTypes.remove(pending->plugin);
                        }
                        return;
                    }
                    pending->intent = data; pending->verified = data.value(QStringLiteral("verified")).toBool();
                    publishPendingTypes(pending, data);
                    if (pending->verified && pending->watcher) pending->watcher->deleteLater();
                });
        }
    }

    void syncTypes() {
        const auto declarations = plugins->previewDeclarations();
        const auto library = native->info().value(QStringLiteral("library_root")).toString();
        if (!ownerMatches(library)) return;
        const auto signature = normalized(library) + QString::fromUtf8(QJsonDocument(declarations).toJson(QJsonDocument::Compact));
        if (signature == typeSignature) return;
        typeSignature = signature;
        for (const auto& value : declarations) {
            const auto declaration = value.toObject();
            const auto pending = pendingTypes.value(declaration.value(QStringLiteral("id")).toString());
            if (pending && normalized(pending->root) == normalized(library) &&
                pending->intent.value(QStringLiteral("extensions")) == declaration.value(QStringLiteral("extensions"))) continue;
            Work work; work.id = token("format-types-"); work.kind = QStringLiteral("types"); work.automatic = true;
            work.plugin = declaration.value(QStringLiteral("id")).toString(); work.requestedPlugin = work.plugin;
            work.root = library; work.intent = {{QStringLiteral("extensions"), declaration.value(QStringLiteral("extensions"))}};
            queue.prepend(work);
            plugins->setNativeFormatStatus(work.plugin, {{QStringLiteral("state"), QStringLiteral("registering")},
                {QStringLiteral("library_root"), library}, {QStringLiteral("extensions"), declaration.value(QStringLiteral("extensions"))}});
        }
        pump();
    }

    void discover() {
        if (stopped) return;
        maintainPendingTypes();
        auto* current = native->currentMaterialView();
        const bool geometryAttached = coverGeometry->attach();
        const auto identity = native->libraryIdentity();
        if (identity != statusIdentity) { coverGeometry->clearStatus(); statusIdentity = identity; }
        const auto epoch = generation;
        const bool statusAttached = !identity.isEmpty() && coverGeometry->attachStatus(current, [this, identity, epoch](const QModelIndex& index) {
            if (stopped || epoch != generation || identity != native->libraryIdentity()) return QString();
            return previewCaption(index);
        });
        if ((geometryAttached || statusAttached) && current && current->viewport()) current->viewport()->update();
        if (current != view || (current && current->model() != model)) {
            for (const auto& connection : modelConnections) QObject::disconnect(connection);
            modelConnections.clear(); hydratedIds.clear(); view = current; model = current ? current->model() : nullptr;
            if (model) {
                const auto changed = [this] { hydratedIds.clear(); scheduleScan(); };
                modelConnections.append(QObject::connect(model, &QAbstractItemModel::modelReset, q, changed));
                modelConnections.append(QObject::connect(model, &QAbstractItemModel::rowsInserted, q, changed));
                modelConnections.append(QObject::connect(model, &QAbstractItemModel::layoutChanged, q, changed));
            }
            scheduleScan();
        }
        auto* proxy = model ? qobject_cast<QAbstractProxyModel*>(model.data()) : nullptr;
        auto* source = proxy ? proxy->sourceModel() : nullptr;
        if (visibilityDirty || model != visibilityModel || source != visibilitySource) {
            visibilityDirty = false;
            visibilityModel = model; visibilitySource = source;
            if (!visibilityRequestedPaths.isEmpty() && !visibilityRoot.isEmpty())
                updateVisibility({}, visibilityRequestedPaths, visibilityRoot);
        }
    }

    void scheduleScan() {
        if (stopped || scanQueued) return;
        scanQueued = true;
        QTimer::singleShot(100, q, [this] { scanQueued = false; scanVisible(); });
    }

    void scanVisible() {
        if (stopped || scanRunning || pluginSignature.isEmpty() || !view || !view->viewport() || !model) return;
        QJsonArray ids; QSet<QString> seen;
        QSet<QPersistentModelIndex> sampled;
        const auto add = [this, &ids, &seen](const QModelIndex& index) {
            if (!index.isValid() || ids.size() >= 512) return;
            const auto item = native->itemForIndex(index);
            const auto id = item.value(QStringLiteral("item_id")).toString();
            const auto extension = item.value(QStringLiteral("extension")).toString();
            if (!id.isEmpty() && !seen.contains(id) && !hydratedIds.contains(id) &&
                !plugins->previewPlugin(QStringLiteral("source") + extension, QStringLiteral("thumbnail")).isEmpty()) {
                seen.insert(id); ids.append(id);
            }
        };
        // Sample only the visible viewport. Native indexAt is authoritative;
        // there is no whole-catalog or entire-model row scan.
        for (int y = 8; y < view->viewport()->height(); y += 24)
            for (int x = 8; x < view->viewport()->width(); x += 24) {
                const auto index = view->indexAt(QPoint(x, y));
                if (index.isValid()) sampled.insert(index);
            }
        sampled.insert(view->currentIndex());
        for (const auto& index : sampled) add(index);
        if (ids.isEmpty()) return;
        const auto info = native->info();
        const auto library = info.value(QStringLiteral("library_root")).toString();
        if (!ownerMatches(library)) return;
        for (const auto& id : ids) hydratedIds.insert(id.toString());
        scanRunning = true; const auto epoch = generation;
        helper(QStringLiteral("preview_items"), QStringLiteral("plugins.thumbnail"), {{QStringLiteral("item_ids"), ids}}, library,
            [this, library, epoch](const QJsonObject& data, const QJsonObject& error) {
                if (epoch != generation) return;
                scanRunning = false;
                if (!error.isEmpty() || !ownerMatches(library)) return;
                const QJsonArray items = data.value(QLatin1String("items")).toArray();
                for (const auto& value : items) {
                    const auto item = value.toObject();
                    const auto plugin = plugins->previewPlugin(item.value(QStringLiteral("path")).toString(), QStringLiteral("thumbnail"));
                    const auto id = item.value(QStringLiteral("id")).toString();
                    const auto stamp = normalized(library) + QLatin1Char('|') + id + QLatin1Char('|') + item.value(QStringLiteral("source_stamp")).toString();
                    if (item.value(QStringLiteral("has_cover")).toBool() || plugin.isEmpty() || attempted.contains(stamp)) continue;
                    attempted.insert(stamp);
                    if (queue.size() >= 256) continue;
                    Work work; work.id = token("format-auto-"); work.itemId = id; work.kind = QStringLiteral("thumbnail");
                    work.root = library; work.requestedPlugin = plugin; work.automatic = true; work.background = true;
                    thumbnailErrors.remove(id);
                    queue.enqueue(work);
                }
                repaintPreviewStatus(); pump();
            });
    }
};

FormatPreviewBridge::FormatPreviewBridge(NativeAdapter* native, PluginManager* plugins, QObject* parent)
    : QObject(parent), d_(std::make_unique<FormatPreviewBridgePrivate>(this, native, plugins)) {
    connect(plugins, &PluginManager::finished, this, [this](const QString& id, const QJsonObject& data) { d_->rendered(id, data); });
    connect(plugins, &PluginManager::failed, this, [this](const QString& id, const QString& code, const QString& message) {
        if (d_->active && d_->active->child == id) d_->fail(code, message);
    });
    connect(plugins, &PluginManager::changed, this, [this] {
        d_->validateOwnerAndPlugin();
        d_->syncTypes();
        QString signature;
        const QJsonArray entries = d_->plugins->listPlugins().value(QLatin1String("plugins")).toArray();
        for (const auto& value : entries) {
            const auto plugin = value.toObject();
            if (d_->plugins->previewEnabled(plugin.value(QStringLiteral("id")).toString()))
                signature += plugin.value(QStringLiteral("id")).toString() + QLatin1Char('|');
        }
        if (signature != d_->pluginSignature) {
            d_->pluginSignature = signature; d_->hydratedIds.clear(); d_->scheduleScan();
        }
    });
    connect(plugins, &PluginManager::builtinSessionStopped, this, [this] { d_->clearVisibility(); d_->clearPendingTypes(); d_->typeSignature.clear(); d_->clearPreviewStatus();
        ++d_->builtinViewerSession;
        d_->removeBuiltinViewers(QStringLiteral("plugin_disabled"), QStringLiteral("The built-in preview session stopped."));
    });
    connect(d_->visibility, &AnimationVisibility::bindingInvalidated, this, [this] {
        // Native arrangement changes can rebind the same source pointer. The
        // component has detached; do not replay cached IDs or reenter its Qt
        // source-change signal. Discovery will obtain a fresh catalog snapshot.
        ++d_->visibilityGeneration; d_->visibilityDirty = true;
        d_->visibilityPaths.clear(); d_->visibilityIds.clear();
        d_->visibilityMappedProxy.clear(); d_->visibilityMappedSource.clear(); d_->visibilityMappedIdentity.clear();
    });
    connect(native, &NativeAdapter::finished, this, [this](const QString& id, const QJsonObject& data) { d_->nativeFinished(id, data); });
    connect(native, &NativeAdapter::failed, this, [this](const QString& id, const QString& code, const QString& message, const QJsonObject& details) {
        if (d_->active && d_->active->child == id) d_->fail(code, message, details);
    });
    // A material view can become interactive between discovery ticks. Observe
    // input now and authenticate its exact target before vendor double-click
    // handling, independently of thumbnail/geometry discovery.
    QCoreApplication::instance()->installEventFilter(this);
}

FormatPreviewBridge::~FormatPreviewBridge() = default;

void FormatPreviewBridge::request(const QString& id, const QString& kind, const QString& itemId,
                                 const QString& pluginId, const QString& root, bool background, const QString& extension) {
    if (d_->stopped) { emit failed(id, QStringLiteral("application_quitting"), QStringLiteral("Billfish is shutting down."), {}); return; }
    FormatPreviewBridgePrivate::Work work;
    work.id = id; work.kind = kind; work.itemId = itemId; work.requestedPlugin = pluginId; work.root = root; work.background = background;
    if (kind == QStringLiteral("viewer")) {
        work.viewerIntent = ++d_->nextViewerIntent; work.viewerSession = d_->builtinViewerSession;
        if (!extension.isEmpty()) work.feedback = {{QStringLiteral("item_id"), itemId}, {QStringLiteral("extension"), extension},
            {QStringLiteral("library_root"), root}, {QStringLiteral("background"), background}};
        if (pluginId.isEmpty()) { d_->resolveViewer(work); return; }
    }
    // New builtin viewer intent removes older queued/active viewer work before
    // capacity checks. Explicit third-party and automatic cover work keep order.
    d_->enqueue(work);
}

void FormatPreviewBridge::publishViewerCover(const QString& id, const QJsonObject& capture, const QString& root) {
    if (d_->stopped || d_->queue.size() >= 256) {
        emit failed(id, QStringLiteral("preview_busy"), QStringLiteral("The format preview queue is unavailable or full."), {}); return;
    }
    FormatPreviewBridgePrivate::Work work;
    work.id = id; work.kind = QStringLiteral("cover"); work.itemId = capture.value(QStringLiteral("item_id")).toString();
    work.requestedPlugin = capture.value(QStringLiteral("plugin_id")).toString(); work.plugin = work.requestedPlugin;
    work.root = root; work.sourcePath = capture.value(QStringLiteral("source_path")).toString();
    work.pose = capture.value(QStringLiteral("pose")).toObject();
    work.sourceFingerprint = capture.value(QStringLiteral("source_fingerprint")).toObject();
    work.animationSource = capture.value(QStringLiteral("animation_source")).toObject();
    work.rendered = {{QStringLiteral("dest"), capture.value(QStringLiteral("dest"))}};
    int position = 0;
    while (position < d_->queue.size() && !d_->queue.at(position).automatic) ++position;
    d_->thumbnailErrors.remove(work.itemId);
    d_->queue.insert(position, work); d_->repaintPreviewStatus(); d_->pump();
}

void FormatPreviewBridge::regenerateThumbnail(const QString& id, const QJsonObject& capture, const QString& root) {
    if (d_->stopped || d_->queue.size() >= 256) {
        emit failed(id, QStringLiteral("preview_busy"), QStringLiteral("The format preview queue is unavailable or full."), {}); return;
    }
    FormatPreviewBridgePrivate::Work work;
    work.id = id; work.kind = QStringLiteral("regenerate"); work.itemId = capture.value(QStringLiteral("item_id")).toString();
    work.requestedPlugin = capture.value(QStringLiteral("plugin_id")).toString(); work.plugin = work.requestedPlugin;
    work.root = root; work.sourcePath = capture.value(QStringLiteral("source_path")).toString();
    work.sourceFingerprint = capture.value(QStringLiteral("source_fingerprint")).toObject();
    work.pose = {{QStringLiteral("kind"), QStringLiteral("base")}, {QStringLiteral("time"), 0}};
    work.background = true;
    int position = 0;
    while (position < d_->queue.size() && !d_->queue.at(position).automatic) ++position;
    d_->thumbnailErrors.remove(work.itemId);
    d_->queue.insert(position, work); d_->repaintPreviewStatus(); d_->pump();
}

void FormatPreviewBridge::setAnimationVisibility(const QString& id, const QJsonObject& arguments, const QString& root) {
    if (d_->stopped) { emit failed(id, QStringLiteral("application_quitting"), QStringLiteral("Billfish is shutting down."), {}); return; }
    d_->updateVisibility(id, arguments.value(QStringLiteral("hidden_paths")).toArray(), root);
}

void FormatPreviewBridge::libraryChanged(const QString& root) {
    d_->clearVisibility(); d_->clearPendingTypes(); d_->clearPreviewStatus();
    d_->clearResolvingViewers(QStringLiteral("library_changed"), QStringLiteral("The native library changed during preview preparation."));
    d_->root = root; ++d_->generation; d_->scanRunning = false; d_->hydratedIds.clear(); d_->typeSignature.clear();
    if (d_->active) d_->fail(QStringLiteral("library_changed"), QStringLiteral("The native library changed during format preview."));
    while (!d_->queue.isEmpty()) {
        const auto work = d_->queue.dequeue();
        if (!work.automatic) emit failed(work.id, QStringLiteral("library_changed"), QStringLiteral("The native library changed before format preview."), {});
    }
    d_->scheduleScan();
}

void FormatPreviewBridge::shutdown() {
    if (d_->stopped) return;
    d_->stopped = true; d_->discovery->stop(); d_->pumpTimer->stop();
    QCoreApplication::instance()->removeEventFilter(this);
    d_->coverGeometry->clear();
    libraryChanged({});
}

bool FormatPreviewBridge::eventFilter(QObject* watched, QEvent* event) {
    if (d_->stopped) return false;
    const auto type = event->type();
    if (type != QEvent::MouseButtonDblClick && type != QEvent::KeyPress) {
        if (d_->view && (watched == d_->view || watched == d_->view->viewport()) &&
            (type == QEvent::Paint || type == QEvent::Show || type == QEvent::Resize || type == QEvent::Wheel))
            d_->scheduleScan();
        return false;
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    auto* inputView = qobject_cast<QAbstractItemView*>(widget);
    if (!inputView && widget) {
        auto* parentView = qobject_cast<QAbstractItemView*>(widget->parentWidget());
        if (parentView && widget == parentView->viewport()) inputView = parentView;
    }
    if (!inputView || !inputView->inherits("BFBaseItemView")) return false;
    QModelIndex index;
    if (watched == inputView->viewport() && type == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        if (inputView != d_->native->currentMaterialView()) return false;
        index = inputView->indexAt(mouse->pos());
    } else if (watched == inputView && type == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->isAutoRepeat() || key->modifiers() != Qt::NoModifier ||
            (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter && key->key() != Qt::Key_Space)) return false;
        if (inputView != d_->native->currentMaterialView()) return false;
        index = inputView->currentIndex();
    } else return false;
    const auto item = d_->native->itemForIndex(index);
    const auto id = item.value(QStringLiteral("item_id")).toString();
    const auto extension = item.value(QStringLiteral("extension")).toString();
    const auto plugin = d_->plugins->previewPlugin(QStringLiteral("source") + extension, QStringLiteral("viewer"));
    if (id.isEmpty() || extension.isEmpty() || plugin.isEmpty()) return false;
    const auto root = d_->native->info().value(QStringLiteral("library_root")).toString();
    request(token("format-open-"), QStringLiteral("viewer"), id, plugin, root, AppInputScope::active(), extension);
    event->accept(); return true;
}
}
