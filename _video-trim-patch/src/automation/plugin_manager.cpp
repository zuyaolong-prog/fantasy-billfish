#include "automation/plugin_manager.hpp"
#include "automation/app_input_scope.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSharedPointer>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <cmath>
#include <cwchar>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace billfish::automation {
namespace {
const QString animationBrowserId = QStringLiteral("billfish.animation-browser");
const QSet<QString> allowedOperations = {
    QStringLiteral("library.info"), QStringLiteral("items.search"), QStringLiteral("items.get"),
    QStringLiteral("folders.list"), QStringLiteral("tags.list"), QStringLiteral("items.selected"), QStringLiteral("items.update"),
    QStringLiteral("items.import_path"), QStringLiteral("items.import_url"), QStringLiteral("items.replace_file"),
    QStringLiteral("tags.create"), QStringLiteral("tags.assign")};

QJsonObject errorResult(const QString& code, const QString& message) {
    return {{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                                {QStringLiteral("message"), message}}}};
}

bool safePath(const QString& path) {
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains(QLatin1Char(':')) &&
        !path.split(QRegularExpression(QStringLiteral("[/\\\\]"))).contains(QStringLiteral(".."));
}

bool validateSchema(const QJsonObject& schema, QString* error, int depth = 0) {
    const QSet<QString> keywords = {QStringLiteral("type"), QStringLiteral("properties"), QStringLiteral("required"),
        QStringLiteral("additionalProperties"), QStringLiteral("items"), QStringLiteral("minItems"), QStringLiteral("maxItems"),
        QStringLiteral("minLength"), QStringLiteral("maxLength"), QStringLiteral("minimum"), QStringLiteral("maximum"),
        QStringLiteral("enum"), QStringLiteral("title"), QStringLiteral("description"), QStringLiteral("default")};
    const QSet<QString> types = {QStringLiteral("object"), QStringLiteral("array"), QStringLiteral("string"),
        QStringLiteral("integer"), QStringLiteral("number"), QStringLiteral("boolean"), QStringLiteral("null")};
    if (depth > 16 || !types.contains(schema.value(QStringLiteral("type")).toString())) {
        *error = QStringLiteral("Schema type must be one supported type string; maximum nesting depth is 16."); return false;
    }
    const auto type = schema.value(QStringLiteral("type")).toString();
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        if (!keywords.contains(it.key())) { *error = QStringLiteral("Unsupported schema keyword: ") + it.key(); return false; }
    }
    for (const auto& annotation : {QStringLiteral("title"), QStringLiteral("description")}) {
        if (schema.contains(annotation) && !schema.value(annotation).isString()) {
            *error = annotation + QStringLiteral(" must be a string."); return false;
        }
    }
    if (schema.contains(QStringLiteral("enum")) && (!schema.value(QStringLiteral("enum")).isArray() || schema.value(QStringLiteral("enum")).toArray().isEmpty())) {
        *error = QStringLiteral("Schema enum must be a non-empty array."); return false;
    }
    const QHash<QString, QString> typeKeywords = {
        {QStringLiteral("properties"), QStringLiteral("object")}, {QStringLiteral("required"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), QStringLiteral("object")}, {QStringLiteral("items"), QStringLiteral("array")},
        {QStringLiteral("minItems"), QStringLiteral("array")}, {QStringLiteral("maxItems"), QStringLiteral("array")},
        {QStringLiteral("minLength"), QStringLiteral("string")}, {QStringLiteral("maxLength"), QStringLiteral("string")}};
    for (auto it = typeKeywords.begin(); it != typeKeywords.end(); ++it) {
        if (schema.contains(it.key()) && type != it.value()) {
            *error = it.key() + QStringLiteral(" is only valid for schema type ") + it.value(); return false;
        }
    }
    for (const auto& limit : {QStringLiteral("minItems"), QStringLiteral("maxItems"), QStringLiteral("minLength"), QStringLiteral("maxLength")}) {
        const auto value = schema.value(limit);
        if (schema.contains(limit) && (!value.isDouble() || value.toDouble() < 0 || std::floor(value.toDouble()) != value.toDouble())) {
            *error = limit + QStringLiteral(" must be a non-negative integer."); return false;
        }
    }
    for (const auto& limit : {QStringLiteral("minimum"), QStringLiteral("maximum")}) {
        if (schema.contains(limit) && (!schema.value(limit).isDouble() ||
            (type != QStringLiteral("integer") && type != QStringLiteral("number")))) {
            *error = limit + QStringLiteral(" requires a numeric value and numeric schema type."); return false;
        }
    }
    for (const auto& pair : {qMakePair(QStringLiteral("minimum"), QStringLiteral("maximum")),
                             qMakePair(QStringLiteral("minItems"), QStringLiteral("maxItems")),
                             qMakePair(QStringLiteral("minLength"), QStringLiteral("maxLength"))}) {
        if (schema.contains(pair.first) && schema.contains(pair.second) && schema.value(pair.first).toDouble() > schema.value(pair.second).toDouble()) {
            *error = pair.first + QStringLiteral(" must not exceed ") + pair.second; return false;
        }
    }
    if (schema.contains(QStringLiteral("properties")) && !schema.value(QStringLiteral("properties")).isObject()) {
        *error = QStringLiteral("Schema properties must be an object."); return false;
    }
    const auto properties = schema.value(QStringLiteral("properties")).toObject();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (!it.value().isObject() || !validateSchema(it.value().toObject(), error, depth + 1)) {
            if (error->isEmpty()) *error = QStringLiteral("Property schema must be an object.");
            return false;
        }
    }
    if (schema.contains(QStringLiteral("required"))) {
        if (!schema.value(QStringLiteral("required")).isArray()) { *error = QStringLiteral("Schema required must be an array."); return false; }
        QSet<QString> required;
        for (const auto& key : schema.value(QStringLiteral("required")).toArray()) {
            if (!key.isString() || !properties.contains(key.toString()) || required.contains(key.toString())) {
                *error = QStringLiteral("Schema required must list unique declared property names."); return false;
            }
            required.insert(key.toString());
        }
    }
    if (schema.contains(QStringLiteral("additionalProperties")) && !schema.value(QStringLiteral("additionalProperties")).isBool()) {
        *error = QStringLiteral("Schema additionalProperties must be a boolean."); return false;
    }
    if (schema.contains(QStringLiteral("items")) && (!schema.value(QStringLiteral("items")).isObject() ||
        !validateSchema(schema.value(QStringLiteral("items")).toObject(), error, depth + 1))) {
        if (error->isEmpty()) *error = QStringLiteral("Array items schema must be an object.");
        return false;
    }
    return true;
}

