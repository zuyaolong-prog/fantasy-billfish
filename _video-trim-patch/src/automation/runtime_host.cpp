#include "automation/runtime_host.hpp"
#include "automation/automation_controller.hpp"
#include "automation/app_control.hpp"

#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLayout>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSaveFile>
#include <QSet>
#include <QShortcut>
#include <QStandardPaths>
#include <QThread>
#include <QTabWidget>
#include <QTimer>
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
QPointer<RuntimeHost> currentHost;
constexpr int maximumRequestBytes = 1024 * 1024;
constexpr int maximumObjects = 20000;
QString keyFor(const QObject* object) {
    return QString::number(reinterpret_cast<quintptr>(object), 16);
}
}

RuntimeHost::RuntimeHost(QObject* pluginInstance, QObject* parent)
    : QObject(parent), pluginInstance_(pluginInstance) {
    setObjectName(QStringLiteral("BillfishAutomationRuntimeHost"));
    currentHost = this;
    appControl_ = new AppControl(this);
    appControl_->setContextProvider([this] { return infoProvider_ ? infoProvider_() : QJsonObject(); });
    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    connect(server_, &QLocalServer::newConnection, this, &RuntimeHost::acceptClients);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, [this] { server_->close(); removeDescriptor(); });
}

RuntimeHost::~RuntimeHost() {
    server_->close();
    removeDescriptor();
}

RuntimeHost* RuntimeHost::instance() { return currentHost.data(); }

void RuntimeHost::setInfoProvider(std::function<QJsonObject()> provider) { infoProvider_ = std::move(provider); }

bool RuntimeHost::start() {
    if (server_->isListening()) return true;
    pipeName_ = QStringLiteral("BillfishAutomation-v1-%1").arg(QCoreApplication::applicationPid());
    if (!server_->listen(pipeName_)) return false;
    QString localRoot = qEnvironmentVariable("LOCALAPPDATA");
    if (localRoot.isEmpty()) localRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString directory = QDir(localRoot).filePath(QStringLiteral("Billfish/automation"));
    if (!QDir().mkpath(directory)) { server_->close(); return false; }
    descriptorPath_ = QDir(directory).filePath(QStringLiteral("host.json"));
    QSaveFile file(descriptorPath_);
    const QJsonObject descriptor {
        {QStringLiteral("protocol"), 1},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("pipe_name"), pipeName_},
        {QStringLiteral("install_root"), QCoreApplication::applicationDirPath()}
    };
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(descriptor).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
        server_->close(); return false;
    }
    QTimer::singleShot(0, this, &RuntimeHost::refreshObjects);
    return true;
}

void RuntimeHost::removeDescriptor() {
    if (descriptorPath_.isEmpty()) return;
    QFile file(descriptorPath_);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject descriptor = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    if (descriptor.value(QStringLiteral("pid")).toVariant().toLongLong() == QCoreApplication::applicationPid())
        QFile::remove(descriptorPath_);
}

void RuntimeHost::acceptClients() {
    while (server_->hasPendingConnections()) {
        QLocalSocket* socket = server_->nextPendingConnection();
        buffers_.insert(socket, QByteArray());
        socket->setReadBufferSize(maximumRequestBytes + 1);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { consume(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            buffers_.remove(socket);
            for (auto it = pending_.begin(); it != pending_.end();) {
                if (it.value().data() == socket) it = pending_.erase(it); else ++it;
            }
            socket->deleteLater();
        });
        QTimer::singleShot(30000, socket, [this, socket] {
            if (buffers_.contains(socket) && socket->state() != QLocalSocket::UnconnectedState)
                socket->disconnectFromServer();
        });
        consume(socket);
    }
}

void RuntimeHost::consume(QLocalSocket* socket) {
    if (!buffers_.contains(socket)) return;
    QByteArray& bytes = buffers_[socket];
    bytes.append(socket->readAll());
    const int newline = bytes.indexOf('\n');
    if (bytes.size() > maximumRequestBytes && (newline < 0 || newline > maximumRequestBytes)) {
        buffers_.remove(socket);
        sendReply(socket, {{QStringLiteral("id"), QString()}, {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("request_too_large")},
             {QStringLiteral("message"), QStringLiteral("Request exceeds 1 MiB")}}}});
        return;
    }
    if (newline < 0) return;
    const QByteArray line = bytes.left(newline);
    buffers_.remove(socket);
    dispatch(socket, line);
}

