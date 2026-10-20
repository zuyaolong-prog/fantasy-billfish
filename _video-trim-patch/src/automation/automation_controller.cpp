#include "automation/automation_controller.hpp"
#include "automation/automation_ui.hpp"
#include "automation/native_adapter.hpp"
#include "automation/plugin_manager.hpp"
#include "automation/runtime_host.hpp"
#include "automation/media_transfer_process.hpp"
#include "automation/video_capture.hpp"
#include "automation/format_preview_bridge.hpp"
#include "automation/app_input_scope.hpp"

#include <QCoreApplication>
#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QItemSelectionModel>
#include <QProcess>
#include <QSharedPointer>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <QWidget>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace billfish::automation {
namespace {
bool writeOperation(const QString& operation) {
    return operation == QStringLiteral("items.update") || operation == QStringLiteral("tags.create") ||
           operation == QStringLiteral("tags.assign");
}
bool importOperation(const QString& operation) {
    return operation == QStringLiteral("items.import_path") || operation == QStringLiteral("items.import_url") ||
        operation == QStringLiteral("video.capture");
}
bool importHelper(const QString& mode) {
    return mode == QStringLiteral("import_validate") || mode == QStringLiteral("import_preflight") ||
        mode == QStringLiteral("import_verify") || mode == QStringLiteral("browser_destination");
}
bool replacementHelper(const QString& mode) {
    return mode == QStringLiteral("replace_preflight") || mode == QStringLiteral("replace_commit") ||
        mode == QStringLiteral("replace_verify");
}
QString fileNameFromTitle(QString title, const QString& suffix) {
    const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    for (int index = 0; index < title.size(); ++index)
        if (title.at(index).unicode() < 32 || forbidden.contains(title.at(index))) title[index] = QLatin1Char('_');
    title = title.trimmed();
    const QString extension = QLatin1Char('.') + suffix;
    if (title.endsWith(extension, Qt::CaseInsensitive)) title.chop(extension.size());
    while (title.endsWith(QLatin1Char('.')) || title.endsWith(QLatin1Char(' '))) title.chop(1);
    if (title.isEmpty()) title = QStringLiteral("video");
    const QString device = title.section(QLatin1Char('.'), 0, 0).toUpper();
    if (device == QStringLiteral("CON") || device == QStringLiteral("PRN") || device == QStringLiteral("AUX") || device == QStringLiteral("NUL") ||
        (device.size() == 4 && (device.startsWith(QStringLiteral("COM")) || device.startsWith(QStringLiteral("LPT"))) &&
            device.at(3) >= QLatin1Char('1') && device.at(3) <= QLatin1Char('9')))
        title.prepend(QLatin1Char('_'));
    title = title.left(qMax(1, 255 - extension.size()));
    if (!title.isEmpty() && title.at(title.size() - 1).isHighSurrogate()) title.chop(1);
    while (title.endsWith(QLatin1Char('.')) || title.endsWith(QLatin1Char(' '))) title.chop(1);
    if (title.isEmpty()) title = QStringLiteral("video");
    return title + extension;
}
bool readOperation(const QString& operation) {
    return operation == QStringLiteral("library.info") || operation == QStringLiteral("items.search") ||
        operation == QStringLiteral("items.get") || operation == QStringLiteral("folders.list") ||
        operation == QStringLiteral("tags.list");
}
QString normalizedRoot(const QString& root) {
    return QDir::cleanPath(QDir::fromNativeSeparators(root)).toCaseFolded();
}
struct HelperOutput {
    QByteArray output;
    bool settled = false;
};
}

AutomationController::AutomationController(RuntimeHost* host, QObject* parent)
    : QObject(parent), host_(host), native_(new NativeAdapter(this)),
      plugins_(new PluginManager(QCoreApplication::applicationDirPath(), this)),
      ui_(new AutomationUi(plugins_, this)), video_(new VideoCaptureController(this)),
      formats_(new FormatPreviewBridge(native_, plugins_, this)) {
    setObjectName(QStringLiteral("BillfishAutomationController"));
    plugins_->setWindowOwnerProvider([this]() -> quintptr {
        auto* window = qobject_cast<QWidget*>(host_->findObjectByClass(QByteArrayLiteral("CFramelessWindow")));
        return window && window->isWindow() ? static_cast<quintptr>(window->effectiveWinId()) : 0;
    });
    ui_->setActiveLibraryProvider([this] {
        return currentInfo().value(QStringLiteral("library_root")).toString();
    });
    ui_->setMaterialContextProvider([this] {
        const auto selection = native_->selectedItems();
        if (!selection.value(QStringLiteral("available")).toBool()) return selection;
        auto* view = native_->currentMaterialView();
        if (!view || !view->selectionModel()) return QJsonObject{};
        QJsonArray items;
        QSet<QString> seen;
        for (const auto& index : view->selectionModel()->selectedIndexes()) {
            const auto item = native_->itemForIndex(index);
            if (!item.value(QStringLiteral("available")).toBool()) return QJsonObject{};
            const auto id = item.value(QStringLiteral("item_id")).toString();
            if (!seen.contains(id)) { seen.insert(id); items.append(item); }
        }
        const auto after = native_->selectedItems();
        if (after != selection) return QJsonObject{};
        return QJsonObject{{QStringLiteral("available"), true},
            {QStringLiteral("library_root"), selection.value(QStringLiteral("library_root"))},
            {QStringLiteral("items"), items}};
    });
    ui_->setFormatPreviewHandler([this](const QString& plugin, const QString& item, const QString& root) {
        formats_->request(QStringLiteral("format-open-menu-") + QUuid::createUuid().toString(QUuid::WithoutBraces),
            QStringLiteral("viewer"), item, plugin, root, AppInputScope::active());
    });
    host_->setInfoProvider([this] { return currentInfo(); });
    connect(host_, &RuntimeHost::requestReceived, this,
            [this](const QString& id, const QJsonObject& request) { dispatch(id, request); });
    connect(native_, &NativeAdapter::finished, this, &AutomationController::nativeFinished);
    connect(native_, &NativeAdapter::failed, this,
        [this](const QString& id, const QString& code, const QString& message, const QJsonObject& details) {
            if (imports_.contains(id) && details.value(QStringLiteral("operation")).toString() == QStringLiteral("items.import")) {
                imports_[id].nativePending = false;
                imports_[id].sourceSafe = details.value(QStringLiteral("source_safe_to_delete")).toBool();
                imports_[id].nativeResult = details;
            }
            fail(id, code, message, details);
        });
    connect(native_, &NativeAdapter::progress, this, &AutomationController::progress);
    connect(native_, &NativeAdapter::activeLibraryChanged, this, [this](const QString& root) {
        plugins_->setLibraryRoot(root);
        ui_->setActiveLibrary(root);
        video_->libraryChanged(root);
        formats_->libraryChanged(root);
        const auto identifiers = imports_.keys();
        for (const QString& id : identifiers) {
            if (calls_.contains(id) && normalizedRoot(calls_[id].libraryRoot) != normalizedRoot(root))
                fail(id, QStringLiteral("library_changed"), QStringLiteral("The bound library closed or changed during import."));
        }
        const auto replacements = replacements_.keys();
        for (const QString& id : replacements) {
            if (calls_.contains(id) && normalizedRoot(calls_[id].libraryRoot) != normalizedRoot(root))
                fail(id, QStringLiteral("library_changed"), QStringLiteral("The bound library closed or changed during image replacement."));
        }
    });
    connect(plugins_, &PluginManager::finished, this, [this](const QString& id, const QJsonObject& data) {
        if (id.startsWith(QStringLiteral("format-"))) return;
        ensureUiJob(id); succeed(id, data);
    });
    connect(plugins_, &PluginManager::failed, this, [this](const QString& id, const QString& code, const QString& message) {
        if (id.startsWith(QStringLiteral("format-"))) return;
        ensureUiJob(id); fail(id, code, message);
    });
    connect(plugins_, &PluginManager::progress, this, [this](const QString& id, int done, int total, const QString& message) {
        if (id.startsWith(QStringLiteral("format-"))) return;
        ensureUiJob(id); progress(id, done, total, message);
    });
    connect(plugins_, &PluginManager::hostCall, this,
        [this](const QString& id, const QString& operation, const QJsonObject& arguments, const QString& root) {
            dispatch(id, {{QStringLiteral("operation"), operation}, {QStringLiteral("arguments"), arguments},
                          {QStringLiteral("library_root"), root}}, true);
        });
    connect(formats_, &FormatPreviewBridge::finished, this, [this](const QString& id, const QJsonObject& data) {
        if (id.startsWith(QStringLiteral("format-open-"))) ensureUiJob(id);
        succeed(id, data);
    });
    connect(formats_, &FormatPreviewBridge::failed, this, [this](const QString& id, const QString& code, const QString& message, const QJsonObject& details) {
        if (id.startsWith(QStringLiteral("format-open-"))) ensureUiJob(id);
        fail(id, code, message, details);
    });
    connect(formats_, &FormatPreviewBridge::progress, this, [this](const QString& id, int done, int total, const QString& message) {
        if (id.startsWith(QStringLiteral("format-auto-"))) return;
        if (id.startsWith(QStringLiteral("format-open-"))) ensureUiJob(id);
        progress(id, done, total, message);
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, formats_, &FormatPreviewBridge::shutdown);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, plugins_, &PluginManager::shutdown);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, video_, &VideoCaptureController::shutdown);
    connect(video_, &VideoCaptureController::progress, this, &AutomationController::progress);
    connect(video_, &VideoCaptureController::finished, this, &AutomationController::succeed);
    connect(video_, &VideoCaptureController::failed, this, &AutomationController::fail);
    connect(video_, &VideoCaptureController::transferRequested, this,
        [this](const QString& id, const QJsonObject& request) { startMediaTransfer(id, request, true); });
    connect(video_, &VideoCaptureController::transferCancelRequested, this, [this](const QString& id) {
        if (videoTransfers_.value(id)) videoTransfers_.value(id)->cancel();
    });
    connect(video_, &VideoCaptureController::importRequested, this,
        [this](const QString& id, const QString& path, const QString& name, const QString& origin, const QString& root) {
            if (!calls_.contains(id)) return;
            Import import;
            import.videoCapture = true;
            import.intent = {{QStringLiteral("source_path"), path},
                {QStringLiteral("name"), fileNameFromTitle(name, QFileInfo(path).suffix())},
                {QStringLiteral("origin"), origin}};
            imports_.insert(id, import);
            if (normalizedRoot(root) != normalizedRoot(calls_[id].libraryRoot) || !importOwnerMatches(id)) {
                if (calls_.contains(id)) fail(id, QStringLiteral("library_changed"), QStringLiteral("The video capture library changed before import."));
                return;
            }
            runHelper(id, {{QStringLiteral("operation"), QStringLiteral("browser.destination")},
                {QStringLiteral("arguments"), QJsonObject()}, {QStringLiteral("library_root"), calls_[id].libraryRoot}},
                QStringLiteral("browser_destination"));
        });
    currentInfo();
}