QJsonObject readManifest(const QString& directory, QString* error) {
    QFile file(QDir(directory).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 262144) {
        *error = QStringLiteral("Cannot read manifest.json (maximum 256 KiB)."); return {};
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("manifest.json must be a JSON object."); return {};
    }
    auto value = doc.object();
    const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    const auto id = value.value(QStringLiteral("id")).toString();
    const auto main = value.value(QStringLiteral("main")).toObject();
    const auto entry = main.value(QStringLiteral("url")).toString();
    const bool dependency = !value.contains(QStringLiteral("main")) && value.value(QStringLiteral("preview")).isObject() &&
        value.value(QStringLiteral("preview")).toObject().isEmpty();
    if (value.contains(QStringLiteral("entry")) || !idPattern.match(id).hasMatch() ||
        value.value(QStringLiteral("name")).toString().isEmpty() || value.value(QStringLiteral("version")).toString().isEmpty() ||
        (!dependency && (!safePath(entry) || (!entry.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive) && !entry.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive)) ||
        !QFileInfo(QDir(directory).filePath(entry)).isFile()))) {
        *error = QStringLiteral("Incompatible plugin: a local Eagle-style main.url HTML entry, id, name and version are required. Python, preview-only and inspector-only plugins are not supported."); return {};
    }
    const auto platform = value.value(QStringLiteral("platform"));
    if ((!platform.isUndefined() && platform != QJsonValue(QStringLiteral("all")) && platform != QJsonValue(QStringLiteral("win"))) ||
        (value.contains(QStringLiteral("arch")) && value.value(QStringLiteral("arch")) != QJsonValue(QStringLiteral("all")) && value.value(QStringLiteral("arch")) != QJsonValue(QStringLiteral("x64")))) {
        *error = QStringLiteral("Plugin does not support Windows x64."); return {};
    }
    const auto dependencies = value.value(QStringLiteral("dependencies"));
    QJsonObject previewExtensions;
    if (value.contains(QStringLiteral("preview"))) {
        if (!value.value(QStringLiteral("preview")).isObject()) {
            *error = QStringLiteral("preview must contain extension groups."); return {};
        }
        const auto groups = value.value(QStringLiteral("preview")).toObject();
        const QRegularExpression extensionPattern(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,31}$"));
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            if (!it.value().isObject() || it.value().toObject().isEmpty()) {
                *error = QStringLiteral("Each preview group must declare viewer and/or thumbnail."); return {};
            }
            const auto handlers = it.value().toObject();
            for (auto handler = handlers.begin(); handler != handlers.end(); ++handler) {
                const auto path = handler.value().toObject().value(QStringLiteral("path")).toString();
                if ((handler.key() != QStringLiteral("viewer") && handler.key() != QStringLiteral("thumbnail")) ||
                    !handler.value().isObject() || !safePath(path) || !QFileInfo(QDir(directory).filePath(path)).isFile()) {
                    *error = QStringLiteral("Preview handler must be an existing plugin-relative viewer/thumbnail path."); return {};
                }
            }
            for (const auto& ext : it.key().toLower().split(QLatin1Char(','))) {
                if (!extensionPattern.match(ext).hasMatch() || previewExtensions.contains(ext)) {
                    *error = QStringLiteral("Preview extensions must be unique lower-case extension names."); return {};
                }
                previewExtensions.insert(ext, handlers);
            }
        }
    }
    value.insert(QStringLiteral("preview_extensions"), previewExtensions);
    if (!dependencies.isUndefined() && !dependencies.isArray()) {
        *error = QStringLiteral("dependencies must be an array of plugin IDs."); return {};
    }
    for (const auto& required : dependencies.toArray()) {
        if (!required.isString() || !idPattern.match(required.toString()).hasMatch()) {
            *error = QStringLiteral("dependencies must contain valid plugin IDs."); return {};
        }
    }
    const auto extension = value.value(QStringLiteral("billfish")).toObject();
    if (value.contains(QStringLiteral("billfish")) && !value.value(QStringLiteral("billfish")).isObject()) {
        *error = QStringLiteral("billfish extension must be an object."); return {};
    }
    if (extension.contains(QStringLiteral("apiVersion")) && extension.value(QStringLiteral("apiVersion")).toInt() != 1) {
        *error = QStringLiteral("Unsupported billfish.apiVersion."); return {};
    }
    QJsonArray permissions;
    if (extension.contains(QStringLiteral("permissions"))) {
        if (!extension.value(QStringLiteral("permissions")).isArray()) { *error = QStringLiteral("billfish.permissions must be an array."); return {}; }
        permissions = extension.value(QStringLiteral("permissions")).toArray();
    } else if (!dependency) {
        auto names = allowedOperations.values(); names.sort();
        for (const auto& name : names) permissions.append(name);
    }
    for (const auto& permission : permissions) {
        if (!permission.isString() || !allowedOperations.contains(permission.toString())) {
            *error = QStringLiteral("Unknown host permission: ") + permission.toString(); return {};
        }
    }
    QJsonArray declarations;
    const bool customCommands = extension.contains(QStringLiteral("commands"));
    if (customCommands) {
        if (!extension.value(QStringLiteral("commands")).isArray()) { *error = QStringLiteral("billfish.commands must be an array."); return {}; }
        declarations = extension.value(QStringLiteral("commands")).toArray();
    } else if (!dependency) {
        const QJsonObject inputSchema{{QStringLiteral("type"), QStringLiteral("object")}};
        declarations.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("run")}, {QStringLiteral("title"), QStringLiteral("打开插件")}, {QStringLiteral("input_schema"), inputSchema}});
        if (main.value(QStringLiteral("serviceMode")).toBool())
            declarations.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("open")}, {QStringLiteral("title"), QStringLiteral("打开服务界面")}, {QStringLiteral("input_schema"), inputSchema}});
    }
    value.insert(QStringLiteral("commands"), declarations);
    value.insert(QStringLiteral("permissions"), permissions);
    value.insert(QStringLiteral("mode"), dependency ? QStringLiteral("dependency") : (customCommands ? QStringLiteral("commands") : (main.value(QStringLiteral("serviceMode")).toBool() ? QStringLiteral("service") : QStringLiteral("window"))));
    value.insert(QStringLiteral("compatible"), true);
    value.insert(QStringLiteral("can_open_window"), !dependency && !customCommands);
    value.insert(QStringLiteral("open_command"), dependency ? QString() : (main.value(QStringLiteral("serviceMode")).toBool() ? QStringLiteral("open") : QStringLiteral("run")));
    const auto displayName = value.value(QStringLiteral("name")).toString();
    if (displayName.startsWith(QStringLiteral("{{")) && displayName.endsWith(QStringLiteral("}}"))) {
        const auto keys = displayName.mid(2, displayName.size() - 4).split(QLatin1Char('.'));
        for (const auto& locale : {QStringLiteral("zh_CN"), value.value(QStringLiteral("fallbackLanguage")).toString(QStringLiteral("en"))}) {
            if (!QRegularExpression(QStringLiteral("^[A-Za-z_-]+$")).match(locale).hasMatch()) continue;
            QFile translation(QDir(directory).filePath(QStringLiteral("_locales/") + locale + QStringLiteral(".json")));
            if (!translation.open(QIODevice::ReadOnly) || translation.size() > 1024 * 1024) continue;
            QJsonValue translated = QJsonDocument::fromJson(translation.readAll()).object();
            for (const auto& key : keys) translated = translated.toObject().value(key);
            if (translated.isString()) { value.insert(QStringLiteral("name"), translated); break; }
        }
    }
    QSet<QString> commands;
    if ((!dependency && declarations.isEmpty()) || declarations.size() > 100 || (dependency && customCommands)) {
        *error = QStringLiteral("Declare between 1 and 100 commands."); return {};
    }
    for (const auto& declaration : declarations) {
        const auto command = declaration.toObject();
        const auto commandId = command.value(QStringLiteral("id")).toString();
        if (!idPattern.match(commandId).hasMatch() || commands.contains(commandId) ||
            command.value(QStringLiteral("title")).toString().isEmpty() ||
            !command.value(QStringLiteral("input_schema")).isObject()) {
            *error = QStringLiteral("Invalid or duplicate command declaration."); return {};
        }
        const auto schema = command.value(QStringLiteral("input_schema")).toObject();
        if (schema.value(QStringLiteral("type")).toString() != QStringLiteral("object") || !validateSchema(schema, error)) {
            if (error->isEmpty()) *error = QStringLiteral("Command input_schema type must be object.");
            return {};
        }
        commands.insert(commandId);
    }
    return value;
}
} // namespace

struct PluginManager::Plugin {
    QString id;
    QString directory;
    QJsonObject manifest;
    bool enabled = false;
    bool builtin = false;
    QString status = QStringLiteral("disabled");
    QString error;
    QProcess* process = nullptr;
    QLocalServer* server = nullptr;
    QLocalSocket* socket = nullptr;
    QByteArray buffer;
    QString requestId;
    QString requestKind;
    QString libraryRoot;
    bool compatible = true;
    bool restartAfterStop = false;
    bool previewProtocol = false;
    QJsonObject nativeFormats;
    bool requiresActivation = false;
    QSet<QString> resolvedDependencyIds;
};