void RuntimeHost::dispatch(QLocalSocket* socket, const QByteArray& line) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    QJsonObject request = document.object();
    const QString id = request.value(QStringLiteral("id")).toString();
    const QString operation = request.value(QStringLiteral("operation")).toString();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || id.isEmpty() ||
        operation.isEmpty() || !request.value(QStringLiteral("arguments")).isObject() ||
        (request.contains(QStringLiteral("library_root")) && !request.value(QStringLiteral("library_root")).isString())) {
        sendReply(socket, {{QStringLiteral("id"), id}, {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("invalid_request")},
             {QStringLiteral("message"), QStringLiteral("Expected id, operation and arguments object")}}}});
        return;
    }
    if (pending_.contains(id)) {
        sendReply(socket, {{QStringLiteral("id"), id}, {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("duplicate_request")},
             {QStringLiteral("message"), QStringLiteral("Request id is already pending")}}}});
        return;
    }
    pending_.insert(id, socket);
    if (operation == QStringLiteral("host.info")) {
        QJsonObject info{{QStringLiteral("protocol"), 1},
            {QStringLiteral("pid"), QCoreApplication::applicationPid()},
            {QStringLiteral("pipe_name"), pipeName_},
            {QStringLiteral("install_root"), QCoreApplication::applicationDirPath()}};
        if (infoProvider_) {
            const QJsonObject nativeInfo = infoProvider_();
            for (auto it = nativeInfo.constBegin(); it != nativeInfo.constEnd(); ++it) info.insert(it.key(), it.value());
        }
        replySuccess(id, info);
    } else if (operation.startsWith(QStringLiteral("ui."))) {
        const QJsonObject result = appControl_->handle(operation, request.value(QStringLiteral("arguments")).toObject(), id);
        if (result.value(QStringLiteral("ok")).toBool()) replySuccess(id, result.value(QStringLiteral("data")));
        else {
            const QJsonObject error = result.value(QStringLiteral("error")).toObject();
            replyError(id, error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString());
        }
    } else if (operation == QStringLiteral("diagnostics.objects")) {
        refreshObjects();
        const QJsonObject arguments = request.value(QStringLiteral("arguments")).toObject();
        const QString classFilter = arguments.value(QStringLiteral("class_contains")).toString();
        const QString nameFilter = arguments.value(QStringLiteral("object_name_contains")).toString();
        const QString rootClass = arguments.value(QStringLiteral("root_class")).toString();
        const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(1000), maximumObjects);
        QJsonArray results;
        int matchingCount = 0;
        for (const auto& pointer : objects_) {
            if (!pointer) continue;
            if (!rootClass.isEmpty()) {
                bool underRoot = false;
                for (QObject* ancestor = pointer.data(); ancestor; ancestor = ancestor->parent()) {
                    if (QString::fromLatin1(ancestor->metaObject()->className()) == rootClass) {
                        underRoot = true;
                        break;
                    }
                }
                if (!underRoot) continue;
            }
            if (!QString::fromLatin1(pointer->metaObject()->className()).contains(classFilter, Qt::CaseInsensitive) ||
                !pointer->objectName().contains(nameFilter, Qt::CaseInsensitive)) continue;
            ++matchingCount;
            if (results.size() < limit) results.append(objectMetadata(pointer.data(), arguments.value(QStringLiteral("include_values")).toBool()));
        }
        replySuccess(id, QJsonObject{{QStringLiteral("objects"), results},
            {QStringLiteral("observed_count"), objects_.size()},
            {QStringLiteral("matching_count"), matchingCount},
            {QStringLiteral("truncated"), matchingCount > results.size() || objects_.size() >= maximumObjects}});
    } else {
        emit requestReceived(id, request);
    }
}

void RuntimeHost::sendReply(QLocalSocket* socket, const QJsonObject& reply) {
    if (!socket || socket->state() == QLocalSocket::UnconnectedState) return;
    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
    socket->disconnectFromServer();
}

void RuntimeHost::replySuccess(const QString& requestId, const QJsonValue& data) {
    const auto socket = pending_.take(requestId);
    sendReply(socket.data(), {{QStringLiteral("id"), requestId}, {QStringLiteral("ok"), true}, {QStringLiteral("data"), data}});
}