QJsonObject AutomationController::currentInfo() {
    QJsonObject info = native_->info();
    const QString root = info.value(QStringLiteral("ready")).toBool()
        ? info.value(QStringLiteral("library_root")).toString() : QString();
    plugins_->setLibraryRoot(root);
    ui_->setActiveLibrary(root);
    return info;
}

void AutomationController::sendSuccess(const QString& id, const QJsonObject& data, bool pluginSdk) {
    if (pluginSdk) plugins_->completeHostCall(id, data); else host_->replySuccess(id, data);
}
void AutomationController::sendError(const QString& id, const QString& code, const QString& message, bool pluginSdk,
                                     const QJsonObject& details) {
    if (pluginSdk) {
        const QString jobId = details.value(QStringLiteral("job_id")).toString();
        plugins_->failHostCall(id, code.toLower(), jobId.isEmpty() ? message :
            message + QStringLiteral(" [job_id: ") + jobId + QStringLiteral("]"), details);
    } else host_->replyError(id, code.toLower(), message, details);
}

void AutomationController::dispatch(const QString& id, const QJsonObject& request, bool pluginSdk) {
    const QString operation = request.value(QStringLiteral("operation")).toString();
    const QJsonObject arguments = request.value(QStringLiteral("arguments")).toObject();
    const bool isJob = writeOperation(operation) || importOperation(operation) || operation == QStringLiteral("items.replace_file") || operation == QStringLiteral("plugins.run") ||
        operation == QStringLiteral("plugins.install") || operation == QStringLiteral("plugins.preview") || operation == QStringLiteral("plugins.thumbnail") || operation == QStringLiteral("plugins.set_cover") || operation == QStringLiteral("plugins.regenerate_thumbnail");
    QJsonObject fingerprintRequest = request;
    fingerprintRequest.remove(QStringLiteral("id"));
    const QByteArray fingerprint = QJsonDocument(fingerprintRequest).toJson(QJsonDocument::Compact);
    if (jobs_.contains(id)) {
        const Job& job = jobs_[id];
        if (job.fingerprint != fingerprint) {
            sendError(id, QStringLiteral("request_conflict"), QStringLiteral("Request id belongs to a different request."), pluginSdk);
        } else if (job.data.value(QStringLiteral("state")).toString() == QStringLiteral("completed")) {
            sendSuccess(id, job.data.value(QStringLiteral("result")).toObject(), pluginSdk);
        } else if (job.data.value(QStringLiteral("state")).toString() == QStringLiteral("failed")) {
            const auto error = job.data.value(QStringLiteral("error")).toObject();
            QJsonObject details = error.value(QStringLiteral("details")).toObject();
            if (details.isEmpty()) {
                details.insert(QStringLiteral("job_id"), id);
                if (job.data.contains(QStringLiteral("result"))) details.insert(QStringLiteral("result"), job.data.value(QStringLiteral("result")));
            }
            sendError(id, error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString(), pluginSdk, details);
        } else {
            sendError(id, QStringLiteral("job_running"), QStringLiteral("Request is still running; query jobs.get with the same job_id."), pluginSdk);
        }
        return;
    }
    calls_.insert(id, Call{request, {}, pluginSdk, isJob});
    if (isJob) {
        jobs_.insert(id, Job{fingerprint, {{QStringLiteral("id"), id}, {QStringLiteral("job_id"), id},
            {QStringLiteral("operation"), operation}, {QStringLiteral("state"), QStringLiteral("accepted")},
            {QStringLiteral("library_root"), QString()}, {QStringLiteral("completed"), 0},
            {QStringLiteral("total"), 0}, {QStringLiteral("message"), QStringLiteral("任务已接收")}}});
        if (!pluginSdk) ui_->updateProgress(id, 0, 0, QStringLiteral("任务已接收"));
    }
    if (operation == QStringLiteral("jobs.get")) {
        const QString jobId = arguments.value(QStringLiteral("job_id")).toString();
        if (!jobs_.contains(jobId)) fail(id, QStringLiteral("job_not_found"), QStringLiteral("Unknown job_id."));
        else succeed(id, jobs_[jobId].data);
    } else if (operation == QStringLiteral("jobs.list")) {
        QJsonArray values;
        for (auto it = jobs_.cbegin(); it != jobs_.cend(); ++it) values.append(it.value().data);
        succeed(id, {{QStringLiteral("jobs"), values}});
    } else if (operation == QStringLiteral("plugins.list")) {
        succeed(id, plugins_->listPlugins());
    } else if (operation == QStringLiteral("plugins.install")) {
        const QJsonValue source = arguments.value(QStringLiteral("source_path"));
        if (arguments.size() != 1 || !source.isString() || source.toString().isEmpty() || source.toString().contains(QChar(0))) {
            fail(id, QStringLiteral("invalid_arguments"), QStringLiteral("plugins.install requires one nonempty source_path string."));
            return;
        }
        plugins_->installPluginAsync(id, source.toString());
    } else if (operation == QStringLiteral("plugins.enable")) {
        if (!arguments.value(QStringLiteral("enabled")).isBool()) {
            fail(id, QStringLiteral("invalid_arguments"), QStringLiteral("enabled must be a boolean.")); return;
        }
        const QJsonObject result = plugins_->setEnabled(arguments.value(QStringLiteral("id")).toString(),
            arguments.value(QStringLiteral("enabled")).toBool());
        if (result.value(QStringLiteral("error")).isObject()) {
            const auto error = result.value(QStringLiteral("error")).toObject();
            fail(id, error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString());
        } else succeed(id, result);
    } else if (operation == QStringLiteral("ui.settings.open")) {
        currentInfo();
        if (ui_->showPluginSettings()) succeed(id, {{QStringLiteral("opened"), true}});
        else fail(id, QStringLiteral("settings_unavailable"),
            QStringLiteral("The native Billfish settings entry is unavailable."));
    } else if (operation == QStringLiteral("animation.visibility")) {
        if (!pluginSdk) { fail(id, QStringLiteral("unknown_operation"), QStringLiteral("Animation visibility is private to the built-in browser.")); return; }
        if (!bindLibrary(id)) return;
        formats_->setAnimationVisibility(id, arguments, calls_[id].libraryRoot);
    } else if (operation == QStringLiteral("plugins.regenerate_thumbnail")) {
        if (!pluginSdk) { fail(id, QStringLiteral("unknown_operation"), QStringLiteral("Thumbnail regeneration is private to the built-in browser.")); return; }
        if (!bindLibrary(id)) return;
        formats_->regenerateThumbnail(id, arguments, calls_[id].libraryRoot);
    } else if (operation == QStringLiteral("plugins.set_cover")) {
        if (!pluginSdk) { fail(id, QStringLiteral("unknown_operation"), QStringLiteral("Viewer cover publication is private to the bound plugin session.")); return; }
        if (!bindLibrary(id)) return;
        formats_->publishViewerCover(id, arguments, calls_[id].libraryRoot);
    } else if (operation == QStringLiteral("plugins.preview") || operation == QStringLiteral("plugins.thumbnail")) {
        if (!bindLibrary(id)) return;
        formats_->request(id, operation == QStringLiteral("plugins.thumbnail") ? QStringLiteral("thumbnail") : QStringLiteral("viewer"),
            arguments.value(QStringLiteral("item_id")).toString(), arguments.value(QStringLiteral("plugin_id")).toString(),
            calls_[id].libraryRoot, AppInputScope::active());
    } else if (operation == QStringLiteral("plugins.run")) {
        if (!request.value(QStringLiteral("library_root")).toString().isEmpty()) {
            if (!bindLibrary(id)) return;
        } else {
            const QJsonObject info = currentInfo();
            const QString root = info.value(QStringLiteral("ready")).toBool()
                ? info.value(QStringLiteral("library_root")).toString() : QString();
            calls_[id].libraryRoot = root;
            jobs_[id].data.insert(QStringLiteral("library_root"), root);
        }
        plugins_->run(id, arguments.value(QStringLiteral("plugin_id")).toString(),
            arguments.value(QStringLiteral("command")).toString(),
            arguments.value(QStringLiteral("arguments")).toObject(), calls_[id].libraryRoot);
    } else if (operation == QStringLiteral("items.replace_file")) {
        if (!bindLibrary(id)) return;
        replacements_.insert(id, Replacement());
        runHelper(id, {{QStringLiteral("operation"), operation}, {QStringLiteral("arguments"), arguments},
            {QStringLiteral("library_root"), calls_[id].libraryRoot}}, QStringLiteral("replace_preflight"));
    } else if (operation == QStringLiteral("items.import_path") || operation == QStringLiteral("items.import_url")) {
        if (!bindLibrary(id)) return;
        imports_.insert(id, Import());
        beginImport(id);
    } else if (operation == QStringLiteral("video.capture")) {
        const QStringList keys{QStringLiteral("url"), QStringLiteral("page_url"), QStringLiteral("title"), QStringLiteral("media_url")};
        for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
            if (!keys.contains(it.key()) || !it.value().isString() || it.value().toString().size() > 8192 || it.value().toString().contains(QChar(0))) {
                fail(id, QStringLiteral("invalid_arguments"), QStringLiteral("Video capture accepts URL, page URL, title and media URL strings.")); return;
            }
        }
        const QUrl url(arguments.value(QStringLiteral("url")).toString());
        if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
            fail(id, QStringLiteral("invalid_arguments"), QStringLiteral("Video capture requires an absolute HTTP or HTTPS URL.")); return;
        }
        if (!bindLibrary(id)) return;
        auto* mainWindow = qobject_cast<QWidget*>(host_->findObjectByClass(QByteArrayLiteral("CFramelessWindow")));
        if (!mainWindow) { fail(id, QStringLiteral("main_window_unavailable"), QStringLiteral("The Billfish main window is unavailable.")); return; }
        video_->setMainWindow(mainWindow);
        calls_[id].alreadyReplied = true;
        sendSuccess(id, {{QStringLiteral("accepted"), true}, {QStringLiteral("job_id"), id}}, pluginSdk);
        video_->start(id, arguments, calls_[id].libraryRoot);
    } else if (operation == QStringLiteral("items.selected")) {
        if (!arguments.isEmpty()) {
            fail(id, QStringLiteral("invalid_arguments"), QStringLiteral("items.selected accepts an empty arguments object."));
            return;
        }
        if (!bindLibrary(id)) return;
        const QJsonObject selection = native_->selectedItems();
        if (!selection.value(QStringLiteral("available")).toBool()) {
            fail(id, selection.value(QStringLiteral("code")).toString(QStringLiteral("selection_unavailable")),
                selection.value(QStringLiteral("message")).toString(QStringLiteral("The native selection is unavailable.")));
            return;
        }
        if (normalizedRoot(selection.value(QStringLiteral("library_root")).toString()) != normalizedRoot(calls_[id].libraryRoot)) {
            fail(id, QStringLiteral("library_changed"), QStringLiteral("The active library changed while reading the selection."));
            return;
        }
        if (!selection.value(QStringLiteral("item_ids")).isArray()) {
            fail(id, QStringLiteral("selection_unavailable"), QStringLiteral("The native selection did not provide item identifiers."));
            return;
        }
        selectionSnapshots_.insert(id, selection);
        runHelper(id, {{QStringLiteral("operation"), operation},
            {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("item_ids"), selection.value(QStringLiteral("item_ids"))}}},
            {QStringLiteral("library_root"), calls_[id].libraryRoot}}, QStringLiteral("selection"));
    } else if (writeOperation(operation) || readOperation(operation)) {
        if (!bindLibrary(id)) return;
        QJsonObject helperRequest = request;
        helperRequest.insert(QStringLiteral("library_root"), calls_[id].libraryRoot);
        runHelper(id, helperRequest, writeOperation(operation) ? QStringLiteral("preflight") : QStringLiteral("read"));
    } else {
        fail(id, QStringLiteral("unknown_operation"), QStringLiteral("Operation is not supported by this host."));
    }
}