PluginManager::PluginManager(QString installRoot, QObject* parent)
    : QObject(parent), installRoot_(std::move(installRoot)) {
    auto dataRoot = qEnvironmentVariable("APPDATA");
    if (dataRoot.isEmpty()) dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QDir automation(QDir(dataRoot).filePath(QStringLiteral("Billfish/automation")));
    directory_ = automation.filePath(QStringLiteral("plugins"));
    statePath_ = automation.filePath(QStringLiteral("plugins-state.json"));
    QDir().mkpath(directory_);
    QJsonObject state;
    QFile stateFile(statePath_);
    if (stateFile.open(QIODevice::ReadOnly) && stateFile.size() <= 262144)
        state = QJsonDocument::fromJson(stateFile.readAll()).object();
    const QDir root(directory_);
    for (const auto& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (name == animationBrowserId) continue;
        QString error;
        const auto manifest = readManifest(root.filePath(name), &error);

        auto* plugin = new Plugin;
        plugin->id = name; plugin->directory = root.filePath(name); plugin->manifest = manifest;
        plugin->compatible = !manifest.isEmpty() && manifest.value(QStringLiteral("id")).toString() == name;
        if (!plugin->compatible) {
            plugin->manifest = {{QStringLiteral("id"), name}, {QStringLiteral("name"), name}, {QStringLiteral("mode"), QStringLiteral("incompatible")},
                                {QStringLiteral("compatible"), false}, {QStringLiteral("commands"), QJsonArray{}}, {QStringLiteral("permissions"), QJsonArray{}}};
            plugin->status = QStringLiteral("incompatible"); plugin->error = error.isEmpty() ? QStringLiteral("Plugin directory and manifest id differ.") : error;
        }
        plugin->enabled = plugin->compatible && state.value(name).toBool(false);
        plugin->requiresActivation = state.value(QStringLiteral("_pending_updates")).toArray().contains(name);
        if (plugin->requiresActivation && plugin->compatible) plugin->status = QStringLiteral("updated");
        plugins_.insert(name, plugin);
    }
    auto* builtin = new Plugin;
    builtin->id = animationBrowserId;
    builtin->directory = QDir(installRoot_).filePath(QStringLiteral("automation/animation_browser"));
    builtin->manifest = readManifest(builtin->directory, &builtin->error);
    builtin->compatible = !builtin->manifest.isEmpty() && builtin->manifest.value(QStringLiteral("id")).toString() == animationBrowserId;
    builtin->builtin = true;
    builtin->enabled = builtin->compatible;
    builtin->manifest.insert(QStringLiteral("builtin"), true);
    builtin->manifest.insert(QStringLiteral("read_only"), true);
    builtin->manifest.insert(QStringLiteral("commands"), QJsonArray{});
    builtin->manifest.insert(QStringLiteral("can_open_window"), false);
    builtin->manifest.insert(QStringLiteral("open_command"), QString());
    builtin->status = builtin->compatible ? QStringLiteral("waiting-context") : QStringLiteral("incompatible");
    plugins_.insert(animationBrowserId, builtin);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &PluginManager::shutdown);
}

PluginManager::~PluginManager() {
    shuttingDown_ = true;
    for (auto* process : installers_) {
        process->disconnect(this); process->kill(); process->waitForFinished(3000);
        QDir(process->property("staging").toString()).removeRecursively();
    }
    // Normal app shutdown sends lifecycle callbacks earlier via aboutToQuit.
    // Destruction cannot run an event loop; kill remaining child processes.
    for (auto* plugin : plugins_) {
        closeChannel(plugin);
        if (plugin->process) { plugin->process->disconnect(this); plugin->process->kill(); delete plugin->process; }
        delete plugin;
    }
}

QString PluginManager::pluginsDirectory() const { return directory_; }

void PluginManager::setWindowOwnerProvider(std::function<quintptr()> provider) {
    windowOwnerProvider_ = std::move(provider);
}

QJsonObject PluginManager::describe(const Plugin* plugin) const {
    auto result = plugin->manifest;
    result.insert(QStringLiteral("enabled"), plugin->enabled);
    result.insert(QStringLiteral("status"), plugin->status);
    result.insert(QStringLiteral("error"), plugin->error);
    result.insert(QStringLiteral("directory"), plugin->directory);
    result.insert(QStringLiteral("native_formats"), plugin->nativeFormats);
    result.insert(QStringLiteral("manifest_commands"), plugin->manifest.value(QStringLiteral("commands")));
    if (plugin->status != QStringLiteral("ready") && plugin->status != QStringLiteral("running"))
        result.insert(QStringLiteral("commands"), QJsonArray{});
    return result;
}

QJsonObject PluginManager::listPlugins() const {
    QJsonArray entries;
    auto ids = plugins_.keys(); ids.sort();
    for (const auto& id : ids) entries.append(describe(plugins_.value(id)));
    return {{QStringLiteral("plugins"), entries}, {QStringLiteral("directory"), directory_}};
}

bool PluginManager::saveState(QString* error) {
    QJsonObject state;
    QJsonArray pending;
    for (auto* plugin : plugins_) {
        if (plugin->builtin) continue;
        state.insert(plugin->id, plugin->enabled);
        if (plugin->requiresActivation) pending.append(plugin->id);
    }
    state.insert(QStringLiteral("_pending_updates"), pending);
    QSaveFile file(statePath_);
    const auto data = QJsonDocument(state).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("Cannot persist plugin state: ") + file.errorString(); return false;
    }
    return true;
}