void RuntimeHost::replyError(const QString& requestId, const QString& code, const QString& message, const QJsonObject& details) {
    const auto socket = pending_.take(requestId);
    QJsonObject error{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!details.isEmpty()) error.insert(QStringLiteral("details"), details);
    sendReply(socket.data(), {{QStringLiteral("id"), requestId}, {QStringLiteral("ok"), false},
        {QStringLiteral("error"), error}});
}

void RuntimeHost::refreshObjects() {
    QList<QObject*> queue;
    queue.append(QCoreApplication::instance());
    if (pluginInstance_) queue.append(pluginInstance_.data());
    for (QWidget* widget : QApplication::allWidgets()) queue.append(widget);
    // Only consult already loaded plugin modules. Calling the Qt plugin accessor
    // returns the existing plugin singleton and never loads a new DLL here.
    const QStringList moduleNames {
        QStringLiteral("basemodule.dll"), QStringLiteral("bfright.dll"), QStringLiteral("bfwebview.dll"),
        QStringLiteral("datacenter.dll"), QStringLiteral("datamodel.dll"), QStringLiteral("importtask.dll"),
        QStringLiteral("lutmanager.dll"), QStringLiteral("netserver.dll"), QStringLiteral("preview.dll"),
        QStringLiteral("toolmodule.dll"), QStringLiteral("treemodule.dll"), QStringLiteral("usercenter.dll"),
        QStringLiteral("viewmodule.dll")
    };
    for (const QString& name : moduleNames) {
        const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ui_plugins/") + name);
        const HMODULE module = GetModuleHandleW(reinterpret_cast<LPCWSTR>(path.utf16()));
        if (!module) continue;
        using InstanceFunction = QObject* (*)();
        const auto instanceFunction = reinterpret_cast<InstanceFunction>(GetProcAddress(module, "qt_plugin_instance"));
        if (instanceFunction) {
            if (QObject* object = instanceFunction()) queue.append(object);
        }
    }
    objects_.clear();
    QSet<QObject*> seen;
    for (int index = 0; index < queue.size() && objects_.size() < maximumObjects; ++index) {
        QObject* object = queue.at(index);
        if (!object || seen.contains(object) || object == this) continue;
        seen.insert(object);
        objects_.append(QPointer<QObject>(object));
        for (QObject* child : object->children()) queue.append(child);
        // Native menus also contain actions owned outside their QObject tree.
        if (auto* widget = qobject_cast<QWidget*>(object)) {
            for (QAction* action : widget->actions()) queue.append(action);
        }
    }
}

QList<QObject*> RuntimeHost::observedObjects() const {
    QList<QObject*> result;
    for (const auto& object : objects_) if (object) result.append(object.data());
    return result;
}

QObject* RuntimeHost::findObjectByClass(const QByteArray& className) {
    refreshObjects();
    for (const auto& object : objects_) if (object && object->inherits(className.constData())) return object.data();
    return nullptr;
}