bool AutomationController::bindLibrary(const QString& id) {
    const QString requested = calls_[id].envelope.value(QStringLiteral("library_root")).toString();
    if (calls_[id].pluginSdk && requested.isEmpty()) {
        fail(id, QStringLiteral("library_not_open"),
            QStringLiteral("This plugin run started without an active library. Start a new run to access a library."));
        return false;
    }
    const QJsonObject info = currentInfo();
    if (!info.value(QStringLiteral("ready")).toBool()) {
        fail(id, info.value(QStringLiteral("library_root")).toString().isEmpty()
            ? QStringLiteral("library_not_open") : QStringLiteral("native_not_ready"), info.value(QStringLiteral("reason")).toString(
            QStringLiteral("Billfish has no ready active library."))); return false;
    }
    const QString root = info.value(QStringLiteral("library_root")).toString();
    if (root.isEmpty() || (!requested.isEmpty() && normalizedRoot(root) != normalizedRoot(requested))) {
        fail(id, QStringLiteral("library_mismatch"), QStringLiteral("Requested library is not the active Billfish library.")); return false;
    }
    calls_[id].libraryRoot = root;
    if (jobs_.contains(id)) jobs_[id].data.insert(QStringLiteral("library_root"), root);
    return true;
}

bool AutomationController::importOwnerMatches(const QString& id) {
    if (!calls_.contains(id)) return false;
    const QJsonObject owner = currentInfo();
    if (!calls_.contains(id)) return false;
    if (!owner.value(QStringLiteral("ready")).toBool() ||
        normalizedRoot(owner.value(QStringLiteral("library_root")).toString()) != normalizedRoot(calls_[id].libraryRoot)) {
        fail(id, QStringLiteral("library_changed"), QStringLiteral("The library bound when this import started is no longer active."));
        return false;
    }
    return true;
}