void PluginManager::installPluginAsync(const QString& requestId, const QString& sourcePath) {
    if (shuttingDown_) { emit failed(requestId, QStringLiteral("host_stopping"), QStringLiteral("Application is shutting down.")); return; }
    if (installers_.contains(requestId)) { emit failed(requestId, QStringLiteral("duplicate_request"), QStringLiteral("Installation request already exists.")); return; }
    QString python = qEnvironmentVariable("BILLFISH_PYTHON");
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
    const auto helper = QDir(installRoot_).filePath(QStringLiteral("automation/plugin_runtime/package_installer.py"));
    if (python.isEmpty() || !QFileInfo::exists(helper)) {
        emit failed(requestId, QStringLiteral("installer_unavailable"), QStringLiteral("Plugin package installer or Python is unavailable.")); return;
    }
    const auto staging = QDir(QFileInfo(directory_).absolutePath()).filePath(QStringLiteral(".plugin-install-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto* process = new QProcess(this);
    installers_.insert(requestId, process);
    process->setProperty("staging", staging);
    auto buffer = QSharedPointer<QByteArray>::create();
    auto failure = QSharedPointer<QString>::create();
    auto staged = QSharedPointer<bool>::create(false);
    auto consumeOutput = [this, process, buffer, failure, staged, requestId]() {
        buffer->append(process->readAllStandardOutput());
        while (buffer->contains('\n')) {
            const auto end = buffer->indexOf('\n');
            const auto message = QJsonDocument::fromJson(buffer->left(end)).object();
            buffer->remove(0, end + 1);
            if (message.contains(QStringLiteral("error"))) *failure = message.value(QStringLiteral("error")).toString();
            if (message.value(QStringLiteral("staged")).toBool()) *staged = true;
            if (message.contains(QStringLiteral("completed")))
                emit progress(requestId, message.value(QStringLiteral("completed")).toInt(), message.value(QStringLiteral("total")).toInt(), message.value(QStringLiteral("message")).toString());
        }
    };
    auto complete = [this, process, requestId, staging, failure, staged, consumeOutput](bool success) {
        if (installers_.value(requestId) != process) return;
        consumeOutput();
        installers_.remove(requestId);
        QJsonObject result;
        if (success && *staged && !shuttingDown_) result = finalizeInstall(staging);
        else result = errorResult(QStringLiteral("install_failed"), failure->isEmpty() ? QStringLiteral("Plugin package installation failed or was interrupted.") : *failure);
        QDir(staging).removeRecursively();
        process->deleteLater();
        if (result.value(QStringLiteral("error")).isObject()) {
            const auto error = result.value(QStringLiteral("error")).toObject();
            emit failed(requestId, error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString());
        } else emit finished(requestId, result);
    };
    connect(process, &QProcess::readyReadStandardOutput, this, consumeOutput);
    connect(process, &QProcess::readyReadStandardError, process, [process]() { process->readAllStandardError(); });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [complete](int code, QProcess::ExitStatus status) { complete(code == 0 && status == QProcess::NormalExit); });
    connect(process, &QProcess::errorOccurred, this, [complete, failure, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { *failure = process->errorString(); complete(false); }
    });
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
    emit progress(requestId, 0, 0, QStringLiteral("正在检查插件包"));
    process->start(python, {QStringLiteral("-I"), helper, QFileInfo(sourcePath).absoluteFilePath(), staging});
}

QJsonObject PluginManager::finalizeInstall(const QString& staging) {
    QString error;
    const auto manifest = readManifest(staging, &error);
    if (manifest.isEmpty()) return errorResult(QStringLiteral("invalid_plugin"), error);
    const auto id = manifest.value(QStringLiteral("id")).toString();
    if (id == animationBrowserId) return errorResult(QStringLiteral("builtin_read_only"), QStringLiteral("The built-in animation browser is maintained with Billfish."));
    auto* previous = plugins_.value(id, nullptr);
    if (previous && previous->process)
        return errorResult(QStringLiteral("plugin_running"), QStringLiteral("Close or disable the running plugin before updating it."));
    for (auto* consumer : plugins_) {
        if (consumer->process && consumer->resolvedDependencyIds.contains(id))
            return errorResult(QStringLiteral("plugin_running"), QStringLiteral("Close or disable the plugin using this dependency before updating it: ") + consumer->id);
    }
    const auto destination = QDir(directory_).filePath(id);
    const auto backup = QDir(QFileInfo(directory_).absolutePath()).filePath(QStringLiteral(".plugin-backup-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const bool existing = QFileInfo::exists(destination);
    if (existing && !QDir().rename(destination, backup))
        return errorResult(QStringLiteral("install_failed"), QStringLiteral("Cannot move the previous plugin; it has been preserved."));
    if (!QDir().rename(staging, destination)) {
        if (existing && !QDir().rename(backup, destination))
            return errorResult(QStringLiteral("install_failed"), QStringLiteral("Cannot finalize plugin. Previous version is preserved at: ") + backup);
        return errorResult(QStringLiteral("install_failed"), QStringLiteral("Cannot finalize plugin; previous version restored."));
    }
    const auto oldManifest = previous ? previous->manifest : QJsonObject{};
    const auto oldStatus = previous ? previous->status : QString();
    const bool oldActivation = previous && previous->requiresActivation;
    const bool oldCompatible = !previous || previous->compatible;
    auto* plugin = previous ? previous : new Plugin;
    plugin->id = id; plugin->directory = destination; plugin->manifest = manifest;
    plugin->compatible = true;
    if (previous) { plugin->enabled = previous->enabled; plugin->status = QStringLiteral("updated"); plugin->requiresActivation = true; }
    plugins_.insert(id, plugin);
    if (!saveState(&error)) {
        if (previous) {
            previous->manifest = oldManifest; previous->status = oldStatus; previous->requiresActivation = oldActivation;
            previous->compatible = oldCompatible;
        } else { plugins_.remove(id); delete plugin; }
        QDir(destination).removeRecursively();
        if (existing && !QDir().rename(backup, destination))
            return errorResult(QStringLiteral("state_write_failed"), error + QStringLiteral(" Previous version is preserved at: ") + backup);
        return errorResult(QStringLiteral("state_write_failed"), error);
    }
    if (existing) QDir(backup).removeRecursively();
    plugin->error.clear();
    emit changed();
    auto result = describe(plugin);
    result.insert(QStringLiteral("completion"), existing ? QStringLiteral("plugin_updated") : QStringLiteral("plugin_installed"));
    result.insert(QStringLiteral("operation"), QStringLiteral("plugins.install"));
    return result;
}

QJsonObject PluginManager::setEnabled(const QString& id, bool enabled) {
    auto* plugin = plugins_.value(id, nullptr);
    if (!plugin) return errorResult(QStringLiteral("plugin_not_found"), QStringLiteral("Unknown plugin."));
    if (plugin->builtin) return errorResult(QStringLiteral("builtin_read_only"), QStringLiteral("The built-in animation browser is always enabled."));
    if (enabled && !plugin->compatible) return errorResult(QStringLiteral("incompatible_plugin"), plugin->error);
    if (shuttingDown_) return errorResult(QStringLiteral("host_stopping"), QStringLiteral("Application is shutting down."));
    if (enabled && plugin->status == QStringLiteral("stopping"))
        return errorResult(QStringLiteral("plugin_stopping"), QStringLiteral("Wait for the plugin to stop before enabling it."));
    const bool previous = plugin->enabled;
    const bool pending = plugin->requiresActivation;
    plugin->enabled = enabled;
    plugin->requiresActivation = false;
    QString error;
    if (!saveState(&error)) { plugin->enabled = previous; plugin->requiresActivation = pending; return errorResult(QStringLiteral("state_write_failed"), error); }
    if (enabled && !plugin->process) start(plugin);
    else if (!enabled && plugin->compatible) { plugin->restartAfterStop = false; plugin->error.clear(); stop(plugin); }
    emit changed();
    return describe(plugin);
}

void PluginManager::start(Plugin* plugin) {
    if (shuttingDown_ || !plugin->enabled || plugin->process || plugin->requiresActivation) return;
    if (plugin->manifest.value(QStringLiteral("mode")).toString() == QStringLiteral("dependency")) {
        plugin->status = QStringLiteral("ready"); return;
    }
    if (!libraryInitialized_ || (plugin->builtin && libraryRoot_.isEmpty())) {
        plugin->status = QStringLiteral("waiting-context"); return;
    }
    QJsonObject dependencies;
    QSet<QString> dependencyIds;
    for (const auto& required : plugin->manifest.value(QStringLiteral("dependencies")).toArray()) {
        const auto id = required.toString();
        Plugin* installed = nullptr;
        for (const auto& candidate : {id + QStringLiteral("-win-x64"), id + QStringLiteral("-win"), id}) {
            auto* entry = plugins_.value(candidate, nullptr);
            if (entry && entry->compatible && entry->manifest.value(QStringLiteral("mode")).toString() == QStringLiteral("dependency")) { installed = entry; break; }
        }
        if (!installed) {
            plugin->status = QStringLiteral("failed");
            plugin->error = QStringLiteral("dependency_not_installed: ") + id;
            emit changed(); return;
        }
        if (installed->requiresActivation) {
            plugin->status = QStringLiteral("failed");
            plugin->error = QStringLiteral("dependency_activation_required: ") + installed->id;
            emit changed(); return;
        }
        dependencies.insert(id, installed->directory);
        dependencyIds.insert(installed->id);
    }
    const auto runner = QDir(installRoot_).filePath(QStringLiteral("automation/plugin_runtime/main.cjs"));
    auto executable = qEnvironmentVariable("BILLFISH_ELECTRON");
    if (executable.isEmpty()) executable = QDir(installRoot_).filePath(QStringLiteral("tools/electron/electron.exe"));
    if (!QFileInfo::exists(executable) || !QFileInfo::exists(runner)) {
        plugin->status = QStringLiteral("failed"); plugin->error = QStringLiteral("Pinned Electron plugin runtime is unavailable.");
        emit changed(); return;
    }
    plugin->libraryRoot = libraryRoot_;
    plugin->status = QStringLiteral("starting"); plugin->error.clear(); plugin->buffer.clear();
    const auto pipeName = QStringLiteral("BillfishPlugin-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto* server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(pipeName)) {
        plugin->status = QStringLiteral("failed"); plugin->error = QStringLiteral("Cannot create private plugin IPC pipe: ") + server->errorString();
        delete server; emit changed(); return;
    }
    plugin->server = server;
    connect(server, &QLocalServer::newConnection, this, [this, plugin, server]() {
        while (server->hasPendingConnections()) {
            auto* socket = server->nextPendingConnection();
            if (plugin->server != server || plugin->socket || !plugin->process || plugin->status != QStringLiteral("starting")) {
                socket->abort(); socket->deleteLater(); continue;
            }
            plugin->socket = socket;
            // Each session accepts exactly one connection. No endpoint is published.
            server->close();
            connect(socket, &QLocalSocket::readyRead, this, [this, plugin, socket]() {
                if (plugin->socket == socket) consume(plugin);
            });
            connect(socket, &QLocalSocket::disconnected, this, [plugin, socket]() {
                if (plugin->socket != socket || plugin->status == QStringLiteral("stopping")) return;
                if (plugin->error.isEmpty()) plugin->error = QStringLiteral("Plugin IPC pipe disconnected.");
                if (plugin->process) plugin->process->kill();
            });
            if (socket->bytesAvailable()) consume(plugin);
        }
    });
    auto* process = new QProcess(this); plugin->process = process;
    plugin->resolvedDependencyIds = dependencyIds;
    process->setProgram(executable);
    process->setArguments({runner, plugin->directory, pipeName});
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("ELECTRON_RUN_AS_NODE"));
    environment.remove(QStringLiteral("BILLFISH_ANIMATION_BROWSER_BUILTIN"));
    environment.remove(QStringLiteral("BILLFISH_BUILTIN_DESCRIPTOR"));
    if (plugin->builtin) {
        environment.insert(QStringLiteral("BILLFISH_ANIMATION_BROWSER_BUILTIN"), QStringLiteral("1"));
        environment.insert(QStringLiteral("BILLFISH_BUILTIN_DESCRIPTOR"), QString::fromUtf8(QJsonDocument(plugin->manifest).toJson(QJsonDocument::Compact)));
    }
    environment.insert(QStringLiteral("BILLFISH_PLUGIN_LIBRARY_ROOT"), plugin->libraryRoot);
    environment.insert(QStringLiteral("BILLFISH_APP_VERSION"), QStringLiteral("3.1.5.12"));
    environment.insert(QStringLiteral("BILLFISH_APP_BUILD"), QStringLiteral("3.1.5.12"));
    if (windowOwnerProvider_) environment.insert(QStringLiteral("BILLFISH_PLUGIN_WINDOW_OWNER_REQUIRED"), QStringLiteral("1"));
    else environment.remove(QStringLiteral("BILLFISH_PLUGIN_WINDOW_OWNER_REQUIRED"));
    environment.insert(QStringLiteral("BILLFISH_PLUGIN_DEPENDENCIES"), QString::fromUtf8(QJsonDocument(dependencies).toJson(QJsonDocument::Compact)));
    environment.insert(QStringLiteral("BILLFISH_PLUGIN_PROFILE"), QDir(QFileInfo(statePath_).absolutePath()).filePath(QStringLiteral("plugin-data/") + plugin->id));
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(plugin->directory);
    process->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
    connect(process, &QProcess::readyReadStandardOutput, this, [process]() { process->readAllStandardOutput(); });
    connect(process, &QProcess::readyReadStandardError, this, [plugin]() {
        if (plugin->process) plugin->process->readAllStandardError(); // Drain output; protocol errors are explicit.
    });
    connect(process, &QProcess::errorOccurred, this, [this, plugin, process](QProcess::ProcessError error) {
        if (plugin->process == process && error == QProcess::FailedToStart) processStopped(plugin, process->errorString());
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, plugin, process](int code, QProcess::ExitStatus) {
        if (plugin->process == process) processStopped(plugin, QStringLiteral("Plugin process exited (%1).").arg(code));
    });
    process->start();
    QTimer::singleShot(15000, this, [this, plugin, process]() {
        if (plugin->process == process && plugin->status == QStringLiteral("starting")) {
            if (plugin->error.isEmpty()) plugin->error = QStringLiteral("Plugin startup timed out.");
            process->kill();
        }
    });
    emit changed();
}

void PluginManager::setLibraryRoot(const QString& libraryRoot) {
    const auto normalized = libraryRoot.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(libraryRoot));
    if (libraryInitialized_ && QString::compare(libraryRoot_, normalized, Qt::CaseInsensitive) == 0) return;
    const bool initial = !libraryInitialized_;
    libraryInitialized_ = true; libraryRoot_ = normalized;
    for (auto* plugin : plugins_) {
        if (!plugin->compatible || !plugin->enabled) continue;
        plugin->nativeFormats = {{QStringLiteral("state"), QStringLiteral("pending")}, {QStringLiteral("library_root"), normalized}};
        if (plugin->requiresActivation) continue;
        if (!initial && plugin->process) { plugin->restartAfterStop = true; stop(plugin); }
        else if (!plugin->process) start(plugin);
    }
    emit changed();
}

void PluginManager::clearHostRequests(Plugin* plugin) {
    for (auto it = hostRequests_.begin(); it != hostRequests_.end();) {
        if (it.value().plugin == plugin) it = hostRequests_.erase(it); else ++it;
    }
}

void PluginManager::closeChannel(Plugin* plugin) {
    auto* socket = plugin->socket; plugin->socket = nullptr;
    if (socket) { socket->disconnect(this); socket->abort(); socket->deleteLater(); }
    auto* server = plugin->server; plugin->server = nullptr;
    if (server) { server->disconnect(this); server->close(); server->deleteLater(); }
}

void PluginManager::stop(Plugin* plugin) {
    if (plugin->builtin) emit builtinSessionStopped();
    clearHostRequests(plugin);
    if (!plugin->requestId.isEmpty()) {
        const auto requestId = plugin->requestId; plugin->requestId.clear(); plugin->requestKind.clear();
        emit failed(requestId, QStringLiteral("plugin_disabled"), QStringLiteral("Plugin stopped accepting work. A submitted native write may already have completed."));
    }
    if (!plugin->process) { plugin->status = QStringLiteral("disabled"); return; }
    plugin->status = QStringLiteral("stopping");
    send(plugin, {{QStringLiteral("type"), QStringLiteral("shutdown")}});
    auto* process = plugin->process;
    QTimer::singleShot(2000, this, [plugin, process]() { if (plugin->process == process) process->kill(); });
}

void PluginManager::shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    for (auto* process : installers_) process->kill();
    for (auto* plugin : plugins_) stop(plugin);
}