QJsonObject RuntimeHost::objectMetadata(const QObject* object, bool includeValues) const {
    const QMetaObject* meta = object->metaObject();
    QJsonArray classChain;
    for (const QMetaObject* base = meta; base; base = base->superClass())
        classChain.append(QString::fromLatin1(base->className()));
    QJsonArray methods;
    for (int index = 0; index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        QJsonArray parameterTypes;
        for (const QByteArray& type : method.parameterTypes()) parameterTypes.append(QString::fromLatin1(type));
        methods.append(QJsonObject{{QStringLiteral("signature"), QString::fromLatin1(method.methodSignature())},
            {QStringLiteral("return_type"), QString::fromLatin1(method.typeName())},
            {QStringLiteral("parameter_types"), parameterTypes}, {QStringLiteral("kind"), static_cast<int>(method.methodType())}});
    }
    QJsonArray properties;
    for (int index = 0; index < meta->propertyCount(); ++index) {
        const QMetaProperty property = meta->property(index);
        properties.append(QJsonObject{{QStringLiteral("name"), QString::fromLatin1(property.name())},
            {QStringLiteral("type"), QString::fromLatin1(property.typeName())}});
    }
    QJsonObject result {{QStringLiteral("key"), keyFor(object)},
        {QStringLiteral("parent_key"), object->parent() ? QJsonValue(keyFor(object->parent())) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("class"), QString::fromLatin1(meta->className())}, {QStringLiteral("class_chain"), classChain},
        {QStringLiteral("object_name"), object->objectName()}, {QStringLiteral("methods"), methods},
        {QStringLiteral("properties"), properties}};
    if (const auto* widget = qobject_cast<const QWidget*>(object)) {
        const QRect geometry = widget->geometry();
        result.insert(QStringLiteral("geometry"), QJsonObject{{QStringLiteral("x"), geometry.x()},
            {QStringLiteral("y"), geometry.y()}, {QStringLiteral("width"), geometry.width()},
            {QStringLiteral("height"), geometry.height()}});
        result.insert(QStringLiteral("is_window"), widget->isWindow());
        result.insert(QStringLiteral("visible"), widget->isVisible());
        result.insert(QStringLiteral("layout_class"), widget->layout()
            ? QJsonValue(QString::fromLatin1(widget->layout()->metaObject()->className())) : QJsonValue(QJsonValue::Null));
    }
    if (includeValues) {
        QJsonObject values;
        if (const auto* widget = qobject_cast<const QWidget*>(object)) {
            values.insert(QStringLiteral("enabled"), widget->isEnabled());
            values.insert(QStringLiteral("minimum_width"), widget->minimumWidth());
            values.insert(QStringLiteral("maximum_width"), widget->maximumWidth());
            if (const QLayout* layout = widget->layout()) {
                const QMargins margins = layout->contentsMargins();
                values.insert(QStringLiteral("layout_margins"), QJsonArray{margins.left(), margins.top(), margins.right(), margins.bottom()});
                values.insert(QStringLiteral("layout_spacing"), layout->spacing());
            }
        }
        if (const auto* action = qobject_cast<const QAction*>(object)) {
            values.insert(QStringLiteral("text"), action->text());
            values.insert(QStringLiteral("enabled"), action->isEnabled());
            values.insert(QStringLiteral("visible"), action->isVisible());
            if (action->menu()) values.insert(QStringLiteral("menu_key"), keyFor(action->menu()));
            QJsonArray shortcuts;
            for (const auto& shortcut : action->shortcuts()) shortcuts.append(shortcut.toString(QKeySequence::PortableText));
            values.insert(QStringLiteral("shortcuts"), shortcuts);
            QJsonArray associated;
            for (const QWidget* widget : action->associatedWidgets()) associated.append(keyFor(widget));
            values.insert(QStringLiteral("associated_widget_keys"), associated);
        } else if (const auto* shortcut = qobject_cast<const QShortcut*>(object)) {
            values.insert(QStringLiteral("key"), shortcut->key().toString(QKeySequence::PortableText));
            values.insert(QStringLiteral("enabled"), shortcut->isEnabled());
        } else if (const auto* button = qobject_cast<const QAbstractButton*>(object)) {
            values.insert(QStringLiteral("text"), button->text());
            values.insert(QStringLiteral("enabled"), button->isEnabled());
        } else if (const auto* edit = qobject_cast<const QLineEdit*>(object)) {
            if (edit->echoMode() == QLineEdit::Normal) values.insert(QStringLiteral("text"), edit->text());
        } else if (const auto* label = qobject_cast<const QLabel*>(object)) {
            values.insert(QStringLiteral("text"), label->text());
        } else if (const auto* tabs = qobject_cast<const QTabWidget*>(object)) {
            QJsonArray pages;
            for (int index = 0; index < tabs->count(); ++index) pages.append(QJsonObject{
                {QStringLiteral("text"), tabs->tabText(index)}, {QStringLiteral("key"), keyFor(tabs->widget(index))}});
            values.insert(QStringLiteral("tabs"), pages);
            values.insert(QStringLiteral("current_index"), tabs->currentIndex());
        } else if (const auto* menu = qobject_cast<const QMenu*>(object)) {
            values.insert(QStringLiteral("title"), menu->title());
            QJsonArray actions;
            for (const QAction* menuAction : menu->actions()) actions.append(keyFor(menuAction));
            values.insert(QStringLiteral("action_keys"), actions);
        }
        result.insert(QStringLiteral("values"), values);
    }
    return result;
}

void installAutomationInjection(QObject* pluginInstance) {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return;
    const QPointer<QObject> pluginPointer(pluginInstance);
    const auto install = [app, pluginPointer] {
        if (RuntimeHost::instance()) return;
        auto* host = new RuntimeHost(pluginPointer.data(), app);
        new AutomationController(host, host);
        if (!host->start()) host->deleteLater();
    };
    if (QThread::currentThread() == app->thread()) install();
    else QMetaObject::invokeMethod(app, install, Qt::QueuedConnection);
}

} // namespace billfish::automation