void AutomationController::beginImport(const QString& id) {
    if (!imports_.contains(id) || !calls_.contains(id) || !importOwnerMatches(id)) return;
    const QJsonObject request = calls_[id].envelope;
    const bool fromUrl = request.value(QStringLiteral("operation")).toString() == QStringLiteral("items.import_url");
    QJsonObject arguments = request.value(QStringLiteral("arguments")).toObject();
    if (!fromUrl && calls_[id].pluginSdk && arguments.contains(QStringLiteral("name")))
        arguments.insert(QStringLiteral("name"), fileNameFromTitle(arguments.value(QStringLiteral("name")).toString(),
            QFileInfo(arguments.value(QStringLiteral("source_path")).toString()).suffix()));
    imports_[id].intent = arguments;
    runHelper(id, {{QStringLiteral("operation"), fromUrl ? QStringLiteral("items.import_url") : QStringLiteral("items.import_path")},
        {QStringLiteral("arguments"), arguments},
        {QStringLiteral("library_root"), calls_[id].libraryRoot}},
        fromUrl ? QStringLiteral("import_validate") : QStringLiteral("import_preflight"));
}

void AutomationController::downloadImage(const QString& id, const QJsonObject& validated) {
    if (!imports_.contains(id) || !importOwnerMatches(id)) return;
    QTemporaryDir workspace(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("Billfish-media-XXXXXX")));
    if (!workspace.isValid()) { fail(id, QStringLiteral("temporary_directory_failed"), QStringLiteral("Cannot create a download workspace.")); return; }
    workspace.setAutoRemove(false);
    imports_[id].workspace = workspace.path();
    imports_[id].intent = validated;
    QJsonObject request{{QStringLiteral("mode"), QStringLiteral("download_image")},
        {QStringLiteral("url"), validated.value(QStringLiteral("url"))},
        {QStringLiteral("workspace"), workspace.path()}};
    const QUrl referer(validated.value(QStringLiteral("origin")).toString());
    if (referer.scheme() == QStringLiteral("http") || referer.scheme() == QStringLiteral("https"))
        request.insert(QStringLiteral("referer"), referer.toString());
    startMediaTransfer(id, request, false);
}

void AutomationController::startMediaTransfer(const QString& id, const QJsonObject& request, bool video) {
    if (!calls_.contains(id)) return;
    auto* transfer = new MediaTransferProcess(this);
    if (video) {
        if (videoTransfers_.value(id) && videoTransfers_.value(id)->isRunning()) {
            transfer->deleteLater();
            videoTransfers_.value(id)->cancel();
            return;
        }
        videoTransfers_.insert(id, transfer);
    } else {
        if (!imports_.contains(id)) { transfer->deleteLater(); return; }
        imports_[id].transfer = transfer;
    }
    connect(transfer, &MediaTransferProcess::progress, this, [this, id, video](const QJsonObject& event) {
        const bool measurable = event.value(QStringLiteral("percent")).isDouble();
        const int percent = static_cast<int>(qBound(0.0, event.value(QStringLiteral("percent")).toDouble(), 100.0));
        const QString message = event.value(QStringLiteral("message")).toString(event.value(QStringLiteral("stage")).toString());
        if (video) video_->transferProgress(id, measurable ? percent : 0, measurable ? 100 : 0, message);
        else progress(id, measurable ? percent : 0, measurable ? 100 : 0, message);
    });
    connect(transfer, &MediaTransferProcess::finished, this, [this, id, video, transfer](const QJsonObject& data) {
        if (video) {
            if (videoTransfers_.value(id).data() == transfer) videoTransfers_.remove(id);
            transfer->deleteLater();
            video_->transferFinished(id, data);
            return;
        }
        if (!imports_.contains(id) || !calls_.contains(id)) { transfer->deleteLater(); return; }
        imports_[id].transfer.clear();
        transfer->deleteLater();
        if (!imports_[id].abortCode.isEmpty()) {
            fail(id, imports_[id].abortCode, imports_[id].abortMessage, imports_[id].abortDetails); return;
        }
        if (!importOwnerMatches(id)) return;
        const QString path = QFileInfo(data.value(QStringLiteral("path")).toString()).absoluteFilePath();
        const QString prefix = QDir::cleanPath(imports_[id].workspace) + QLatin1Char('/');
        if (!QDir::fromNativeSeparators(path).startsWith(prefix, Qt::CaseInsensitive)) {
            fail(id, QStringLiteral("media_transfer_protocol_error"), QStringLiteral("Downloaded file is outside the owned workspace.")); return;
        }
        const QJsonObject validated = imports_[id].intent;
        QJsonObject arguments{{QStringLiteral("source_path"), path},
            {QStringLiteral("folder_id"), QString::number(validated.value(QStringLiteral("target_folder_id")).toInt())},
            {QStringLiteral("origin"), validated.value(QStringLiteral("origin"))}};
        if (validated.contains(QStringLiteral("name"))) {
            const QString name = validated.value(QStringLiteral("name")).toString();
            arguments.insert(QStringLiteral("name"), calls_[id].pluginSdk ?
                fileNameFromTitle(name, QFileInfo(path).suffix()) : name);
        }
        runHelper(id, {{QStringLiteral("operation"), QStringLiteral("items.import_path")},
            {QStringLiteral("arguments"), arguments}, {QStringLiteral("library_root"), calls_[id].libraryRoot}}, QStringLiteral("import_preflight"));
    });
    connect(transfer, &MediaTransferProcess::failed, this,
        [this, id, video, transfer](const QString& code, const QString& message, const QJsonObject& details) {
            if (video) {
                if (videoTransfers_.value(id).data() == transfer) videoTransfers_.remove(id);
                transfer->deleteLater(); video_->transferFailed(id, code, message, details); return;
            }
            if (imports_.contains(id)) imports_[id].transfer.clear();
            transfer->deleteLater(); fail(id, code, message, details);
        });
    const int timeout = request.value(QStringLiteral("mode")).toString() == QStringLiteral("download_video") ? 60 * 60 * 1000 : 120000;
    transfer->start(request, timeout);
}

void AutomationController::submitImport(const QString& id, const QJsonObject& intent) {
    if (!imports_.contains(id) || !calls_.contains(id) || !importOwnerMatches(id)) return;
    imports_[id].intent = intent;
    imports_[id].nativePending = true;
    imports_[id].sourceSafe = false;
    progress(id, 0, 0, QStringLiteral("正在导入素材"));
    native_->importFile(id, {{QStringLiteral("library_root"), calls_[id].libraryRoot},
        {QStringLiteral("path"), intent.value(QStringLiteral("path"))},
        {QStringLiteral("target_folder_id"), QString::number(intent.value(QStringLiteral("target_folder_id")).toInt())},
        {QStringLiteral("name"), intent.value(QStringLiteral("name"))},
        {QStringLiteral("origin"), intent.value(QStringLiteral("origin"))}});
}

void AutomationController::continuePreparedFolder(const QString& id) {
    if (!imports_.contains(id) || !calls_.contains(id) || !importOwnerMatches(id)) return;
    Import& import = imports_[id];
    if (import.waitingForFolder && import.folderDeadline.hasExpired()) {
        fail(id, QStringLiteral("browser_folder_visibility_timeout"),
            QStringLiteral("The native browser capture folder was not visible to a fresh catalog read within five seconds."));
        return;
    }
    ++import.folderReadAttempts;
    const int timeout = import.waitingForFolder
        ? qMax(1, static_cast<int>(qMin<qint64>(30000, import.folderDeadline.remainingTime()))) : 30000;
    runHelper(id, {{QStringLiteral("operation"), QStringLiteral("items.import_path")},
        {QStringLiteral("arguments"), import.folderArguments}, {QStringLiteral("library_root"), calls_[id].libraryRoot}},
        QStringLiteral("import_preflight"), timeout);
}

QJsonObject AutomationController::releaseImport(const QString& id) {
    if (!imports_.contains(id)) return {};
    const Import import = imports_.take(id);
    importHelpers_.remove(id);
    QJsonObject details = import.nativeResult;
    if (!import.preparedFolder.isEmpty()) details.insert(QStringLiteral("prepared_folder"), import.preparedFolder);
    details.insert(QStringLiteral("source_safe_to_delete"), import.sourceSafe && !import.nativePending);
    if (!import.workspace.isEmpty()) {
        if (!import.sourceSafe || import.nativePending || !QDir(import.workspace).removeRecursively())
            details.insert(QStringLiteral("temporary_workspace_retained"), import.workspace);
    }
    return details;
}