void PluginManager::processStopped(Plugin* plugin, const QString& reason) {
    auto* process = plugin->process;
    if (!process) return;
    if (plugin->builtin) emit builtinSessionStopped();
    plugin->process = nullptr; process->deleteLater();
    plugin->resolvedDependencyIds.clear();
    plugin->previewProtocol = false;
    closeChannel(plugin);
    clearHostRequests(plugin);
    if (!plugin->requestId.isEmpty()) {
        const auto requestId = plugin->requestId; plugin->requestId.clear(); plugin->requestKind.clear();
        emit failed(requestId, QStringLiteral("plugin_crashed"), reason);
    }
    plugin->buffer.clear();
    if (plugin->restartAfterStop && plugin->enabled && !shuttingDown_) {
        plugin->restartAfterStop = false; plugin->error.clear();
        plugin->status = QStringLiteral("disabled");
        QTimer::singleShot(0, this, [this, plugin]() { start(plugin); });
    } else if (plugin->enabled && !shuttingDown_) {
        plugin->status = QStringLiteral("failed");
        if (plugin->error.isEmpty()) plugin->error = reason;
    } else { plugin->status = QStringLiteral("disabled"); plugin->error.clear(); }
    emit changed();
}

void PluginManager::send(Plugin* plugin, const QJsonObject& message) {
    if (plugin->socket && plugin->socket->state() == QLocalSocket::ConnectedState)
        plugin->socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void PluginManager::consume(Plugin* plugin) {
    if (!plugin->process || !plugin->socket) return;
    plugin->buffer += plugin->socket->readAll();
    while (true) {
        const auto newline = plugin->buffer.indexOf('\n');
        if (newline < 0) break;
        const auto line = plugin->buffer.left(newline); plugin->buffer.remove(0, newline + 1);
        if (line.size() > 4 * 1024 * 1024) { plugin->error = QStringLiteral("Plugin protocol message is too large."); plugin->process->kill(); return; }
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(line, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
            plugin->error = QStringLiteral("Invalid plugin protocol output."); plugin->process->kill(); return;
        }
        receive(plugin, doc.object());
    }
    if (plugin->buffer.size() > 4 * 1024 * 1024) {
        plugin->error = QStringLiteral("Plugin protocol buffer is too large."); plugin->process->kill();
    }
}

void PluginManager::run(const QString& requestId, const QString& pluginId, const QString& command,
                        const QJsonObject& args, const QString& libraryRoot) {
    auto* plugin = plugins_.value(pluginId, nullptr);
    if (!plugin || !plugin->enabled || plugin->status != QStringLiteral("ready")) {
        emit failed(requestId, plugin && !plugin->requestId.isEmpty() ? QStringLiteral("plugin_busy") : QStringLiteral("plugin_not_ready"),
                    QStringLiteral("Enable the plugin and wait until it is ready.")); return;
    }
    bool declared = false;
    for (const auto& declaration : plugin->manifest.value(QStringLiteral("commands")).toArray())
        if (declaration.toObject().value(QStringLiteral("id")).toString() == command) declared = true;
    if (!declared) { emit failed(requestId, QStringLiteral("unknown_command"), QStringLiteral("Command is not declared by this plugin.")); return; }
    if (QString::compare(QDir::cleanPath(QDir::fromNativeSeparators(libraryRoot)), QDir::cleanPath(QDir::fromNativeSeparators(plugin->libraryRoot)), Qt::CaseInsensitive) != 0) {
        emit failed(requestId, QStringLiteral("library_context_changed"), QStringLiteral("Plugin session is bound to another library; restart it in the current library.")); return;
    }
    const QJsonObject request{{QStringLiteral("type"), QStringLiteral("run")}, {QStringLiteral("id"), requestId},
        {QStringLiteral("command"), command}, {QStringLiteral("arguments"), args}, {QStringLiteral("library_root"), libraryRoot},
        {QStringLiteral("background"), AppInputScope::active()}};
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024) {
        emit failed(requestId, QStringLiteral("request_too_large"), QStringLiteral("The complete plugin request exceeds the 4 MiB message limit.")); return;
    }
    plugin->requestId = requestId; plugin->requestKind.clear(); plugin->status = QStringLiteral("running");
    const auto mode = plugin->manifest.value(QStringLiteral("mode")).toString();
    const auto activity = mode == QStringLiteral("commands") ? QStringLiteral("正在执行插件命令: ") :
        (mode == QStringLiteral("service") && command != QStringLiteral("open") ? QStringLiteral("正在启动服务: ") : QStringLiteral("正在打开插件界面: "));
    emit progress(requestId, 0, 0, activity + plugin->manifest.value(QStringLiteral("name")).toString());
    send(plugin, request);
    emit changed();
}