bool AutomationController::replacementOwnerMatches(const QString& id) {
    if (!calls_.contains(id) || !replacements_.contains(id)) return false;
    const QJsonObject owner = currentInfo();
    if (!calls_.contains(id) || !replacements_.contains(id)) return false;
    if (!owner.value(QStringLiteral("ready")).toBool() ||
        normalizedRoot(owner.value(QStringLiteral("library_root")).toString()) != normalizedRoot(calls_[id].libraryRoot)) {
        fail(id, QStringLiteral("library_changed"), QStringLiteral("The bound library changed during image replacement."));
        return false;
    }
    return true;
}

void AutomationController::verifyReplacement(const QString& id) {
    if (!replacementOwnerMatches(id)) return;
    if (replacements_[id].deadline.hasExpired()) {
        fail(id, QStringLiteral("replacement_verification_timeout"),
            QStringLiteral("The file was replaced, but native analysis did not finish within the deadline. No rollback or replay was performed."));
        return;
    }
    runHelper(id, {{QStringLiteral("operation"), QStringLiteral("items.replace_file")},
        {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("intent"), replacements_[id].intent}}},
        {QStringLiteral("library_root"), calls_[id].libraryRoot}}, QStringLiteral("replace_verify"),
        static_cast<int>(qMin<qint64>(30000, replacements_[id].deadline.remainingTime())));
}

void AutomationController::runHelper(const QString& id, const QJsonObject& request, const QString& mode, int timeoutMs) {
    QString python = qEnvironmentVariable("BILLFISH_PYTHON");
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (python.isEmpty()) { fail(id, QStringLiteral("python_unavailable"), QStringLiteral("Python is unavailable.")); return; }
    auto* process = new QProcess(this);
    if (mode == QStringLiteral("verify") || mode == QStringLiteral("import_verify")) verificationProcesses_.insert(id, process);
    if (importHelper(mode)) importHelpers_.insert(id, process);
    if (replacementHelper(mode)) {
        replacementHelpers_.insert(id, process);
        if (replacements_.contains(id)) replacements_[id].helperMode = mode;
    }
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
    const auto output = QSharedPointer<HelperOutput>::create();
    auto* timeout = new QTimer(process);
    timeout->setSingleShot(true);
    timeout->setTimerType(Qt::PreciseTimer);
    const QJsonObject input{{QStringLiteral("mode"), mode},
        {QStringLiteral("operation"), request.value(QStringLiteral("operation"))},
        {QStringLiteral("arguments"), request.value(QStringLiteral("arguments"))},
        {QStringLiteral("library_root"), request.value(QStringLiteral("library_root"))}};
    connect(process, &QProcess::started, this, [this, id, mode, process, input] {
        if (replacementHelper(mode) && !replacementOwnerMatches(id)) { process->kill(); return; }
        process->write(QJsonDocument(input).toJson(QJsonDocument::Compact)); process->closeWriteChannel();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, id, mode, process, output] {
        output->output.append(process->readAllStandardOutput());
        if (output->output.size() > 16 * 1024 * 1024 && !output->settled) {
            output->settled = true; process->kill();
            fail(id, mode == QStringLiteral("selection") ? QStringLiteral("selection_too_large") : QStringLiteral("helper_output_too_large"),
                mode == QStringLiteral("selection") ? QStringLiteral("Selected item metadata exceeds the 16 MiB response limit.") : QStringLiteral("Helper output exceeds 16 MiB."));
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(timeout, &QTimer::timeout, this, [this, id, process, output] {
        if (output->settled) return;
        output->settled = true; process->kill();
        fail(id, QStringLiteral("helper_timeout"), QStringLiteral("Metadata helper exceeded its deadline."));
    });
    connect(process, &QProcess::errorOccurred, this, [this, id, process, output](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || output->settled) return;
        output->settled = true;
        fail(id, QStringLiteral("helper_start_failed"), process->errorString()); process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, id, request, mode, process, timeout, output](int exitCode, QProcess::ExitStatus status) {
            timeout->stop();
            if (verificationProcesses_.value(id).data() == process) verificationProcesses_.remove(id);
            if (importHelpers_.value(id).data() == process) importHelpers_.remove(id);
            if (replacementHelpers_.value(id).data() == process) replacementHelpers_.remove(id);
            output->output.append(process->readAllStandardOutput());
            process->readAllStandardError();
            process->deleteLater();
            if (importHelper(mode) && imports_.contains(id) && !imports_[id].abortCode.isEmpty() && calls_.contains(id)) {
                output->settled = true;
                fail(id, imports_[id].abortCode, imports_[id].abortMessage, imports_[id].abortDetails);
                return;
            }
            if (replacementHelper(mode) && replacements_.contains(id) && !replacements_[id].abortCode.isEmpty() && calls_.contains(id)) {
                output->settled = true;
                fail(id, replacements_[id].abortCode, replacements_[id].abortMessage, replacements_[id].abortDetails);
                return;
            }
            if (output->settled || !calls_.contains(id)) return;
            output->settled = true;
            if (output->output.size() > 16 * 1024 * 1024) {
                fail(id, mode == QStringLiteral("selection") ? QStringLiteral("selection_too_large") : QStringLiteral("helper_output_too_large"),
                    mode == QStringLiteral("selection") ? QStringLiteral("Selected item metadata exceeds the 16 MiB response limit.") : QStringLiteral("Helper output exceeds 16 MiB.")); return;
            }
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(output->output, &error);
            if (status != QProcess::NormalExit || error.error != QJsonParseError::NoError || !document.isObject()) {
                fail(id, QStringLiteral("helper_failed"), QStringLiteral("Metadata helper did not return a valid response.")); return;
            }
            const QJsonObject response = document.object();
            if (!response.value(QStringLiteral("ok")).toBool() || exitCode != 0) {
                const auto failure = response.value(QStringLiteral("error")).toObject();
                if (mode == QStringLiteral("import_preflight") && imports_.contains(id) && imports_[id].waitingForFolder) {
                    Import& import = imports_[id];
                    const QString code = failure.value(QStringLiteral("code")).toString();
                    const QString actualId = failure.value(QStringLiteral("details")).toObject().value(QStringLiteral("folder_id")).toString();
                    const QString expectedId = import.preparedFolder.value(QStringLiteral("target_folder_id")).toString();
                    if ((code == QStringLiteral("folder_not_found") || code == QStringLiteral("folder_not_writable")) &&
                        !expectedId.isEmpty() && actualId == expectedId) {
                        import.folderLastError = failure;
                        if (import.folderDeadline.hasExpired()) {
                            fail(id, QStringLiteral("browser_folder_visibility_timeout"),
                                QStringLiteral("The native browser capture folder was not visible to a fresh catalog read within five seconds."));
                        } else {
                            const int delay = qMax(1, static_cast<int>(qMin<qint64>(150, import.folderDeadline.remainingTime())));
                            QTimer::singleShot(delay, Qt::PreciseTimer, this, [this, id] { continuePreparedFolder(id); });
                        }
                        return;
                    }
                }
                fail(id, failure.value(QStringLiteral("code")).toString(QStringLiteral("helper_failed")),
                    failure.value(QStringLiteral("message")).toString(QStringLiteral("Metadata helper rejected the request.")),
                    failure.value(QStringLiteral("details")).toObject()); return;
            }
            const QJsonObject data = response.value(QStringLiteral("data")).toObject();
            if (mode == QStringLiteral("replace_preflight")) {
                if (!replacementOwnerMatches(id)) return;
                replacements_[id].intent = data;
                if (data.value(QStringLiteral("dry_run")).toBool(true)) {
                    QJsonObject result = data;
                    result.insert(QStringLiteral("modified"), 0);
                    succeed(id, result); return;
                }
                replacements_[id].commitState = QStringLiteral("in_progress");
                runHelper(id, {{QStringLiteral("operation"), QStringLiteral("items.replace_file")},
                    {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("intent"), data}}},
                    {QStringLiteral("library_root"), calls_[id].libraryRoot}}, QStringLiteral("replace_commit"), 120000);
                return;
            }
            if (mode == QStringLiteral("replace_commit")) {
                if (!replacements_.contains(id)) return;
                replacements_[id].intent = data;
                replacements_[id].commitState = data.value(QStringLiteral("file_replaced")).toBool()
                    ? QStringLiteral("replaced") : QStringLiteral("unchanged");
                replacements_[id].deadline = QDeadlineTimer(30000, Qt::PreciseTimer);
                verifyReplacement(id); return;
            }
            if (mode == QStringLiteral("replace_verify")) {
                if (!replacementOwnerMatches(id)) return;
                replacements_[id].lastActual = data;
                if (data.value(QStringLiteral("verified")).toBool() && data.value(QStringLiteral("analysis_ready")).toBool()) {
                    succeed(id, data); return;
                }
                if (!data.value(QStringLiteral("retryable")).toBool()) {
                    fail(id, QStringLiteral("replacement_verification_failed"),
                        QStringLiteral("The replaced item's identity or content changed before verification. No rollback or replay was performed."));
                    return;
                }
                QTimer::singleShot(150, Qt::PreciseTimer, this, [this, id] { verifyReplacement(id); });
                return;
            }
            if (mode == QStringLiteral("verify") || mode == QStringLiteral("import_verify")) { acceptVerification(id, data); return; }
            if (mode == QStringLiteral("import_validate")) { downloadImage(id, data); return; }
            if (mode == QStringLiteral("import_preflight")) {
                if (imports_.contains(id) && imports_[id].waitingForFolder && imports_[id].folderDeadline.hasExpired()) {
                    fail(id, QStringLiteral("browser_folder_visibility_timeout"),
                        QStringLiteral("The native browser capture folder was not visible to a fresh catalog read within five seconds."));
                    return;
                }
                if (imports_.contains(id)) imports_[id].waitingForFolder = false;
                submitImport(id, data); return;
            }
            if (mode == QStringLiteral("browser_destination")) {
                if (!imports_.contains(id) || !importOwnerMatches(id)) return;
                const QJsonObject target = native_->prepareBrowserCaptureFolder(calls_[id].libraryRoot, data);
                if (!target.value(QStringLiteral("ok")).toBool()) {
                    fail(id, target.value(QStringLiteral("code")).toString(QStringLiteral("browser_destination_unavailable")),
                        target.value(QStringLiteral("message")).toString(QStringLiteral("The browser capture folder could not be prepared.")),
                        target.value(QStringLiteral("details")).toObject()); return;
                }
                if (!importOwnerMatches(id)) return;
                QJsonObject arguments = imports_[id].intent;
                arguments.insert(QStringLiteral("folder_id"), target.value(QStringLiteral("target_folder_id")));
                Import& import = imports_[id];
                import.preparedFolder = target;
                import.folderArguments = arguments;
                import.waitingForFolder = target.value(QStringLiteral("created")).toBool() ||
                    target.value(QStringLiteral("bound")).toBool() || data.value(QStringLiteral("needs_binding")).toBool();
                if (import.waitingForFolder) {
                    import.folderDeadline = QDeadlineTimer(5000, Qt::PreciseTimer);
                    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this, id] {
                        if (imports_.contains(id) && imports_[id].waitingForFolder && imports_[id].folderDeadline.hasExpired())
                            fail(id, QStringLiteral("browser_folder_visibility_timeout"),
                                QStringLiteral("The native browser capture folder was not visible to a fresh catalog read within five seconds."));
                    });
                }
                continuePreparedFolder(id);
                return;
            }
            if (mode == QStringLiteral("selection")) {
                const QJsonObject owner = currentInfo();
                if (!owner.value(QStringLiteral("ready")).toBool() ||
                    normalizedRoot(owner.value(QStringLiteral("library_root")).toString()) != normalizedRoot(calls_[id].libraryRoot)) {
                    fail(id, QStringLiteral("library_changed"), QStringLiteral("The active library changed while resolving the selection."));
                    return;
                }
                QJsonObject result = data;
                const QJsonObject selection = selectionSnapshots_.take(id);
                result.insert(QStringLiteral("source"), selection.value(QStringLiteral("source")));
                result.insert(QStringLiteral("item_ids"), selection.value(QStringLiteral("item_ids")));
                const QJsonObject envelope{{QStringLiteral("id"), id}, {QStringLiteral("ok"), true},
                    {QStringLiteral("data"), result}};
                if (QJsonDocument(envelope).toJson(QJsonDocument::Compact).size() + 1 > 16 * 1024 * 1024) {
                    fail(id, QStringLiteral("selection_too_large"),
                        QStringLiteral("Selected item metadata exceeds the 16 MiB response limit."));
                    return;
                }
                succeed(id, result);
                return;
            }
            if (mode == QStringLiteral("read")) { succeed(id, data); return; }
            const bool dryRun = request.value(QStringLiteral("arguments")).toObject().value(QStringLiteral("dry_run")).toBool(true);
            if (dryRun || data.value(QStringLiteral("modified_count")).toInt() == 0) {
                QJsonObject result = data;
                result.insert(QStringLiteral("dry_run"), dryRun);
                result.insert(QStringLiteral("modified"), dryRun ? 0 : data.value(QStringLiteral("modified_count")).toInt());
                succeed(id, result); return;
            }
            QJsonObject arguments = data.value(QStringLiteral("arguments")).toObject();
            arguments.insert(QStringLiteral("dry_run"), false);
            QJsonObject nativeRequest{{QStringLiteral("operation"), data.value(QStringLiteral("operation"))},
                {QStringLiteral("arguments"), arguments}, {QStringLiteral("library_root"), calls_[id].libraryRoot},
                {QStringLiteral("preflight"), data}};
            progress(id, 0, data.value(QStringLiteral("modified_count")).toInt(), QStringLiteral("正在同步素材信息"));
            native_->execute(id, nativeRequest);
        });
    progress(id, 0, 0, (mode == QStringLiteral("verify") || mode == QStringLiteral("import_verify") || mode == QStringLiteral("replace_verify")) ? QStringLiteral("正在确认素材信息已保存") :
        (mode == QStringLiteral("preflight") || importHelper(mode)) ? QStringLiteral("正在检查修改计划") : QStringLiteral("正在读取素材库"));
    const QString helper = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("automation/billfish_host_helper.py"));
    process->start(python, {QStringLiteral("-I"), helper});
    timeout->start(timeoutMs);
}

void AutomationController::nativeFinished(const QString& id, const QJsonObject& data) {
    if (!calls_.contains(id)) return;
    const QString operation = calls_[id].envelope.value(QStringLiteral("operation")).toString();
    if (imports_.contains(id)) {
        imports_[id].nativePending = false;
        imports_[id].sourceSafe = data.value(QStringLiteral("source_safe_to_delete")).toBool();
        imports_[id].nativeResult = data;
        if (!imports_[id].abortCode.isEmpty()) {
            fail(id, imports_[id].abortCode, imports_[id].abortMessage, imports_[id].abortDetails);
            return;
        }
        if (!imports_[id].sourceSafe) {
            fail(id, QStringLiteral("native_import_not_drained"), QStringLiteral("The native importer has not confirmed its source lease is released."), data);
            return;
        }
        if (!data.value(QStringLiteral("item_ids")).isArray() || data.value(QStringLiteral("item_ids")).toArray().size() != 1) {
            fail(id, QStringLiteral("native_import_result_invalid"), QStringLiteral("The native importer did not return exactly one real item identifier."), data);
            return;
        }
    }
    if ((!writeOperation(operation) && !importOperation(operation)) || data.value(QStringLiteral("failed_steps")).toInt() > 0 ||
        data.value(QStringLiteral("status")).toString() == QStringLiteral("partial_failure")) {
        succeed(id, data); return;
    }
    verifications_.insert(id, Verification{data, {}, QDeadlineTimer(5000, Qt::PreciseTimer), 0});
    if (jobs_.contains(id)) jobs_[id].data.insert(QStringLiteral("stage"), QStringLiteral("verifying"));
    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this, id] {
        if (verifications_.contains(id) && verifications_[id].deadline.hasExpired())
            fail(id, QStringLiteral("verification_timeout"),
                QStringLiteral("Persisted metadata was not confirmed within five seconds."));
    });
    startVerification(id);
}