bool PluginManager::previewEnabled(const QString& id) const {
    const auto* plugin = plugins_.value(id, nullptr);
    return plugin && !shuttingDown_ && plugin->enabled && plugin->compatible && plugin->previewProtocol &&
        (plugin->status == QStringLiteral("ready") || plugin->status == QStringLiteral("running"));
}

QJsonArray PluginManager::previewDeclarations() const {
    QJsonArray result;
    auto ids = plugins_.keys(); ids.sort();
    for (const auto& id : ids) {
        if (!previewEnabled(id)) continue;
        const auto extensions = plugins_[id]->manifest.value(QStringLiteral("preview_extensions")).toObject().keys();
        if (extensions.isEmpty()) continue;
        QJsonArray names;
        for (const auto& extension : extensions) names.append(extension);
        result.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("extensions"), names}});
    }
    return result;
}

void PluginManager::setNativeFormatStatus(const QString& id, const QJsonObject& status) {
    auto* plugin = plugins_.value(id, nullptr);
    if (!plugin || plugin->nativeFormats == status) return;
    plugin->nativeFormats = status; emit changed();
}

QString PluginManager::previewPlugin(const QString& path, const QString& kind, const QString& requestedId) const {
    auto ids = plugins_.keys(); ids.sort();
    const auto extension = QFileInfo(path).suffix().toLower();
    if (extension == QStringLiteral("fbx") || extension == QStringLiteral("glb")) {
        if ((!requestedId.isEmpty() && requestedId != animationBrowserId) || !previewEnabled(animationBrowserId)) return {};
        return plugins_.value(animationBrowserId)->manifest.value(QStringLiteral("preview_extensions")).toObject().value(extension).toObject().contains(kind) ? animationBrowserId : QString();
    }
    for (const auto& id : ids) {
        if ((!requestedId.isEmpty() && requestedId != id) || !previewEnabled(id)) continue;
        if (plugins_[id]->manifest.value(QStringLiteral("preview_extensions")).toObject().value(extension).toObject().contains(kind)) return id;
    }
    return {};
}

QString PluginManager::previewStagingRoot(const QString& id) const {
    return QDir(QFileInfo(statePath_).absolutePath()).filePath(QStringLiteral("plugin-data/") + id + QStringLiteral("/preview-staging"));
}