void AutomationController::startVerification(const QString& id) {
    if (!calls_.contains(id) || !verifications_.contains(id)) return;
    if (verifications_[id].deadline.hasExpired()) {
        fail(id, QStringLiteral("verification_timeout"),
            QStringLiteral("Persisted metadata was not confirmed within five seconds."));
        return;
    }
    const QJsonObject info = currentInfo();
    if (!calls_.contains(id) || !verifications_.contains(id)) return;
    if (!info.value(QStringLiteral("ready")).toBool() ||
        normalizedRoot(info.value(QStringLiteral("library_root")).toString()) != normalizedRoot(calls_[id].libraryRoot)) {
        fail(id, QStringLiteral("library_changed"),
            QStringLiteral("The active library changed before metadata verification. Completed native changes remain applied."));
        return;
    }
    Verification& verification = verifications_[id];
    ++verification.attempts;
    const QJsonObject& request = calls_[id].envelope;
    if (imports_.contains(id)) {
        const QJsonObject intent = imports_[id].intent;
        const QJsonObject arguments{{QStringLiteral("item_ids"), verification.nativeResult.value(QStringLiteral("item_ids"))},
            {QStringLiteral("path"), intent.value(QStringLiteral("path"))},
            {QStringLiteral("extension"), intent.value(QStringLiteral("extension"))},
            {QStringLiteral("expected_name"), intent.value(QStringLiteral("expected_name"))},
            {QStringLiteral("origin"), intent.value(QStringLiteral("origin"))},
            {QStringLiteral("folder_id"), QString::number(intent.value(QStringLiteral("target_folder_id")).toInt())}};
        runHelper(id, {{QStringLiteral("operation"), QStringLiteral("items.import_path")},
            {QStringLiteral("arguments"), arguments}, {QStringLiteral("library_root"), calls_[id].libraryRoot}},
            QStringLiteral("import_verify"), qMax(1, static_cast<int>(qMin<qint64>(30000, verification.deadline.remainingTime()))));
        return;
    }
    runHelper(id, {{QStringLiteral("operation"), request.value(QStringLiteral("operation"))},
        {QStringLiteral("arguments"), request.value(QStringLiteral("arguments"))},
        {QStringLiteral("library_root"), calls_[id].libraryRoot}}, QStringLiteral("verify"),
        qMax(1, static_cast<int>(qMin<qint64>(30000, verification.deadline.remainingTime()))));
}

void AutomationController::acceptVerification(const QString& id, const QJsonObject& data) {
    if (!calls_.contains(id) || !verifications_.contains(id)) return;
    verifications_[id].lastActual = data;
    if (verifications_[id].deadline.hasExpired()) {
        fail(id, QStringLiteral("verification_timeout"),
            QStringLiteral("Persisted metadata was not confirmed within five seconds."));
        return;
    }
    const QJsonObject owner = currentInfo();
    if (!calls_.contains(id) || !verifications_.contains(id)) return;
    if (!owner.value(QStringLiteral("ready")).toBool() ||
        normalizedRoot(owner.value(QStringLiteral("library_root")).toString()) != normalizedRoot(calls_[id].libraryRoot)) {
        fail(id, QStringLiteral("library_changed"),
            QStringLiteral("The active library changed during metadata verification. Completed native changes remain applied."));
        return;
    }
    const Verification verification = verifications_[id];
    if (data.value(QStringLiteral("verified")).toBool()) {
        QJsonObject result = verification.nativeResult;
        result.insert(QStringLiteral("native_completion"), result.value(QStringLiteral("completion")));
        result.insert(QStringLiteral("completion"), imports_.contains(id) ? QStringLiteral("native_import_persisted") : QStringLiteral("persisted_metadata_verified"));
        result.insert(QStringLiteral("verification_count"), verification.attempts);
        result.insert(QStringLiteral("verification"), QJsonObject{{QStringLiteral("verified"), true},
            {QStringLiteral("scope"), QStringLiteral("requested_main_catalog_metadata")}});
        if (data.value(QStringLiteral("tag")).isObject()) result.insert(QStringLiteral("tag"), data.value(QStringLiteral("tag")));
        if (imports_.contains(id)) {
            const QJsonArray identifiers = result.value(QStringLiteral("item_ids")).toArray();
            result.insert(QStringLiteral("item_id"), identifiers.at(0));
            result.insert(QStringLiteral("item"), data.value(QStringLiteral("item")));
            result.insert(QStringLiteral("analysis_ready"), data.value(QStringLiteral("analysis_ready")).toBool());
            result.insert(QStringLiteral("source_safe_to_delete"), imports_[id].sourceSafe);
        }
        verifications_.remove(id);
        result.insert(QStringLiteral("search_refresh"), native_->refreshCurrentSearch());
        succeed(id, result);
        return;
    }
    if (!data.value(QStringLiteral("retryable")).toBool()) {
        fail(id, QStringLiteral("verification_failed"), QStringLiteral("Persisted metadata could not be verified unambiguously."));
        return;
    }
    const int delay = qMax(1, static_cast<int>(qMin<qint64>(150, verification.deadline.remainingTime())));
    QTimer::singleShot(delay, Qt::PreciseTimer, this, [this, id] { startVerification(id); });
}

void AutomationController::ensureUiJob(const QString& id) {
    if (calls_.contains(id) || jobs_.contains(id)) return;
    const bool installation = id.startsWith(QStringLiteral("plugin-install-"));
    const QString operation = installation ? QStringLiteral("plugins.install") :
        (id.startsWith(QStringLiteral("format-open-")) ? QStringLiteral("plugins.preview") : QStringLiteral("plugins.run"));
    const QJsonObject request{{QStringLiteral("operation"), operation},
        {QStringLiteral("arguments"), QJsonObject()}};
    const QString root = installation ? QString() : currentInfo().value(QStringLiteral("library_root")).toString();
    calls_.insert(id, Call{request, root, false, true});
    jobs_.insert(id, Job{{}, {{QStringLiteral("id"), id}, {QStringLiteral("job_id"), id},
        {QStringLiteral("operation"), operation}, {QStringLiteral("state"), QStringLiteral("running")},
        {QStringLiteral("library_root"), root}, {QStringLiteral("completed"), 0}, {QStringLiteral("total"), 0},
        {QStringLiteral("message"), installation ? QStringLiteral("正在安装插件") : QStringLiteral("正在运行插件")}}});
}

void AutomationController::progress(const QString& id, int completed, int total, const QString& message) {
    if (!jobs_.contains(id)) return;
    auto& data = jobs_[id].data;
    data.insert(QStringLiteral("state"), QStringLiteral("running"));
    data.insert(QStringLiteral("completed"), completed);
    data.insert(QStringLiteral("total"), total);
    data.insert(QStringLiteral("message"), message);
    if (!calls_.value(id).pluginSdk) ui_->updateProgress(id, completed, total, message);
}

void AutomationController::succeed(const QString& id, QJsonObject data) {
    if (!calls_.contains(id)) return;
    replacements_.remove(id);
    replacementHelpers_.remove(id);
    if (imports_.contains(id)) {
        const bool videoCapture = imports_[id].videoCapture;
        const QJsonObject released = releaseImport(id);
        data.insert(QStringLiteral("source_safe_to_delete"), released.value(QStringLiteral("source_safe_to_delete")));
        if (released.contains(QStringLiteral("temporary_workspace_retained")))
            data.insert(QStringLiteral("temporary_workspace_retained"), released.value(QStringLiteral("temporary_workspace_retained")));
        if (videoCapture && video_->has(id)) { video_->finishImport(id, data); return; }
    }
    const Call call = calls_.take(id);
    const bool partialFailure = data.value(QStringLiteral("status")).toString() == QStringLiteral("partial_failure") ||
        data.value(QStringLiteral("failed_steps")).toInt() > 0;
    const QString completion = data.value(QStringLiteral("completion")).toString();
    const bool windowOpened = completion == QStringLiteral("plugin_window_opened");
    const bool serviceStarted = completion == QStringLiteral("plugin_service_started");
    const bool pluginInstalled = completion == QStringLiteral("plugin_installed");
    const bool pluginUpdated = completion == QStringLiteral("plugin_updated");
    if (call.job && jobs_.contains(id)) {
        data.insert(QStringLiteral("job_id"), id);
        data.insert(QStringLiteral("source"), QStringLiteral("online"));
        if (!data.contains(QStringLiteral("library_root"))) data.insert(QStringLiteral("library_root"), call.libraryRoot);
        auto& job = jobs_[id].data;
        if ((pluginInstalled || pluginUpdated) && data.value(QStringLiteral("operation")).toString() == QStringLiteral("plugins.install"))
            job.insert(QStringLiteral("operation"), QStringLiteral("plugins.install"));
        job.insert(QStringLiteral("state"), partialFailure ? QStringLiteral("failed") : QStringLiteral("completed"));
        job.insert(QStringLiteral("stage"), partialFailure ? QStringLiteral("failed") :
            windowOpened ? QStringLiteral("window_opened") : serviceStarted ? QStringLiteral("service_started") : QStringLiteral("completed"));
        job.insert(QStringLiteral("completed"), job.value(QStringLiteral("total")));
        const QString message = partialFailure ? QStringLiteral("任务部分失败, 请查看结果") :
            windowOpened ? QStringLiteral("插件窗口已打开") : serviceStarted ? QStringLiteral("插件服务已启动") :
            pluginInstalled ? QStringLiteral("插件已安装") : pluginUpdated ? QStringLiteral("插件已更新") : QStringLiteral("任务已完成");
        job.insert(QStringLiteral("message"), message);
        if (partialFailure) job.insert(QStringLiteral("error"), QJsonObject{
            {QStringLiteral("code"), QStringLiteral("partial_failure")}, {QStringLiteral("message"), message}});
        job.insert(QStringLiteral("result"), data);
        if (!call.pluginSdk) ui_->finishTask(id, !partialFailure, message);
        retainCompleted(id);
    }
    if (call.alreadyReplied) return;
    if (partialFailure) sendError(id, QStringLiteral("partial_failure"),
        QStringLiteral("Some native actions failed; completed changes remain applied."), call.pluginSdk,
        {{QStringLiteral("job_id"), id}, {QStringLiteral("result"), data}});
    else sendSuccess(id, data, call.pluginSdk);
}