void PluginManager::preview(const QString& id, const QString& pluginId, const QString& kind,
                            const QJsonObject& item, const QString& root, bool background, const QString& viewerIntent) {
    auto* plugin = plugins_.value(pluginId, nullptr);
    if ((kind != QStringLiteral("viewer") && kind != QStringLiteral("thumbnail")) ||
        previewPlugin(item.value(QStringLiteral("path")).toString(), kind, pluginId).isEmpty()) {
        emit failed(id, QStringLiteral("preview_not_supported"), QStringLiteral("No enabled ready plugin owns this format and preview kind.")); return;
    }
    if (!plugin || plugin->status != QStringLiteral("ready")) {
        emit failed(id, QStringLiteral("plugin_busy"), QStringLiteral("The preview plugin is processing another request.")); return;
    }
    if (QString::compare(QDir::cleanPath(root), QDir::cleanPath(plugin->libraryRoot), Qt::CaseInsensitive) != 0) {
        emit failed(id, QStringLiteral("library_context_changed"), QStringLiteral("The preview plugin is bound to another library.")); return;
    }
    plugin->requestId = id; plugin->requestKind = kind; plugin->status = QStringLiteral("running");
    const QJsonObject source{{QStringLiteral("id"), item.value(QStringLiteral("id"))},
        {QStringLiteral("path"), item.value(QStringLiteral("path"))}, {QStringLiteral("name"), item.value(QStringLiteral("name"))}};
    QJsonObject request{{QStringLiteral("type"), QStringLiteral("preview")}, {QStringLiteral("id"), id},
        {QStringLiteral("kind"), kind}, {QStringLiteral("item"), source}, {QStringLiteral("background"), background}};
    if (plugin->builtin && kind == QStringLiteral("viewer") && !viewerIntent.isEmpty())
        request.insert(QStringLiteral("viewer_intent"), viewerIntent);
    if (kind == QStringLiteral("thumbnail"))
        request.insert(QStringLiteral("dest"), QDir(previewStagingRoot(pluginId)).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png")));
    send(plugin, request);
    emit changed();
}

void PluginManager::admitViewerIntent(const QString& intentId, const QJsonObject& feedback) {
    auto* plugin = plugins_.value(animationBrowserId, nullptr);
    if (!plugin || !plugin->builtin || intentId.isEmpty() || !previewEnabled(animationBrowserId)) return;
    send(plugin, {{QStringLiteral("type"), QStringLiteral("preview_intent")}, {QStringLiteral("id"), intentId},
        {QStringLiteral("feedback"), feedback}});
}

void PluginManager::failViewerIntent(const QString& intentId, const QString& code, const QString& message) {
    auto* plugin = plugins_.value(animationBrowserId, nullptr);
    if (!plugin || !plugin->builtin || intentId.isEmpty() || !previewEnabled(animationBrowserId)) return;
    send(plugin, {{QStringLiteral("type"), QStringLiteral("preview_intent_error")}, {QStringLiteral("id"), intentId},
        {QStringLiteral("code"), code}, {QStringLiteral("message"), message}});
}

void PluginManager::cancelViewerPreview(const QString& requestId) {
    auto* plugin = plugins_.value(animationBrowserId, nullptr);
    if (!plugin || !plugin->builtin || requestId.isEmpty() || plugin->requestId != requestId ||
        plugin->requestKind != QStringLiteral("viewer")) return;
    send(plugin, {{QStringLiteral("type"), QStringLiteral("preview_cancel")}, {QStringLiteral("id"), requestId}});
    plugin->requestId.clear(); plugin->requestKind.clear();
    if (plugin->status == QStringLiteral("running")) plugin->status = QStringLiteral("ready");
    // Late result/error frames retain their old id and cannot complete a newer
    // request. Thumbnail and cover work never enters this cancellation path.
    emit changed();
}

void PluginManager::receive(Plugin* plugin, const QJsonObject& message) {
    const auto type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("ready") && plugin->status == QStringLiteral("starting")) {
        const auto capabilities = message.value(QStringLiteral("capabilities")).toObject();
        plugin->previewProtocol = capabilities.value(QStringLiteral("preview")).toBool() &&
            capabilities.value(QStringLiteral("previewProtocol")).toInt() == 1;
        plugin->status = QStringLiteral("ready"); emit changed(); return;
    }
    if (type == QStringLiteral("startup_error")) {
        plugin->error = message.value(QStringLiteral("message")).toString();
        if (plugin->process) plugin->process->kill(); return;
    }
    const bool sessionActive = plugin->enabled && (plugin->status == QStringLiteral("starting") || plugin->status == QStringLiteral("ready") || plugin->status == QStringLiteral("running"));
    if (type == QStringLiteral("animation_visibility")) {
        const auto requestId = message.value(QStringLiteral("request_id")).toString();
        const auto paths = message.value(QStringLiteral("hidden_paths"));
        bool valid = sessionActive && plugin->builtin && previewEnabled(plugin->id) && !plugin->libraryRoot.isEmpty() &&
            !requestId.isEmpty() && requestId.size() <= 128 && paths.isArray() && paths.toArray().size() <= 100000;
        for (const auto& path : paths.toArray())
            if (!path.isString() || !QFileInfo(path.toString()).isAbsolute() || path.toString().contains(QChar(0))) valid = false;
        const auto reject = [this, plugin, requestId](const QString& code, const QString& reason) {
            send(plugin, {{QStringLiteral("type"), QStringLiteral("animation_visibility_result")}, {QStringLiteral("request_id"), requestId},
                {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), reason}}}});
        };
        if (!valid) {
            reject(QStringLiteral("invalid_animation_visibility"), QStringLiteral("Animation visibility requires an active built-in session and a complete array of absolute source paths.")); return;
        }
        if (hostRequests_.size() >= 256) { reject(QStringLiteral("host_busy"), QStringLiteral("Too many pending plugin host calls.")); return; }
        const auto internalId = QStringLiteral("animation-visibility-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        hostRequests_.insert(internalId, {plugin, requestId, QStringLiteral("animation.visibility"), QStringLiteral("animation_visibility_result")});
        emit hostCall(internalId, QStringLiteral("animation.visibility"), {{QStringLiteral("hidden_paths"), paths}}, plugin->libraryRoot);
        return;
    }
    if (type == QStringLiteral("preview_cover") || type == QStringLiteral("animation_thumbnail")) {
        const bool thumbnail = type == QStringLiteral("animation_thumbnail");
        const auto replyType = thumbnail ? QStringLiteral("animation_thumbnail_result") : QStringLiteral("preview_cover_result");
        const auto requestId = message.value(QStringLiteral("request_id")).toString();
        const auto reject = [this, plugin, requestId, replyType](const QString& code, const QString& reason) {
            send(plugin, {{QStringLiteral("type"), replyType}, {QStringLiteral("request_id"), requestId},
                {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), reason}}}});
        };
        const auto item = message.value(QStringLiteral("item")).toObject();
        const auto itemId = item.value(QStringLiteral("id")).toString();
        const auto source = item.value(QStringLiteral("path")).toString();
        const auto dest = message.value(QStringLiteral("dest")).toString();
        const auto pose = message.value(QStringLiteral("pose")).toObject();
        const auto fingerprint = message.value(QStringLiteral("source")).toObject();
        bool validId = false; const auto identifier = itemId.toLongLong(&validId, 10);
        const auto clip = pose.value(QStringLiteral("clip_index"));
        const auto time = pose.value(QStringLiteral("time"));
        const auto fps = pose.value(QStringLiteral("fps"));
        const auto animation = message.value(QStringLiteral("animation_source")).toObject();
        const bool basePose = plugin->builtin && pose.value(QStringLiteral("kind")).toString() == QStringLiteral("base") &&
            time.isDouble() && time.toDouble() == 0 && animation.isEmpty();
        const bool clipPose = clip.isDouble() && clip.toDouble() >= 0 && std::floor(clip.toDouble()) == clip.toDouble() &&
            pose.value(QStringLiteral("clip_name")).isString() && pose.value(QStringLiteral("clip_name")).toString().size() <= 1024 &&
            time.isDouble() && std::isfinite(time.toDouble()) && time.toDouble() >= 0 &&
            ((fps.isDouble() && std::isfinite(fps.toDouble()) && fps.toDouble() > 0) || (plugin->builtin && fps.isNull())) &&
            (!plugin->builtin || (pose.value(QStringLiteral("kind")).toString() == QStringLiteral("clip") &&
                !pose.value(QStringLiteral("rowId")).toString().isEmpty() && animation.value(QStringLiteral("rowId")) == pose.value(QStringLiteral("rowId")) &&
                animation.value(QStringLiteral("clip_index")) == clip && QFileInfo(animation.value(QStringLiteral("path")).toString()).isAbsolute()));
        if (!sessionActive || !previewEnabled(plugin->id) || plugin->libraryRoot.isEmpty() ||
            requestId.isEmpty() || requestId.size() > 128 || !validId || identifier <= 0 ||
            !QRegularExpression(QStringLiteral("^[1-9][0-9]{0,18}$")).match(itemId).hasMatch() ||
            !QFileInfo(source).isAbsolute() || (!thumbnail && !QFileInfo(dest).isAbsolute()) ||
            previewPlugin(source, QStringLiteral("viewer"), plugin->id).isEmpty() ||
            (thumbnail ? !plugin->builtin : (!basePose && !clipPose)) ||
            !QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(fingerprint.value(QStringLiteral("sha256")).toString()).hasMatch() ||
            !QRegularExpression(QStringLiteral("^(0|[1-9][0-9]{0,19})$")).match(fingerprint.value(QStringLiteral("bytes")).toString()).hasMatch() ||
            !QRegularExpression(QStringLiteral("^-?(0|[1-9][0-9]{0,24})$")).match(fingerprint.value(QStringLiteral("mtime_ns")).toString()).hasMatch()) {
            reject(QStringLiteral("invalid_viewer_cover"), QStringLiteral("The viewer cover request or its bound plugin session is invalid.")); return;
        }
        if (hostRequests_.size() >= 256) {
            reject(QStringLiteral("host_busy"), QStringLiteral("Too many pending plugin host calls.")); return;
        }
        const auto internalId = QStringLiteral("viewer-cover-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto operation = thumbnail ? QStringLiteral("plugins.regenerate_thumbnail") : QStringLiteral("plugins.set_cover");
        hostRequests_.insert(internalId, {plugin, requestId, operation, replyType});
        QJsonObject arguments
            {{QStringLiteral("plugin_id"), plugin->id}, {QStringLiteral("item_id"), itemId},
                {QStringLiteral("source_path"), source}, {QStringLiteral("dest"), dest}, {QStringLiteral("pose"), pose},
                {QStringLiteral("source_fingerprint"), fingerprint}};
        if (!animation.isEmpty()) arguments.insert(QStringLiteral("animation_source"), animation);
        emit hostCall(internalId, operation, arguments, plugin->libraryRoot);
        return;
    }
    if (type == QStringLiteral("window_owner")) {
        const auto requestId = message.value(QStringLiteral("request_id")).toString();
        const auto reply = [this, plugin, requestId](const QJsonObject& data, const QString& error = QString()) {
            QJsonObject result{{QStringLiteral("type"), QStringLiteral("window_owner_result")}, {QStringLiteral("request_id"), requestId}};
            if (error.isEmpty()) result.insert(QStringLiteral("data"), data);
            else result.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("window_owner_unavailable")}, {QStringLiteral("message"), error}});
            send(plugin, result);
        };
#ifdef Q_OS_WIN
        const auto encodedHandle = message.value(QStringLiteral("hwnd")).toString();
        const auto kind = message.value(QStringLiteral("kind")).toString();
        bool converted = false;
        const auto handleValue = encodedHandle.toULongLong(&converted, 10);
        const auto* process = plugin->process;
        const auto windowId = message.value(QStringLiteral("window_id"));
        if (!sessionActive || !process || !windowOwnerProvider_ || requestId.isEmpty() || requestId.size() > 128 ||
            !QRegularExpression(QStringLiteral("^[1-9][0-9]{0,19}$")).match(encodedHandle).hasMatch() || !converted ||
            handleValue > (std::numeric_limits<quintptr>::max)() ||
            (kind != QStringLiteral("main") && kind != QStringLiteral("viewer")) ||
            !message.value(QStringLiteral("background")).isBool() || !windowId.isDouble() ||
            windowId.toDouble() < 1 || std::floor(windowId.toDouble()) != windowId.toDouble()) {
            reply({}, QStringLiteral("The plugin window owner request is invalid or the session stopped.")); return;
        }
        const auto window = reinterpret_cast<HWND>(static_cast<quintptr>(handleValue));
        const auto owner = reinterpret_cast<HWND>(windowOwnerProvider_());
        DWORD windowPid = 0, ownerPid = 0;
        wchar_t windowClass[128] = {};
        GetWindowThreadProcessId(window, &windowPid); GetWindowThreadProcessId(owner, &ownerPid);
        GetClassNameW(window, windowClass, 128);
        if (!IsWindow(window) || !IsWindow(owner) || window == owner ||
            windowPid != static_cast<DWORD>(process->processId()) || ownerPid != GetCurrentProcessId() ||
            GetAncestor(window, GA_ROOT) != window || GetAncestor(owner, GA_ROOT) != owner ||
            (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) ||
            (GetWindowLongPtrW(owner, GWL_STYLE) & WS_CHILD) || wcscmp(windowClass, L"Chrome_WidgetWin_1") != 0) {
            reply({}, QStringLiteral("The requested HWND is not a top-level window of this plugin process or its Billfish owner is unavailable.")); return;
        }
        SetLastError(0);
        const auto previous = SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
        const auto error = GetLastError();
        if ((!previous && error) || GetWindow(window, GW_OWNER) != owner ||
            !SetWindowPos(window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
            reply({}, QStringLiteral("Windows did not attach the plugin window to its Billfish owner.")); return;
        }
        reply({{QStringLiteral("attached"), true}, {QStringLiteral("owner_hwnd"), QString::number(reinterpret_cast<quintptr>(owner))},
            {QStringLiteral("window_hwnd"), encodedHandle}, {QStringLiteral("owner_topmost"), bool(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST)}});
#else
        reply({}, QStringLiteral("Native plugin window ownership is available only on Windows."));
#endif
        return;
    }
    if (type != QStringLiteral("host_call") && (plugin->status != QStringLiteral("running") || plugin->requestId.isEmpty() ||
        message.value(QStringLiteral("id")).toString() != plugin->requestId)) return;
    if (type == QStringLiteral("progress")) {
        const int total = qMax(0, message.value(QStringLiteral("total")).toInt());
        emit progress(plugin->requestId, qBound(0, message.value(QStringLiteral("completed")).toInt(), total), total,
                      message.value(QStringLiteral("message")).toString().left(4096));
    } else if (type == QStringLiteral("host_call")) {
        const auto operation = message.value(QStringLiteral("operation")).toString();
        const auto childId = message.value(QStringLiteral("call_id")).toString();
        if (childId.isEmpty() || childId.size() > 200 || !sessionActive) return;
        if (plugin->libraryRoot.isEmpty()) {
            send(plugin, {{QStringLiteral("type"), QStringLiteral("host_result")}, {QStringLiteral("call_id"), childId},
                          {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("library_not_open")},
                          {QStringLiteral("message"), QStringLiteral("This plugin session has no library context.")}}}}); return;
        }
        if (!allowedOperations.contains(operation) || !plugin->manifest.value(QStringLiteral("permissions")).toArray().contains(operation)) {
            send(plugin, {{QStringLiteral("type"), QStringLiteral("host_result")}, {QStringLiteral("call_id"), childId},
                          {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("permission_denied")},
                          {QStringLiteral("message"), QStringLiteral("Operation is not declared in plugin permissions.")}}}}); return;
        }
        if (hostRequests_.size() >= 256) {
            send(plugin, {{QStringLiteral("type"), QStringLiteral("host_result")}, {QStringLiteral("call_id"), childId},
                          {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("host_busy")},
                          {QStringLiteral("message"), QStringLiteral("Too many pending host calls.")}}}}); return;
        }
        const auto internalId = QStringLiteral("plugin-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        hostRequests_.insert(internalId, {plugin, childId, operation});
        emit hostCall(internalId, operation, message.value(QStringLiteral("arguments")).toObject(), plugin->libraryRoot);
    } else if (type == QStringLiteral("result") || type == QStringLiteral("error")) {
        const auto requestId = plugin->requestId; plugin->requestId.clear(); plugin->requestKind.clear();
        plugin->status = QStringLiteral("ready");
        if (type == QStringLiteral("result")) emit finished(requestId, message.value(QStringLiteral("data")).toObject());
        else emit failed(requestId, message.value(QStringLiteral("code")).toString(QStringLiteral("plugin_error")),
                         message.value(QStringLiteral("message")).toString());
        emit changed();
    }
}

void PluginManager::completeHostCall(const QString& id, const QJsonObject& data) {
    const auto it = hostRequests_.find(id); if (it == hostRequests_.end()) return;
    const auto request = it.value(); hostRequests_.erase(it);
    QJsonObject reply{{QStringLiteral("type"), request.replyType.isEmpty() ? QStringLiteral("host_result") : request.replyType},
                      {request.replyType.isEmpty() ? QStringLiteral("call_id") : QStringLiteral("request_id"), request.childId}, {QStringLiteral("data"), data}};
    if (QJsonDocument(reply).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024) {
        const auto code = request.operation == QStringLiteral("items.selected") ? QStringLiteral("selection_too_large") : QStringLiteral("response_too_large");
        reply.remove(QStringLiteral("data"));
        reply.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
            {QStringLiteral("message"), QStringLiteral("The complete host response exceeds the plugin 4 MiB message limit; no items were truncated.")}});
    }
    send(request.plugin, reply);
}

void PluginManager::failHostCall(const QString& id, const QString& code, const QString& message, const QJsonObject& details) {
    const auto it = hostRequests_.find(id); if (it == hostRequests_.end()) return;
    const auto request = it.value(); hostRequests_.erase(it);
    send(request.plugin, {{QStringLiteral("type"), request.replyType.isEmpty() ? QStringLiteral("host_result") : request.replyType},
                         {request.replyType.isEmpty() ? QStringLiteral("call_id") : QStringLiteral("request_id"), request.childId},
                         {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code.left(128)}, {QStringLiteral("message"), message.left(4096)},
                         {QStringLiteral("details"), details}}}});
}

} // namespace billfish::automation