void AutomationController::fail(const QString& id, const QString& code, const QString& message, const QJsonObject& suppliedDetails) {
    if (!calls_.contains(id)) return;
    QString errorCode = code.toLower();
    QString errorMessage = message;
    QJsonObject details = suppliedDetails;
    if (replacements_.contains(id)) {
        Replacement& replacement = replacements_[id];
        if (!replacement.abortCode.isEmpty()) {
            errorCode = replacement.abortCode;
            errorMessage = replacement.abortMessage;
            for (auto it = replacement.abortDetails.constBegin(); it != replacement.abortDetails.constEnd(); ++it)
                if (!details.contains(it.key())) details.insert(it.key(), it.value());
        }
        const auto process = replacementHelpers_.value(id);
        if (process && process->state() != QProcess::NotRunning) {
            // A killed preflight may still have a decoder child reading its input.
            if (replacement.helperMode == QStringLiteral("replace_preflight")) replacement.sourceSafe = false;
            replacement.abortCode = errorCode;
            replacement.abortMessage = errorMessage;
            replacement.abortDetails = details;
            progress(id, 0, 0, QStringLiteral("正在结束图片替换任务"));
            process->kill();
            return;
        }
        const Replacement actual = replacements_.take(id);
        replacementHelpers_.remove(id);
        if (!details.contains(QStringLiteral("file_replaced"))) {
            details.insert(QStringLiteral("file_replaced"), actual.commitState == QStringLiteral("in_progress") ? QJsonValue(QJsonValue::Null) :
                QJsonValue(actual.commitState == QStringLiteral("replaced")));
        }
        if (!details.contains(QStringLiteral("source_safe_to_delete")))
            details.insert(QStringLiteral("source_safe_to_delete"), actual.sourceSafe);
        details.insert(QStringLiteral("rollback"), false);
        if (!actual.intent.isEmpty()) {
            details.insert(QStringLiteral("item_id"), actual.intent.value(QStringLiteral("item_id")));
            details.insert(QStringLiteral("backup_path"), actual.intent.value(QStringLiteral("backup_path")));
            details.insert(QStringLiteral("recovery_manifest"), QDir(actual.intent.value(QStringLiteral("workspace")).toString()).filePath(QStringLiteral("state.json")));
        }
        if (!actual.lastActual.isEmpty()) details.insert(QStringLiteral("last_actual"), actual.lastActual);
        if (jobs_.contains(id)) jobs_[id].data.insert(QStringLiteral("result"), details);
    }
    if (imports_.contains(id)) {
        Import& import = imports_[id];
        if (import.waitingForFolder) {
            if (errorCode == QStringLiteral("helper_timeout")) errorCode = QStringLiteral("browser_folder_visibility_timeout");
            details.insert(QStringLiteral("browser_folder_verification"), QJsonObject{
                {QStringLiteral("target_folder_id"), import.preparedFolder.value(QStringLiteral("target_folder_id"))},
                {QStringLiteral("attempts"), import.folderReadAttempts}, {QStringLiteral("last_error"), import.folderLastError}});
        }
        if (!import.abortCode.isEmpty()) {
            errorCode = import.abortCode;
            errorMessage = import.abortMessage;
            for (auto it = import.abortDetails.constBegin(); it != import.abortDetails.constEnd(); ++it)
                if (!details.contains(it.key())) details.insert(it.key(), it.value());
        }
        const auto process = importHelpers_.value(id);
        if ((import.transfer && import.transfer->isRunning()) ||
            (process && process->state() != QProcess::NotRunning) || import.nativePending) {
            import.abortCode = errorCode;
            import.abortMessage = errorMessage;
            import.abortDetails = details;
            if (jobs_.contains(id)) jobs_[id].data.insert(QStringLiteral("stage"), QStringLiteral("draining"));
            progress(id, 0, 0, QStringLiteral("正在结束导入任务"));
            if (import.transfer && import.transfer->isRunning()) { import.transfer->cancel(); return; }
            if (process && process->state() != QProcess::NotRunning) { process->kill(); return; }
            native_->cancelImport(id);
            return;
        }
        const bool videoCapture = import.videoCapture;
        const QJsonObject released = releaseImport(id);
        for (auto it = released.constBegin(); it != released.constEnd(); ++it)
            if (!details.contains(it.key())) details.insert(it.key(), it.value());
        details.insert(QStringLiteral("source_safe_to_delete"), released.value(QStringLiteral("source_safe_to_delete")));
        if (videoCapture && video_->has(id)) { video_->failImport(id, errorCode, errorMessage, details); return; }
    }
    const Call call = calls_.take(id);
    selectionSnapshots_.remove(id);
    if (!suppliedDetails.isEmpty() && jobs_.contains(id)) jobs_[id].data.insert(QStringLiteral("result"), suppliedDetails);
    if (verifications_.contains(id)) {
        const Verification verification = verifications_.take(id);
        const auto process = verificationProcesses_.take(id);
        if (process && process->state() != QProcess::NotRunning) process->kill();
        if (errorCode != QStringLiteral("library_changed") &&
            (errorCode == QStringLiteral("helper_timeout") || verification.deadline.hasExpired()))
            errorCode = QStringLiteral("verification_timeout");
        else if (errorCode != QStringLiteral("library_changed")) errorCode = QStringLiteral("verification_failed");
        errorMessage = QStringLiteral("Native changes were submitted, but persisted metadata verification did not finish. No rollback was performed. ") + message;
        details.insert(QStringLiteral("native_result"), verification.nativeResult);
        details.insert(QStringLiteral("expected"), QJsonObject{
            {QStringLiteral("operation"), call.envelope.value(QStringLiteral("operation"))},
            {QStringLiteral("arguments"), call.envelope.value(QStringLiteral("arguments"))},
            {QStringLiteral("library_root"), call.libraryRoot}});
        details.insert(QStringLiteral("last_actual"), verification.lastActual);
        details.insert(QStringLiteral("verification_count"), verification.attempts);
        details.insert(QStringLiteral("rollback"), false);
        if (jobs_.contains(id)) jobs_[id].data.insert(QStringLiteral("result"), details);
    }
    if (call.job && jobs_.contains(id)) {
        details.insert(QStringLiteral("job_id"), id);
        auto& job = jobs_[id].data;
        job.insert(QStringLiteral("state"), QStringLiteral("failed"));
        job.insert(QStringLiteral("stage"), QStringLiteral("failed"));
        const bool installation = call.envelope.value(QStringLiteral("operation")).toString() == QStringLiteral("plugins.install");
        const QString uiMessage = (installation ? QStringLiteral("插件安装失败: ") : QStringLiteral("任务失败: ")) + errorMessage;
        job.insert(QStringLiteral("message"), installation ? uiMessage : errorMessage);
        job.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), errorCode},
            {QStringLiteral("message"), errorMessage}, {QStringLiteral("details"), details}});
        if (!call.pluginSdk) ui_->finishTask(id, false, uiMessage);
        retainCompleted(id);
    }
    if (!call.alreadyReplied) sendError(id, errorCode, errorMessage, call.pluginSdk, details);
}

void AutomationController::retainCompleted(const QString& id) {
    completedJobs_.append(id);
    while (completedJobs_.size() > 200) jobs_.remove(completedJobs_.takeFirst());
}
} // namespace billfish::automation
