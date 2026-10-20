#include "automation/native_adapter.hpp"

#include <QCoreApplication>
#include <QApplication>
#include <QAbstractItemView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaMethod>
#include <QMetaType>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVector>

#include <cstring>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <windows.h>

namespace billfish::automation {
namespace {

// This exact layout is consumed by the frozen BFTagAction helper at RVA 0x767E0.
// No metatype is registered: the host already owns its native registration.
struct DynamicTag final {
    qint64 id = 0;
    QString name;
};
static_assert(sizeof(DynamicTag) == 16, "Frozen Windows x64 Qt5 ABI required");

// Frozen BFTagAction::slotCreateTags consumes these exact offsets (RVA 0x76DF0).
struct ITagNodeInfo final {
    QString name;
    qint64 pid = 0;
    int color = 0;
    qint64 groupId = 0;
    int icon = 0;
    double sequence = 0;
    qint64 id = 0;
};
static_assert(sizeof(ITagNodeInfo) == 56 && offsetof(ITagNodeInfo, pid) == 8 &&
              offsetof(ITagNodeInfo, groupId) == 0x18 && offsetof(ITagNodeInfo, sequence) == 0x28 &&
              offsetof(ITagNodeInfo, id) == 0x30, "Frozen native tag node ABI required");

// The frozen core 618F0 and importtask 9D20 consume this record. Keep the
// original registration untouched; common-task submission is a native virtual.
struct CommonImportTask final {
    int kind = 0;
    QString path;
    int folderId = 0;
    int mode = 1; // BFImportMode::ImportCopy, never Cut or Index.
    QString metadata;
    bool defaultFlag = true; // Match the core entry; no broader meaning assumed.
};
static_assert(sizeof(CommonImportTask) == 0x28 && offsetof(CommonImportTask, path) == 8 &&
              offsetof(CommonImportTask, folderId) == 0x10 && offsetof(CommonImportTask, mode) == 0x14 &&
              offsetof(CommonImportTask, metadata) == 0x18 && offsetof(CommonImportTask, defaultFlag) == 0x20,
              "Frozen native common-import ABI required");

struct Module final {
    HMODULE handle = nullptr;
    quintptr begin = 0;
    quintptr end = 0;
};

bool readable(const void* address, const size_t size) {
    if (!address || size == 0) return false;
    MEMORY_BASIC_INFORMATION information{};
    if (!VirtualQuery(address, &information, sizeof(information)) ||
        information.State != MEM_COMMIT || (information.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const auto pointer = reinterpret_cast<quintptr>(address);
    const auto regionEnd = reinterpret_cast<quintptr>(information.BaseAddress) + information.RegionSize;
    return pointer <= regionEnd && size <= regionEnd - pointer;
}

template <typename T>
bool read(const void* object, const size_t offset, T& result) {
    if (!object) return false;
    const auto* address = static_cast<const unsigned char*>(object) + offset;
    if (!readable(address, sizeof(T))) return false;
    std::memcpy(&result, address, sizeof(T));
    return true;
}

bool inModule(const void* pointer, const Module& module) {
    const auto address = reinterpret_cast<quintptr>(pointer);
    return address >= module.begin && address < module.end;
}

bool moduleIdentity(const wchar_t* name, const QByteArray& expectedHash, Module& result, QString& reason) {
    result.handle = GetModuleHandleW(name);
    if (!result.handle) {
        reason = QStringLiteral("The required native module is not loaded.");
        return false;
    }
    wchar_t path[32768]{};
    const DWORD size = GetModuleFileNameW(result.handle, path, 32768);
    QFile file(QString::fromWCharArray(path, static_cast<int>(size)));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!size || size >= 32768 || !file.open(QIODevice::ReadOnly) || !hash.addData(&file) ||
        hash.result().toHex() != expectedHash) {
        reason = QStringLiteral("The loaded native module does not match the supported frozen runtime.");
        return false;
    }
    result.begin = reinterpret_cast<quintptr>(result.handle);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(result.handle);
    if (!readable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(result.begin + dos->e_lfanew);
    if (!readable(pe, sizeof(*pe)) || pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        reason = QStringLiteral("The native module architecture is unsupported.");
        return false;
    }
    result.end = result.begin + pe->OptionalHeader.SizeOfImage;
    return true;
}

QString normalizedPath(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool samePath(const QString& left, const QString& right) {
    return !left.isEmpty() && !right.isEmpty() &&
        normalizedPath(left).compare(normalizedPath(right), Qt::CaseInsensitive) == 0;
}

bool positiveId(const QJsonValue& value, qint64& result, bool allowZero = false) {
    if (!value.isString()) return false;
    const QString text = value.toString();
    if (text.isEmpty() || text.size() > 19 || (text.size() > 1 && text.startsWith(QLatin1Char('0')))) return false;
    for (const QChar character : text) if (character < QLatin1Char('0') || character > QLatin1Char('9')) return false;
    bool valid = false;
    result = text.toLongLong(&valid, 10);
    return valid && result >= (allowZero ? 0 : 1);
}

bool allowedKeys(const QJsonObject& object, const QStringList& allowed) {
    for (auto it = object.begin(); it != object.end(); ++it) if (!allowed.contains(it.key())) return false;
    return true;
}

bool parseIds(const QJsonValue& value, int maximum, QVector<qint64>& result) {
    if (!value.isArray()) return false;
    const auto array = value.toArray();
    if (array.isEmpty() || array.size() > maximum) return false;
    QSet<qint64> seen;
    for (const auto& item : array) {
        qint64 id = 0;
        if (!positiveId(item, id) || seen.contains(id)) return false;
        seen.insert(id);
        result.append(id);
    }
    return true;
}

bool nativeListBounds(const void* object, size_t offset, void*& data, int& begin, int& end) {
    int allocation = 0;
    return read(object, offset, data) && read(data, 4, allocation) && read(data, 8, begin) &&
        read(data, 12, end) && allocation >= 0 && begin >= 0 && end >= begin && end <= allocation;
}

bool importResultIds(const BFTaskResultInfo& result, QList<qint64>& ids) {
    void* data = nullptr;
    int begin = 0, end = 0;
    // Native BFTaskResultInfo+40 is QList<qint64>, not QVector or a guessed ID.
    // Read its original Qt5 backing without copying an opaque result value or
    // taking ownership of its reference count.
    if (!nativeListBounds(&result, 0x40, data, begin, end) || end - begin > 500) return false;
    for (int index = begin; index < end; ++index) {
        qint64 id = 0;
        if (!read(data, 0x10 + size_t(index) * sizeof(qint64), id) || id <= 0 || ids.contains(id)) return false;
        ids.append(id);
    }
    return true;
}

bool nativeString(const void* object, size_t offset, QString& result) {
    void* data = nullptr;
    int length = 0;
    if (!read(object, offset, data) || !readable(data, 24) || !read(data, 4, length) || length < 0 || length > 32767)
        return false;
    const auto* value = reinterpret_cast<const QString*>(static_cast<const unsigned char*>(object) + offset);
    result = *value; // A real QString copy retains the native shared backing.
    return true;
}

bool readConfigObject(const QString& path, QJsonObject& result) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    result = document.object();
    return true;
}

} // namespace

class NativeAdapterPrivate final {
public:
    explicit NativeAdapterPrivate(NativeAdapter* object) : q(object) {
        ownerTimer = new QTimer(q);
        ownerTimer->setInterval(250);
        QObject::connect(ownerTimer, &QTimer::timeout, q, &NativeAdapter::verifyOwner);
    }

    struct Owner final {
        QObject* manager = nullptr;
        void* library = nullptr;
        void* fileManager = nullptr;
        QString root;
        QString reason;
        int state = -1;
        bool ready = false;
    };
    struct Step final {
        qint64 itemId = 0;
        QString field;
        QString value;
        QVector<qint64> selectedTags;
    };
    struct Import final {
        QString id, path, name, origin, folderId;
        Owner owner;
        QSharedPointer<QObject> manager;
        QSharedPointer<QObject> renameAction;
        QPointer<QThread> worker;
        QList<qint64> ids;
        QString errorCode, errorMessage;
        int taskResults = 0;
        int announcedTotal = -1, success = -1, failed = -1;
        bool submitted = false;
        bool queueFinished = false;
        bool abortRequested = false;
        bool draining = false;
        bool earlyDrainQueued = false;
        bool workerFinished = false;
        qlonglong renameActionId = 0;
        bool renameFinished = false;
    };

    NativeAdapter* q;
    Module rightModule, dataModule, libraryModule, toolModule, coreModule, viewModule, interfaceModule, modelModule, importModule, configModule;
    bool identitiesChecked = false;
    QString identityError;
    QTimer* ownerTimer;
    QPointer<QObject> watchedManager;
    QPointer<QObject> lifecycleManager;
    QMetaObject::Connection lifecycleClosed, lifecycleLoaded, lifecycleDestroyed;
    QString activeRoot;
    QMetaObject::Connection closedConnection, destroyedConnection;
    Owner requestOwner;
    QString requestId, operation, mode;
    QVector<Step> steps;
    QVector<qint64> tagIds;
    QVector<QString> typeNames;
    ITagNodeInfo newTag;
    QJsonArray results;
    QHash<qint64, int> resultIndexes;
    int stepIndex = 0;
    int failedSteps = 0;
    qlonglong activeActionId = 0;
    QSharedPointer<QObject> fileAction, tagAction, typeAction;
    std::unique_ptr<Import> importing;
    bool shutdownPending = false;
    bool nativeShutdownHeld = false;
    bool queuedQuitPending = false;
    int shutdownTimeoutMs = 5000;
    QPointer<QObject> shutdownManager;
    QPointer<QTimer> shutdownTimer;
    QMetaObject::Connection shutdownConnection;

    bool busy() const { return !requestId.isEmpty() || bool(importing); }

    bool ensureNativeShutdownGuard(QString& reason) {
        if (!coreModule.handle && !moduleIdentity(L"Billfish.core.exe",
            QByteArrayLiteral("47abf9bf0464d88c1ca259965bbdd00165317fbf1fea8308c339c548c405c24f"), coreModule, reason)) {
            coreModule = Module(); return false;
        }
        if (GetModuleHandleW(L"Billfish.core.exe") != coreModule.handle) {
            reason = QStringLiteral("The native application module binding changed."); return false;
        }
        QList<QObject*> managers;
        for (QObject* child : QCoreApplication::instance()->children()) {
            if (QByteArray(child->metaObject()->className()) == QByteArrayLiteral("PluginManager")) managers.append(child);
        }
        if (managers.size() != 1) {
            reason = QStringLiteral("A unique native application shutdown manager is required before import."); return false;
        }
        QObject* manager = managers.first();
        void* table = nullptr;
        void* interfaceTable = nullptr;
        void* delay = nullptr;
        void* continuation = nullptr;
        void* timerTable = nullptr;
        const auto expectedTimerTable = GetProcAddress(GetModuleHandleW(L"Qt5Core.dll"), "??_7QTimer@@6B@");
        auto* timer = reinterpret_cast<QTimer*>(reinterpret_cast<unsigned char*>(manager) + 0x20);
        int state = -1;
        if (manager->thread() != q->thread() || !read(manager, 0, table) ||
            reinterpret_cast<quintptr>(table) != coreModule.begin + 0x136BF8 ||
            !read(manager, 0x10, interfaceTable) || reinterpret_cast<quintptr>(interfaceTable) != coreModule.begin + 0x136C68 ||
            !read(interfaceTable, 0xC8, delay) || reinterpret_cast<quintptr>(delay) != coreModule.begin + 0xCAC60 ||
            !read(interfaceTable, 0xD0, continuation) || reinterpret_cast<quintptr>(continuation) != coreModule.begin + 0xCA8D0 ||
            !read(manager, 0x20, timerTable) || !expectedTimerTable ||
            reinterpret_cast<quintptr>(timerTable) != reinterpret_cast<quintptr>(expectedTimerTable) ||
            !timer->inherits("QTimer") || timer->thread() != q->thread() ||
            !read(manager, 0x18, state) || state != 0 ||
            manager->metaObject()->indexOfSignal("shutdownStarted()") < 0 ||
            manager->metaObject()->indexOfSlot("delayShutdown()") < 0 ||
            manager->metaObject()->indexOfSlot("continueShutdown()") < 0) {
            reason = QStringLiteral("The native shutdown protocol identity or state is unsupported."); return false;
        }
        if (shutdownManager == manager && shutdownConnection) return true;
        QObject::disconnect(shutdownConnection);
        shutdownManager = manager;
        shutdownTimer = timer; // Embedded QTimer, not an owned QObject child.
        shutdownConnection = QObject::connect(manager, SIGNAL(shutdownStarted()), q, SLOT(nativeShutdownStarted()), Qt::DirectConnection);
        if (!shutdownConnection) {
            shutdownManager.clear(); shutdownTimer.clear();
            reason = QStringLiteral("The native shutdown protocol could not be bound."); return false;
        }
        return true;
    }

    bool stillImportOwner() {
        if (!importing) return false;
        const Owner current = owner();
        return current.ready && current.manager == importing->owner.manager && current.library == importing->owner.library &&
            samePath(current.root, importing->owner.root);
    }

    bool setImportState(int state) {
        if (!importing || !importing->manager || GetModuleHandleW(L"importtask.dll") != importModule.handle) return false;
        void* table = nullptr;
        void* function = nullptr;
        if (!read(importing->manager.data(), 0, table) || reinterpret_cast<quintptr>(table) != importModule.begin + 0x152F8 ||
            !read(table, 0x90, function) || reinterpret_cast<quintptr>(function) != importModule.begin + 0xA860) return false;
        using SetState = void (__fastcall*)(void*, int);
        reinterpret_cast<SetState>(function)(importing->manager.data(), state);
        return true;
    }

    void abortImport(const QString& code, const QString& message) {
        if (!importing) return;
        if (importing->errorCode.isEmpty()) { importing->errorCode = code; importing->errorMessage = message; }
        if (!importing->abortRequested && importing->manager && !importing->draining) {
            importing->abortRequested = true;
            setImportState(1); // Native cooperative cancellation; never a rollback claim.
        }
        if (!importing->submitted && !importing->earlyDrainQueued) {
            importing->earlyDrainQueued = true;
            QTimer::singleShot(0, q, &NativeAdapter::drainImport);
        }
    }

    bool createImportManager(QString& reason) {
        if (!importModule.handle && !moduleIdentity(L"importtask.dll",
            QByteArrayLiteral("ff0998019d41a379b56b9ce1850206a336a47fce5055d60802a64350ee6e0ba4"), importModule, reason)) {
            importModule = Module();
            return false;
        }
        if (GetModuleHandleW(L"importtask.dll") != importModule.handle) {
            reason = QStringLiteral("The native import module binding changed."); return false;
        }
        using Instance = QObject* (*)();
        const auto instance = reinterpret_cast<Instance>(GetProcAddress(importModule.handle, "qt_plugin_instance"));
        if (reinterpret_cast<quintptr>(instance) != importModule.begin + 0x108A0) {
            reason = QStringLiteral("The native import plugin accessor is unsupported."); return false;
        }
        QObject* plugin = instance(); // Already loaded native plugin singleton.
        void* table = nullptr;
        void* manager = nullptr;
        void* function = nullptr;
        if (!plugin || QByteArray(plugin->metaObject()->className()) != QByteArrayLiteral("ImportTaskPlugin") ||
            plugin->thread() != q->thread() || !read(plugin, 0, table) ||
            reinterpret_cast<quintptr>(table) != importModule.begin + 0x161D8) {
            reason = QStringLiteral("The current native import plugin identity is unsupported."); return false;
        }
        void* taskInterface = static_cast<unsigned char*>(static_cast<void*>(plugin)) + 0x18;
        if (!read(taskInterface, 0, table) || reinterpret_cast<quintptr>(table) != importModule.begin + 0x16278 ||
            !read(taskInterface, 8, manager) || manager != importing->owner.manager || !read(table, 8, function) ||
            reinterpret_cast<quintptr>(function) != importModule.begin + 0xEC30) {
            reason = QStringLiteral("The native import factory is not bound to the current library owner."); return false;
        }
        // Native factory returns its real QSharedPointer and deleter (RCX=this,
        // RDX=return buffer). Its manager creates the native worker QThread.
        using Factory = void* (__fastcall*)(void*, QSharedPointer<QObject>*);
        reinterpret_cast<Factory>(function)(taskInterface, &importing->manager);
        void* boundManager = nullptr;
        QThread* worker = nullptr;
        if (!importing->manager || !read(importing->manager.data(), 0, table) ||
            reinterpret_cast<quintptr>(table) != importModule.begin + 0x152F8 ||
            !read(importing->manager.data(), 0x38, worker) || !worker || !read(worker, 0, table) ||
            reinterpret_cast<quintptr>(table) != importModule.begin + 0x15290) {
            reason = QStringLiteral("The native import manager or worker identity is unsupported."); return false;
        }
        // Retain and observe the known native worker before checking later
        // owner/Qt guards, so a rejected factory result still drains its thread.
        importing->worker = worker;
        QObject::connect(worker, &QThread::finished, q, &NativeAdapter::importWorkerFinished, Qt::QueuedConnection);
        if (importing->manager->thread() != q->thread() || !importing->manager->inherits("IImportTaskManager") ||
            !read(importing->manager.data(), 0x10, boundManager) || boundManager != importing->owner.manager ||
            !worker->inherits("QThread") || worker->thread() != q->thread() || worker->parent()) {
            reason = QStringLiteral("The native import manager or worker ownership is unsupported."); return false;
        }
        return true;
    }

    bool identities() {
        if (identitiesChecked) return identityError.isEmpty();
        if (!GetModuleHandleW(L"bfright.dll") || !GetModuleHandleW(L"datacenter.dll") ||
            !GetModuleHandleW(L"libBFDataCenter.dll")) {
            identityError = QStringLiteral("The required native modules have not loaded yet.");
            return false;
        }
        identityError.clear();
        identitiesChecked = true;
        if (!moduleIdentity(L"bfright.dll", QByteArrayLiteral("645091e7584e3a317d51fc687aa673e068470da0173a5906762e50e53d0a224d"), rightModule, identityError) ||
            !moduleIdentity(L"datacenter.dll", QByteArrayLiteral("c3086ed8efce3d2dc11a482b2798eaa1b5f171970b05a681015bece4d5b27f09"), dataModule, identityError) ||
            !moduleIdentity(L"libBFDataCenter.dll", QByteArrayLiteral("302c9b755b81ad97cd863ddfeb3caf58dded6dd662c9ea486c02e5d626459ce6"), libraryModule, identityError)) {
            if (identityError.isEmpty()) identityError = QStringLiteral("Native runtime identity could not be verified.");
            return false;
        }
        return true;
    }

    Owner owner() {
        Owner result;
        if (QThread::currentThread() != q->thread() || q->thread() != QCoreApplication::instance()->thread()) {
            result.reason = QStringLiteral("Native owner inspection requires the GUI thread.");
            return result;
        }
        if (!identities()) { result.reason = identityError; return result; }
        if (GetModuleHandleW(L"bfright.dll") != rightModule.handle ||
            GetModuleHandleW(L"datacenter.dll") != dataModule.handle ||
            GetModuleHandleW(L"libBFDataCenter.dll") != libraryModule.handle) {
            result.reason = QStringLiteral("The native runtime module binding changed.");
            return result;
        }
        void* manager = nullptr;
        void* table = nullptr;
        if (!read(reinterpret_cast<void*>(rightModule.begin), 0x4C418, manager) ||
            !read(manager, 0, table) || reinterpret_cast<quintptr>(table) != dataModule.begin + 0x139100) {
            result.reason = QStringLiteral("The current native data-center manager is unavailable.");
            return result;
        }
        result.manager = static_cast<QObject*>(manager);
        if (!result.manager->inherits("IDataCenterManager") || result.manager->thread() != q->thread() ||
            !read(manager, 0x10, result.state) || result.state != 2) {
            result.reason = QStringLiteral("The current native library is not ready.");
            return result;
        }
        if (!read(manager, 0x18, result.library) || !read(result.library, 0, table) ||
            reinterpret_cast<quintptr>(table) != libraryModule.begin + 0x53FE08 ||
            !read(result.library, 0x68, result.fileManager)) {
            result.reason = QStringLiteral("The current library owner identity is unsupported.");
            return result;
        }
        size_t pathBytes = 0;
        if (!read(result.fileManager, 0x10, pathBytes) || pathBytes == 0 || pathBytes > 32767) {
            result.reason = QStringLiteral("The current native library path is unavailable.");
            return result;
        }
        using PathGetter = const char* (__fastcall*)(void*);
        const auto getter = reinterpret_cast<PathGetter>(GetProcAddress(libraryModule.handle,
            "?libraryPath@CBFFileManager@Billfish@@QEAAPEBDXZ"));
        if (!getter || !inModule(reinterpret_cast<const void*>(getter), libraryModule)) {
            result.reason = QStringLiteral("The native library path getter is unsupported.");
            return result;
        }
        const char* path = getter(result.fileManager);
        if (!readable(path, pathBytes)) {
            result.reason = QStringLiteral("The native library path is not readable.");
            return result;
        }
        const QString root = QString::fromUtf8(path, static_cast<int>(pathBytes));
        if (!QFileInfo(root).isAbsolute() || root.contains(QChar::ReplacementCharacter) || root.contains(QChar(0))) {
            result.reason = QStringLiteral("The native library path is not a valid absolute UTF-8 path.");
            return result;
        }
        result.root = normalizedPath(root);
        result.ready = true;
        return result;
    }

    bool stillOwner() {
        const Owner current = owner();
        return current.ready && current.manager == requestOwner.manager && current.library == requestOwner.library &&
            samePath(current.root, requestOwner.root);
    }

    QSharedPointer<QObject> makeAction(const Owner& owner, const size_t slotOffset, const quintptr expectedFunction, const char* className) {
        void* table = nullptr;
        void* address = nullptr;
        QSharedPointer<QObject> result;
        if (!read(owner.manager, 0, table) || !read(table, slotOffset, address) ||
            reinterpret_cast<quintptr>(address) != dataModule.begin + expectedFunction) return result;
        // MSVC member-function ABI: RCX=this, RDX=shared-pointer return buffer.
        using Factory = void* (__fastcall*)(void*, QSharedPointer<QObject>*);
        reinterpret_cast<Factory>(address)(owner.manager, &result);
        if (!result || !result->inherits(className) || result->thread() != q->thread()) result.clear();
        return result;
    }

    void reset() {
        ownerTimer->stop();
        activeActionId = 0;
        QObject::disconnect(closedConnection);
        QObject::disconnect(destroyedConnection);
        if (fileAction) QObject::disconnect(fileAction.data(), nullptr, q, nullptr);
        if (tagAction) QObject::disconnect(tagAction.data(), nullptr, q, nullptr);
        if (typeAction) QObject::disconnect(typeAction.data(), nullptr, q, nullptr);
        // Defer wrapper destruction past the native signal emission stack.
        const auto oldFile = fileAction;
        const auto oldTag = tagAction;
        const auto oldType = typeAction;
        fileAction.clear();
        tagAction.clear();
        typeAction.clear();
        if (oldFile || oldTag || oldType) QTimer::singleShot(0, q, [oldFile, oldTag, oldType] {});
        requestId.clear();
        steps.clear();
        tagIds.clear();
        typeNames.clear();
        resultIndexes.clear();
        results = QJsonArray();
        stepIndex = 0;
        failedSteps = 0;
    }

    void fail(const QString& code, const QString& message) {
        const QString id = requestId;
        QJsonArray snapshot = results;
        for (int index = stepIndex; index < steps.size(); ++index) {
            const auto& step = steps.at(index);
            const int resultIndex = resultIndexes.value(step.itemId);
            QJsonObject item = snapshot.at(resultIndex).toObject();
            QJsonObject fields = item.value(QStringLiteral("fields")).toObject();
            const bool unknown = index == stepIndex && activeActionId > 0;
            QJsonObject field{{QStringLiteral("ok"), false},
                              {QStringLiteral("status"), unknown ? QStringLiteral("unknown") : QStringLiteral("not_submitted")}};
            if (unknown) field.insert(QStringLiteral("action_id"), QString::number(activeActionId));
            fields.insert(step.field, field);
            item.insert(QStringLiteral("fields"), fields);
            item.insert(QStringLiteral("ok"), false);
            snapshot.replace(resultIndex, item);
        }
        QJsonObject details{
            {QStringLiteral("operation"), operation}, {QStringLiteral("library_root"), requestOwner.root},
            {QStringLiteral("completed_steps"), stepIndex}, {QStringLiteral("failed_steps"), failedSteps},
            {QStringLiteral("remaining_steps"), steps.size() - stepIndex - (activeActionId > 0 ? 1 : 0)},
            {QStringLiteral("active_status"), activeActionId > 0 ? QStringLiteral("unknown") : QStringLiteral("none")},
            {QStringLiteral("results"), snapshot}, {QStringLiteral("rollback"), false}};
        if (activeActionId > 0) details.insert(QStringLiteral("active_action_id"), QString::number(activeActionId));
        reset();
        if (!id.isEmpty()) emit q->failed(id, code, message, details);
    }

    bool responseCode(const QSharedPointer<const IResponseInfo>& response, int& code) const {
        const void* object = response.data();
        void* table = nullptr;
        void* getter = nullptr;
        if (!read(object, 0, table) || reinterpret_cast<quintptr>(table) != dataModule.begin + 0x13DBD0 ||
            !read(table, 0x10, getter) || reinterpret_cast<quintptr>(getter) != dataModule.begin + 0x5A7F0) return false;
        using CodeGetter = int (__fastcall*)(const void*);
        code = reinterpret_cast<CodeGetter>(getter)(object);
        return true;
    }

    void completeStep(qlonglong actionId, const QSharedPointer<const IResponseInfo>& response, bool isTag) {
        if (requestId.isEmpty() || activeActionId != actionId || stepIndex >= steps.size() ||
            (steps.at(stepIndex).field == QStringLiteral("tags") || steps.at(stepIndex).field == QStringLiteral("tag.create")) != isTag) return;
        if (!stillOwner()) { fail(QStringLiteral("library_changed"), QStringLiteral("The active library changed before the native action completed. No remaining actions were submitted.")); return; }
        int nativeCode = 0;
        if (!responseCode(response, nativeCode)) {
            fail(QStringLiteral("native_result_unavailable"), QStringLiteral("The native action result identity could not be verified."));
            return;
        }
        const Step& step = steps.at(stepIndex);
        const int resultIndex = resultIndexes.value(step.itemId);
        QJsonObject item = results.at(resultIndex).toObject();
        QJsonObject fields = item.value(QStringLiteral("fields")).toObject();
        const bool success = nativeCode == 0;
        fields.insert(step.field, QJsonObject{
            {QStringLiteral("ok"), success}, {QStringLiteral("native_code"), nativeCode},
            {QStringLiteral("action_id"), QString::number(actionId)}});
        item.insert(QStringLiteral("fields"), fields);
        item.insert(QStringLiteral("ok"), item.value(QStringLiteral("ok")).toBool(true) && success);
        results.replace(resultIndex, item);
        if (!success) ++failedSteps;
        ++stepIndex;
        activeActionId = 0;
        emit q->progress(requestId, stepIndex, steps.size(), success ? QStringLiteral("原生整理任务已完成") : QStringLiteral("原生整理任务返回失败"));
        QTimer::singleShot(0, q, &NativeAdapter::continueRequest);
    }
};

NativeAdapter::NativeAdapter(QObject* parent) : QObject(parent), d_(std::make_unique<NativeAdapterPrivate>(this)) {
    setObjectName(QStringLiteral("BillfishAutomationNativeAdapter"));
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &NativeAdapter::libraryClosed);
    QTimer::singleShot(0, this, &NativeAdapter::observeLibrary);
    QCoreApplication::instance()->installEventFilter(this);
}

NativeAdapter::~NativeAdapter() = default;

QJsonObject NativeAdapter::info() {
    observeLibrary();
    const auto owner = d_->owner();
    return QJsonObject{
        {QStringLiteral("ready"), owner.ready}, {QStringLiteral("library_root"), owner.root},
        {QStringLiteral("manager_state"), owner.state}, {QStringLiteral("reason"), owner.reason},
        {QStringLiteral("manager_class"), owner.manager ? QString::fromLatin1(owner.manager->metaObject()->className()) : QString()},
        {QStringLiteral("gui_thread"), QThread::currentThread() == QCoreApplication::instance()->thread()},
        {QStringLiteral("busy"), d_->busy()},
        {QStringLiteral("operations"), QJsonArray{QStringLiteral("items.update"), QStringLiteral("tags.assign"), QStringLiteral("tags.create"), QStringLiteral("items.import")}},
        {QStringLiteral("completion"), QStringLiteral("native_action_result")}
    };
}

QByteArray NativeAdapter::libraryIdentity() {
    // Predicate callers need an ownership check without observeLibrary() and
    // its lifecycle signals reentering an in-progress native model refresh.
    const auto owner = d_->owner();
    if (!owner.ready) return {};
    return normalizedPath(owner.root).toUtf8() + '\0' + QByteArray::number(reinterpret_cast<quintptr>(owner.manager), 16) + ':' +
        QByteArray::number(reinterpret_cast<quintptr>(owner.library), 16) + ':' + QByteArray::number(reinterpret_cast<quintptr>(owner.fileManager), 16);
}

QJsonObject NativeAdapter::prepareBrowserCaptureFolder(const QString& libraryRoot, const QJsonObject& preflight) {
    bool created = false;
    bool saveAttempted = false;
    qint64 targetId = 0;
    const auto reject = [&libraryRoot, &created, &targetId, &saveAttempted](const QString& code, const QString& message) {
        QJsonObject details{{QStringLiteral("library_root"), libraryRoot}, {QStringLiteral("created"), created},
            {QStringLiteral("save_attempted"), saveAttempted},
            {QStringLiteral("config_saved"), saveAttempted ? QJsonValue(QJsonValue::Null) : QJsonValue(false)},
            {QStringLiteral("rollback"), false}};
        if (targetId > 0) details.insert(QStringLiteral("target_folder_id"), QString::number(targetId));
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("code"), code},
                           {QStringLiteral("message"), message}, {QStringLiteral("details"), details}};
    };
    if (QThread::currentThread() != thread() || thread() != QCoreApplication::instance()->thread())
        return reject(QStringLiteral("gui_thread_required"), QStringLiteral("Browser destination preparation requires the GUI thread."));
    if (d_->busy() || d_->shutdownPending)
        return reject(QStringLiteral("native_busy"), QStringLiteral("A native request or shutdown is already in progress."));
    observeLibrary();
    const auto owner = d_->owner();
    if (!owner.ready) return reject(QStringLiteral("native_not_ready"), owner.reason);
    if (!QFileInfo(libraryRoot).isAbsolute() || !samePath(libraryRoot, owner.root) ||
        !preflight.value(QStringLiteral("library_root")).isString() ||
        !samePath(preflight.value(QStringLiteral("library_root")).toString(), owner.root))
        return reject(QStringLiteral("library_mismatch"), QStringLiteral("The browser destination preflight is not bound to the current native library."));
    const QString status = preflight.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("ambiguous"))
        return reject(QStringLiteral("browser_destination_ambiguous"), QStringLiteral("Multiple root browser-capture folders exist; a unique destination is required."));
    if (status != QStringLiteral("configured") && status != QStringLiteral("candidate") && status != QStringLiteral("needs_create"))
        return reject(QStringLiteral("invalid_preflight"), QStringLiteral("The browser destination preflight status is unsupported."));
    const QString defaultName = QStringLiteral("浏览器采集");
    const QString name = preflight.value(QStringLiteral("name")).toString();
    const QString expectedPath = normalizedPath(QDir(owner.root).filePath(QStringLiteral(".bf/.ui_config/lib_info.json")));
    if (!preflight.value(QStringLiteral("config_path")).isString() ||
        !samePath(preflight.value(QStringLiteral("config_path")).toString(), expectedPath) ||
        !preflight.value(QStringLiteral("needs_binding")).isBool() || !preflight.value(QStringLiteral("candidate_ids")).isArray() ||
        name.isEmpty() || name.contains(QChar(0)) || (status != QStringLiteral("configured") && name != defaultName))
        return reject(QStringLiteral("invalid_preflight"), QStringLiteral("The browser destination preflight fields are invalid."));
    const QJsonArray candidates = preflight.value(QStringLiteral("candidate_ids")).toArray();
    if (status == QStringLiteral("needs_create")) {
        if (!preflight.value(QStringLiteral("target_folder_id")).isNull() || !candidates.isEmpty())
            return reject(QStringLiteral("invalid_preflight"), QStringLiteral("Folder creation requires a preflight with no active candidate."));
    } else {
        if (!positiveId(preflight.value(QStringLiteral("target_folder_id")), targetId) || targetId > std::numeric_limits<int>::max() ||
            (status == QStringLiteral("candidate") && (candidates.size() != 1 || candidates.first() != preflight.value(QStringLiteral("target_folder_id")))))
            return reject(QStringLiteral("invalid_preflight"), QStringLiteral("The browser destination must identify one real native folder."));
    }
    QString reason;
    if (!d_->configModule.handle && !moduleIdentity(L"bfconfproxy.dll",
        QByteArrayLiteral("d5dc736f215538b2096d06626859b665e826b6687c12f6378bfb289848e2d713"), d_->configModule, reason)) {
        d_->configModule = Module();
        return reject(QStringLiteral("unsupported_capture_runtime"), reason);
    }
    if (GetModuleHandleW(L"bfconfproxy.dll") != d_->configModule.handle)
        return reject(QStringLiteral("capture_runtime_changed"), QStringLiteral("The native browser configuration module binding changed."));
    using GetConfig = void* (*)();
    const auto getter = reinterpret_cast<GetConfig>(GetProcAddress(d_->configModule.handle, "getConfProxyLibrary"));
    if (reinterpret_cast<quintptr>(getter) != d_->configModule.begin + 0x9220)
        return reject(QStringLiteral("unsupported_capture_runtime"), QStringLiteral("The native browser configuration accessor is unsupported."));
    void* config = getter();
    void* table = nullptr;
    void* setter = nullptr;
    void* saver = nullptr;
    qint64 configuredId = 0;
    QString configuredName, configPath;
    if (reinterpret_cast<quintptr>(config) != d_->configModule.begin + 0x2B040 || !read(config, 0, table) ||
        reinterpret_cast<quintptr>(table) != d_->configModule.begin + 0x1F260 ||
        !read(table, 0xA0, setter) || reinterpret_cast<quintptr>(setter) != d_->configModule.begin + 0x5580 ||
        !read(table, 0x168, saver) || reinterpret_cast<quintptr>(saver) != d_->configModule.begin + 0x5560 ||
        !read(config, 8, configuredId) || !nativeString(config, 0x10, configuredName) ||
        !nativeString(config, 0x28, configPath) || !samePath(configPath, expectedPath))
        return reject(QStringLiteral("capture_configuration_unavailable"), QStringLiteral("The native browser configuration is not bound to the current library."));
    QJsonObject disk;
    const bool diskValid = readConfigObject(expectedPath, disk);
    bool diskIdValid = false;
    const qint64 diskId = disk.value(QStringLiteral("downloadFolderId")).toString().toLongLong(&diskIdValid, 10);
    const QString diskName = disk.value(QStringLiteral("downloadFolderName")).toString();
    const bool alreadyDesired = targetId > 0 && configuredId == targetId && configuredName == name;
    const bool currentMatchesDisk = diskValid && diskIdValid && configuredId == diskId && configuredName == diskName;
    if (status == QStringLiteral("configured") && configuredId != targetId)
        return reject(QStringLiteral("capture_configuration_changed"), QStringLiteral("The configured native browser folder differs from the validated destination."));
    if (!alreadyDesired && !currentMatchesDisk && !(configuredId <= 0 && (!diskValid || !diskIdValid)))
        return reject(QStringLiteral("capture_configuration_changed"), QStringLiteral("The native browser destination changed after the read-only preflight."));
    const auto sameOwner = [this, &owner] {
        const auto current = d_->owner();
        return current.ready && current.manager == owner.manager && current.library == owner.library && samePath(current.root, owner.root);
    };
    if (status == QStringLiteral("configured") && alreadyDesired && diskValid && diskIdValid && diskId == targetId && diskName == name) {
        if (!sameOwner()) return reject(QStringLiteral("library_changed"), QStringLiteral("The library changed while browser destination was checked."));
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("library_root"), owner.root},
            {QStringLiteral("target_folder_id"), QString::number(targetId)}, {QStringLiteral("name"), name},
            {QStringLiteral("created"), false}, {QStringLiteral("bound"), false}, {QStringLiteral("config_verified"), true}};
    }
    if (status == QStringLiteral("needs_create")) {
        auto folderAction = d_->makeAction(owner, 0x88, 0x1D010, "IFolderAction");
        void* actionTable = nullptr;
        void* create = nullptr;
        if (!folderAction || !read(folderAction.data(), 0, actionTable) ||
            reinterpret_cast<quintptr>(actionTable) != d_->dataModule.begin + 0x13BF18 ||
            !read(actionTable, 0xA0, create) || reinterpret_cast<quintptr>(create) != d_->dataModule.begin + 0x3AA10)
            return reject(QStringLiteral("native_folder_unavailable"), QStringLiteral("The native browser folder creation entry is unsupported."));
        if (!sameOwner()) return reject(QStringLiteral("library_changed"), QStringLiteral("The library changed before browser folder creation."));
        // The GUI-native call returns response IDs[0], not an action token.
        // It does not establish when a separate database reader sees the row.
        using CreateFolder = qint64 (__fastcall*)(void*, qint64, const QString*, double);
        targetId = reinterpret_cast<CreateFolder>(create)(folderAction.data(), 0, &defaultName, 0.0);
        created = targetId > 0;
        if (!created) return reject(QStringLiteral("native_folder_failed"), QStringLiteral("The native browser folder creation did not return a real folder ID."));
        if (targetId > std::numeric_limits<int>::max())
            return reject(QStringLiteral("native_folder_id_unsupported"), QStringLiteral("The created native folder ID exceeds the importer's 32-bit range."));
    }
    if (!sameOwner() || !nativeString(config, 0x28, configPath) || !samePath(configPath, expectedPath))
        return reject(QStringLiteral("library_changed"), QStringLiteral("The library changed before browser destination binding. A created folder is not rolled back."));
    using SetDestination = bool (__fastcall*)(void*, qint64, const QString*);
    using SaveDestination = void (__fastcall*)(void*);
    reinterpret_cast<SetDestination>(setter)(config, targetId, &name);
    saveAttempted = true;
    reinterpret_cast<SaveDestination>(saver)(config);
    if (!sameOwner()) return reject(QStringLiteral("library_changed"), QStringLiteral("The library changed while browser destination was saved."));
    QJsonObject saved;
    if (!readConfigObject(expectedPath, saved) || saved.value(QStringLiteral("downloadFolderId")).toString() != QString::number(targetId) ||
        saved.value(QStringLiteral("downloadFolderName")).toString() != name ||
        !saved.value(QStringLiteral("createdDefaultDownloadFolder")).toBool())
        return reject(QStringLiteral("capture_configuration_not_saved"), QStringLiteral("The native browser destination could not be verified in lib_info.json."));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("library_root"), owner.root},
        {QStringLiteral("target_folder_id"), QString::number(targetId)}, {QStringLiteral("name"), name},
        {QStringLiteral("created"), created}, {QStringLiteral("bound"), true}, {QStringLiteral("config_verified"), true}};
}

void NativeAdapter::importFile(const QString& id, const QJsonObject& request) {
    const auto reject = [this, &id](const QString& code, const QString& message, bool sourceReleased = true) {
        emit failed(id, code, message, QJsonObject{{QStringLiteral("operation"), QStringLiteral("items.import")},
            {QStringLiteral("submitted"), false}, {QStringLiteral("source_safe_to_delete"), sourceReleased}});
    };
    const bool unusedSource = !d_->importing || !samePath(d_->importing->path, request.value(QStringLiteral("path")).toString());
    if (d_->shutdownPending) {
        reject(QStringLiteral("application_quitting"), QStringLiteral("Billfish is finishing shutdown."), unusedSource); return;
    }
    if (d_->busy()) { reject(QStringLiteral("native_busy"), QStringLiteral("A native automation request is already in progress."), unusedSource); return; }
    if (QThread::currentThread() != thread() || thread() != QCoreApplication::instance()->thread()) {
        reject(QStringLiteral("gui_thread_required"), QStringLiteral("Native import submission requires the GUI thread.")); return;
    }
    const QStringList keys{QStringLiteral("library_root"), QStringLiteral("path"), QStringLiteral("target_folder_id"),
                           QStringLiteral("name"), QStringLiteral("origin")};
    if (id.isEmpty() || !allowedKeys(request, keys)) {
        reject(QStringLiteral("invalid_arguments"), QStringLiteral("Invalid native import envelope.")); return;
    }
    for (const QString& key : keys) {
        if (!request.value(key).isString() || request.value(key).toString().contains(QChar(0))) {
            reject(QStringLiteral("invalid_arguments"), QStringLiteral("Every native import field is a required string without NUL characters.")); return;
        }
    }
    const QString root = request.value(QStringLiteral("library_root")).toString();
    const QString path = request.value(QStringLiteral("path")).toString();
    const QString name = request.value(QStringLiteral("name")).toString();
    const QString origin = request.value(QStringLiteral("origin")).toString();
    qint64 folderId = 0;
    const QFileInfo source(path);
    if (!QFileInfo(root).isAbsolute() || !source.isAbsolute() || !source.isFile() || !source.isReadable() || source.suffix().isEmpty()) {
        reject(QStringLiteral("invalid_arguments"), QStringLiteral("Import requires an absolute library root and a readable source file with an extension.")); return;
    }
    if (!positiveId(request.value(QStringLiteral("target_folder_id")), folderId, true) || folderId > std::numeric_limits<int>::max()) {
        reject(QStringLiteral("invalid_arguments"), QStringLiteral("target_folder_id must be a decimal string in the native 32-bit nonnegative range.")); return;
    }
    const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    bool validName = !name.trimmed().isEmpty() && name.size() + source.suffix().size() + 1 <= 255 &&
        !name.endsWith(QLatin1Char('.')) && !name.endsWith(QLatin1Char(' '));
    for (QChar character : name) if (character.unicode() < 32 || forbidden.contains(character)) validName = false;
    if (!validName || origin.toUcs4().size() > 8192) {
        reject(QStringLiteral("invalid_arguments"), QStringLiteral("name must be a valid base filename, and origin must not exceed 8192 characters.")); return;
    }
    observeLibrary();
    const auto owner = d_->owner();
    if (!owner.ready) { reject(QStringLiteral("native_not_ready"), owner.reason); return; }
    if (!samePath(root, owner.root)) {
        reject(QStringLiteral("library_mismatch"), QStringLiteral("The requested import library is not the current native owner.")); return;
    }
    QString reason;
    if (!d_->ensureNativeShutdownGuard(reason)) {
        reject(QStringLiteral("native_import_unavailable"), reason); return;
    }
    d_->importing = std::make_unique<NativeAdapterPrivate::Import>();
    auto& import = *d_->importing;
    import.id = id; import.owner = owner; import.path = normalizedPath(path);
    import.folderId = QString::number(folderId); import.name = name; import.origin = origin;
    d_->ownerTimer->start();
    if (name.size() > 32) {
        // The deep import explicit-name branch truncates stems to 32 UTF-16
        // units. Native rename preserves the existing ID and the full stem.
        import.renameAction = d_->makeAction(owner, 0x80, 0x1CF50, "IFileAction");
        void* renameTable = nullptr;
        void* renameSlot = nullptr;
        if (!import.renameAction || !read(import.renameAction.data(), 0, renameTable) ||
            reinterpret_cast<quintptr>(renameTable) != d_->dataModule.begin + 0x13B670 ||
            !read(renameTable, 0x58, renameSlot) || reinterpret_cast<quintptr>(renameSlot) != d_->dataModule.begin + 0x2C300 ||
            import.renameAction->metaObject()->indexOfSlot("slotRenameFile(qlonglong,QString)") < 0 ||
            !connect(import.renameAction.data(), SIGNAL(signalFileResult(qlonglong,QSharedPointer<const IResponseInfo>)),
                this, SLOT(importRenameResult(qlonglong,QSharedPointer<const IResponseInfo>)), Qt::QueuedConnection)) {
            d_->abortImport(QStringLiteral("native_rename_unavailable"), QStringLiteral("The native full-name import action is unavailable.")); return;
        }
    }
    if (!d_->createImportManager(reason)) {
        d_->abortImport(QStringLiteral("native_import_unavailable"), reason); return;
    }
    // AutoConnection uses the vendor's existing registered BFTaskResultInfo;
    // the receiver takes a const reference and copies only evidenced IDs.
    const bool connected = connect(import.manager.data(), SIGNAL(signalTaskFinished(BFTaskResultInfo)),
        this, SLOT(importTaskResult(BFTaskResultInfo))) &&
        connect(import.manager.data(), SIGNAL(signalFinished(int,int,int)), this, SLOT(importQueueFinished(int,int,int))) &&
        connect(import.manager.data(), SIGNAL(signalProgress(int,int)), this, SLOT(importProgress(int,int)));
    if (!connected) {
        d_->abortImport(QStringLiteral("native_import_unavailable"), QStringLiteral("The native import completion signals could not be bound.")); return;
    }
    if (!d_->stillImportOwner()) {
        d_->abortImport(QStringLiteral("library_changed"), QStringLiteral("The active library changed before native import submission.")); return;
    }
    void* table = nullptr;
    void* submit = nullptr;
    if (!read(import.manager.data(), 0, table) || !read(table, 0x68, submit) ||
        reinterpret_cast<quintptr>(submit) != d_->importModule.begin + 0x9D20 || !d_->setImportState(0)) {
        d_->abortImport(QStringLiteral("native_import_unavailable"), QStringLiteral("The native common-task submission entry is unsupported.")); return;
    }
    CommonImportTask task;
    task.path = QDir::toNativeSeparators(import.path);
    task.folderId = static_cast<int>(folderId);
    task.metadata = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("origin"), origin}}).toJson(QJsonDocument::Compact));
    using Submit = void (__fastcall*)(void*, const CommonImportTask*);
    // This only queues the native task. The native manager's own QThread calls
    // blocking IImportAction::importTask; no GUI-thread semaphore is used here.
    reinterpret_cast<Submit>(submit)(import.manager.data(), &task);
    void* queue = nullptr;
    int begin = 0, end = 0;
    if (!nativeListBounds(import.manager.data(), 0x20, queue, begin, end) || end - begin != 1) {
        d_->abortImport(QStringLiteral("native_submission_failed"), QStringLiteral("The native manager did not queue the single requested file.")); return;
    }
    import.submitted = true;
    emit progress(id, 0, 1, QStringLiteral("正在导入素材到资料库"));
}

void NativeAdapter::cancelImport(const QString& id) {
    if (d_->importing && d_->importing->id == id)
        d_->abortImport(QStringLiteral("cancelled"), QStringLiteral("Native import cancellation was requested; committed data is not rolled back."));
}

void NativeAdapter::importTaskResult(const BFTaskResultInfo& result) {
    if (!d_->importing || sender() != d_->importing->manager.data()) return;
    auto& import = *d_->importing;
    ++import.taskResults;
    QList<qint64> ids;
    const bool valid = importResultIds(result, ids);
    for (qint64 id : ids) if (!import.ids.contains(id)) import.ids.append(id);
    if (!valid) {
        d_->abortImport(QStringLiteral("native_result_unavailable"), QStringLiteral("The native import result ID list could not be verified.")); return;
    }
    if (!d_->stillImportOwner()) d_->abortImport(QStringLiteral("library_changed"),
        QStringLiteral("The active library changed while the native import was in progress. Its outcome is not rolled back."));
}

void NativeAdapter::importQueueFinished(int announcedTotal, int success, int failedCount) {
    if (!d_->importing || sender() != d_->importing->manager.data() || d_->importing->queueFinished) return;
    auto& import = *d_->importing;
    import.queueFinished = true; import.announcedTotal = announcedTotal; import.success = success; import.failed = failedCount;
    // AD50 still writes manager fields after emitting signalFinished.
    QTimer::singleShot(0, this, &NativeAdapter::drainImport);
}

void NativeAdapter::importProgress(int completed, int total) {
    if (!d_->importing || sender() != d_->importing->manager.data()) return;
    emit progress(d_->importing->id, completed, total, d_->shutdownPending ?
        QStringLiteral("正在结束导入后退出") : QStringLiteral("正在导入素材到资料库"));
}

void NativeAdapter::drainImport() {
    if (!d_->importing || d_->importing->draining || (d_->importing->submitted && !d_->importing->queueFinished)) return;
    auto& import = *d_->importing;
    if (import.manager) {
        void* current = nullptr;
        void* queue = nullptr;
        int begin = 0, end = 0;
        if (!read(import.manager.data(), 0x28, current) || current ||
            !nativeListBounds(import.manager.data(), 0x20, queue, begin, end) || begin != end) {
            d_->abortImport(QStringLiteral("native_import_lifecycle_unavailable"),
                QStringLiteral("The native import manager has not finished draining its tasks."));
            return;
        }
    }
    import.draining = true;
    if (!import.worker) {
        import.workerFinished = true;
        QTimer::singleShot(0, this, &NativeAdapter::finishImport);
    } else if (import.worker->isFinished()) {
        import.workerFinished = true;
        QTimer::singleShot(0, this, &NativeAdapter::finishImport);
    } else {
        // Only quit the default native event loop after the import has really
        // finished. quit() cannot be used as an import interruption mechanism.
        import.worker->quit();
    }
}

void NativeAdapter::importWorkerFinished() {
    if (!d_->importing || sender() != d_->importing->worker.data()) return;
    d_->importing->workerFinished = true;
    if (d_->importing->draining) QTimer::singleShot(0, this, &NativeAdapter::finishImport);
}

void NativeAdapter::finishImport() {
    if (!d_->importing || !d_->importing->draining || !d_->importing->workerFinished) return;
    auto& import = *d_->importing;
    // A submitted rename uses the data-center queue independently of the
    // drained import worker. Keep its wrapper, busy state and shutdown hold
    // until the matching native result, including after cancellation.
    if (import.renameActionId > 0 && !import.renameFinished) return;
    if (import.errorCode.isEmpty() && !d_->stillImportOwner()) {
        import.errorCode = QStringLiteral("library_changed");
        import.errorMessage = QStringLiteral("The active library changed before import completion could be verified.");
    }
    // The first native int is the progress-announced subtask count (+40),
    // populated by slotSubTaskNums at B280. A single-file task can leave it
    // zero when callback 8D40 never announces a positive total. Successful and
    // failed file counts (+48/+44) come independently from real task results.
    if (import.errorCode.isEmpty() && (!import.queueFinished || import.success != 1 || import.failed != 0)) {
        import.errorCode = QStringLiteral("native_import_failed");
        import.errorMessage = QStringLiteral("The native import did not report one successful file.");
    }
    if (import.errorCode.isEmpty() && (import.taskResults != 1 || import.ids.size() != 1)) {
        import.errorCode = QStringLiteral("native_result_unavailable");
        import.errorMessage = QStringLiteral("The native import did not return exactly one verified material ID.");
    }
    if (import.errorCode.isEmpty() && import.renameAction && !import.renameFinished) {
        qlonglong token = 0;
        const bool invoked = QMetaObject::invokeMethod(import.renameAction.data(), "slotRenameFile", Qt::DirectConnection,
            Q_RETURN_ARG(qlonglong, token), Q_ARG(qlonglong, import.ids.first()), Q_ARG(QString, import.name));
        if (invoked && token > 0) {
            import.renameActionId = token;
            emit progress(import.id, 1, 1, QStringLiteral("正在保存完整素材名称"));
            return;
        }
        import.errorCode = QStringLiteral("native_rename_failed");
        import.errorMessage = QStringLiteral("The imported material could not be submitted for its full native filename. The imported ID is not rolled back.");
    }
    QJsonArray ids;
    for (qint64 id : import.ids) ids.append(QString::number(id));
    const QString id = import.id, code = import.errorCode, message = import.errorMessage;
    const bool submitted = import.submitted;
    QJsonObject result{{QStringLiteral("operation"), QStringLiteral("items.import")},
        {QStringLiteral("library_root"), import.owner.root}, {QStringLiteral("path"), import.path},
        {QStringLiteral("target_folder_id"), import.folderId}, {QStringLiteral("name"), import.name},
        {QStringLiteral("origin"), import.origin}, {QStringLiteral("item_ids"), ids},
        {QStringLiteral("mode"), QStringLiteral("copy")}, {QStringLiteral("submitted"), import.submitted},
        {QStringLiteral("source_safe_to_delete"), true}, {QStringLiteral("analysis_ready"), false},
        {QStringLiteral("native_counts"), QJsonObject{{QStringLiteral("total"), import.announcedTotal},
            {QStringLiteral("success"), import.success}, {QStringLiteral("failed"), import.failed}}},
        {QStringLiteral("rollback"), false}, {QStringLiteral("completion"), QStringLiteral("native_import_task_finished")}};
    if (import.renameAction) result.insert(QStringLiteral("name_action"), QJsonObject{
        {QStringLiteral("action_id"), import.renameActionId > 0 ? QJsonValue(QString::number(import.renameActionId)) : QJsonValue()},
        {QStringLiteral("submitted"), import.renameActionId > 0}, {QStringLiteral("completed"), import.renameFinished}});
    if (import.manager) QObject::disconnect(import.manager.data(), nullptr, this, nullptr);
    if (import.renameAction) QObject::disconnect(import.renameAction.data(), nullptr, this, nullptr);
    const QPointer<QThread> worker = import.worker;
    // Native manager's deleter does not quit/wait its worker. Both releases
    // occur only after finished and after returning to the GUI event loop.
    import.manager.clear();
    if (worker) worker->deleteLater();
    d_->importing.reset();
    if (d_->requestId.isEmpty()) d_->ownerTimer->stop();
    if (d_->nativeShutdownHeld) QTimer::singleShot(0, this, &NativeAdapter::continueNativeShutdown);
    else if (d_->queuedQuitPending) QTimer::singleShot(0, QCoreApplication::instance(), [] { QCoreApplication::quit(); });
    if (code.isEmpty()) {
        result.insert(QStringLiteral("status"), QStringLiteral("completed"));
        result.insert(QStringLiteral("dry_run"), false);
        emit finished(id, result);
    } else {
        result.insert(QStringLiteral("outcome"), ids.isEmpty() && submitted ? QStringLiteral("unknown") : QStringLiteral("not_rolled_back"));
        emit failed(id, code, message, result);
    }
}

void NativeAdapter::importRenameResult(qlonglong actionId, QSharedPointer<const IResponseInfo> response) {
    if (!d_->importing || sender() != d_->importing->renameAction.data() ||
        actionId != d_->importing->renameActionId || d_->importing->renameFinished) return;
    auto& import = *d_->importing;
    import.renameFinished = true;
    int code = -1;
    if (!d_->responseCode(response, code) || code != 0) {
        if (import.errorCode.isEmpty()) {
            import.errorCode = QStringLiteral("native_rename_failed");
            import.errorMessage = QStringLiteral("The native full-filename action failed. The imported material and its ID are not rolled back.");
        }
    }
    // Return past both the native signal and this queued receiver before
    // releasing the native wrapper or completing a pending shutdown.
    QTimer::singleShot(0, this, &NativeAdapter::finishImport);
}

void NativeAdapter::nativeShutdownStarted() {
    if (sender() != d_->shutdownManager.data()) return;
    d_->shutdownPending = true;
    if (!d_->importing) return;
    // Qt5 quit() calls exit() directly. Hold the native protocol before its
    // timer can unload importtask/datacenter, rather than waiting for Qt Quit.
    if (!d_->nativeShutdownHeld) {
        const bool held = QMetaObject::invokeMethod(d_->shutdownManager.data(), "delayShutdown", Qt::DirectConnection);
        if (!held) {
            d_->abortImport(QStringLiteral("native_shutdown_unavailable"), QStringLiteral("The native shutdown delay could not be acquired."));
            return;
        }
        d_->nativeShutdownHeld = true;
        d_->queuedQuitPending = false; // Resume the native protocol, not direct Qt exit.
        const int remaining = d_->shutdownTimer ? d_->shutdownTimer->remainingTime() : -1;
        d_->shutdownTimeoutMs = remaining >= 0 ? remaining : 5000;
    }
    // Native D8FA0's 5-second deadline ignores outstanding delay counts. Pause
    // that exact embedded timer while our evidenced worker still uses the DLLs.
    if (d_->shutdownTimer) d_->shutdownTimer->stop();
    d_->abortImport(QStringLiteral("application_quitting"), QStringLiteral("Billfish is cancelling and draining the active native import before shutdown."));
    emit progress(d_->importing->id, 0, 1, QStringLiteral("正在结束导入后退出"));
}

void NativeAdapter::continueNativeShutdown() {
    if (!d_->nativeShutdownHeld || d_->importing || !d_->shutdownManager || !d_->shutdownTimer) return;
    d_->nativeShutdownHeld = false;
    QMetaObject::invokeMethod(d_->shutdownManager.data(), "continueShutdown", Qt::DirectConnection);
    // continueShutdown starts timer(0) once every participant is done. If
    // another native participant is pending, restore its paused native timeout.
    int remainingParticipants = 0;
    if (!d_->shutdownTimer->isActive() && read(d_->shutdownManager.data(), 0x1C, remainingParticipants) && remainingParticipants > 0)
        d_->shutdownTimer->start(d_->shutdownTimeoutMs);
}

bool NativeAdapter::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::Quit && d_->importing) {
        if (!d_->shutdownPending) {
            d_->shutdownPending = true;
            d_->queuedQuitPending = true;
            d_->abortImport(QStringLiteral("application_quitting"), QStringLiteral("Billfish is cancelling and draining the active native import before shutdown."));
            emit progress(d_->importing->id, 0, 1, QStringLiteral("正在结束导入后退出"));
        }
        event->ignore();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

QAbstractItemView* NativeAdapter::currentMaterialView() {
    if (QThread::currentThread() != thread() || thread() != QCoreApplication::instance()->thread())
        return nullptr;
    observeLibrary();
    const auto owner = d_->owner();
    if (!owner.ready) return nullptr;
    QString reason;
    if ((!d_->coreModule.handle && !moduleIdentity(L"Billfish.core.exe", QByteArrayLiteral("47abf9bf0464d88c1ca259965bbdd00165317fbf1fea8308c339c548c405c24f"), d_->coreModule, reason)) ||
        (!d_->viewModule.handle && !moduleIdentity(L"viewmodule.dll", QByteArrayLiteral("ad1e8e407433f8028ad7418afa485ff3a80dfabded2e4f65b4aece283ced57db"), d_->viewModule, reason)) ||
        (!d_->interfaceModule.handle && !moduleIdentity(L"interfaces_plugin.dll", QByteArrayLiteral("be89bad69ce69eb955f35de1759e2e6c934b0f082d0fda4ebe01b0524b08b408"), d_->interfaceModule, reason)) ||
        (!d_->modelModule.handle && !moduleIdentity(L"datamodel.dll", QByteArrayLiteral("4d47008537fd72ccbe63f84c87b1b822f661e1f83186d56acbae37cf3af3d244"), d_->modelModule, reason))) {
        d_->coreModule = Module(); d_->viewModule = Module(); d_->interfaceModule = Module(); d_->modelModule = Module();
        return nullptr;
    }
    if (GetModuleHandleW(L"Billfish.core.exe") != d_->coreModule.handle ||
        GetModuleHandleW(L"viewmodule.dll") != d_->viewModule.handle ||
        GetModuleHandleW(L"interfaces_plugin.dll") != d_->interfaceModule.handle ||
        GetModuleHandleW(L"datamodel.dll") != d_->modelModule.handle)
        return nullptr;
    const auto widgets = QApplication::allWidgets();
    QList<QWidget*> containers;
    for (QWidget* widget : widgets) {
        if (widget->isVisible() && QByteArray(widget->metaObject()->className()) == QByteArrayLiteral("BFItemIconView"))
            containers.append(widget);
    }
    if (containers.size() != 1)
        return nullptr;
    QWidget* container = containers.first();
    void* context = nullptr;
    void* manager = nullptr;
    QWidget* stackedWidget = nullptr;
    // Frozen BFItemIconView uses +98 private context, whose +8 is the data-center manager.
    // Its +30 field is the BFStackedView holding the four material layouts.
    if (container->thread() != thread() || !read(container, 0x98, context) || !read(context, 8, manager) ||
        manager != owner.manager || !read(container, 0x30, stackedWidget) || !widgets.contains(stackedWidget) ||
        QByteArray(stackedWidget->metaObject()->className()) != QByteArrayLiteral("BFStackedView") ||
        !container->isAncestorOf(stackedWidget))
        return nullptr;
    auto* stacked = qobject_cast<QStackedWidget*>(stackedWidget);
    QWidget* current = stacked ? stacked->currentWidget() : nullptr;
    auto* view = current && current->inherits("BFBaseItemView") ? qobject_cast<QAbstractItemView*>(current) : nullptr;
    if (!view || !view->isVisible() || view->thread() != thread() || !view->model() || !view->selectionModel() ||
        view->selectionModel()->model() != view->model())
        return nullptr;
    return view;
}

QJsonObject NativeAdapter::itemForIndex(const QModelIndex& index) {
    auto* view = currentMaterialView();
    if (!view || !index.isValid() || index.model() != view->model()) return {};
    const int itemPointerType = QMetaType::type("IDataItem*");
    // Native slotSelectionChanged and its selected-item helper use role 0x164.
    const QVariant value = index.data(0x164);
    void* item = nullptr;
    void* table = nullptr;
    int type = 0;
    if (itemPointerType == QMetaType::UnknownType || value.userType() != itemPointerType)
        return {};
    if (!read(value.constData(), 0, item) || !read(item, 0, table) || !read(item, 8, type))
        return {};
    // The model returns its own FileItem/FolderItem subclasses. Their frozen
    // vtables live in datamodel.dll, not the interface or view module.
    const quintptr expectedTable = type == 2 ? d_->modelModule.begin + 0x15A80 :
                                   type == 1 ? d_->modelModule.begin + 0x15A70 : 0;
    if (!expectedTable || reinterpret_cast<quintptr>(table) != expectedTable)
        return {};
    if (type == 1) return {{QStringLiteral("available"), true}, {QStringLiteral("folder"), true}}; // Folder selection is not a selected material.
    void* fileInfo = nullptr;
    qint64 id = 0;
    // Exported IDataItem::getFileInfo returns the shared pointer at +20.
    // The native selection slot extracts IFileInfo's id at +8.
    if (!read(item, 0x20, fileInfo) || !read(fileInfo, 8, id) || id <= 0)
        return {};
    void* infoTable = nullptr;
    void* extensionFunction = nullptr;
    QString extension;
    const bool trustedFileInfo = read(fileInfo, 0, infoTable) && reinterpret_cast<quintptr>(infoTable) == d_->dataModule.begin + 0x13B7D0 &&
        read(infoTable, 0x38, extensionFunction) && reinterpret_cast<quintptr>(extensionFunction) == d_->dataModule.begin + 0x2D8C0;
    if (trustedFileInfo) {
        using ExtensionGetter = void* (__fastcall*)(const void*, QString*);
        reinterpret_cast<ExtensionGetter>(extensionFunction)(fileInfo, &extension);
    }
    unsigned char nativeAnalysis = 1, indexLost = 1;
    qint64 cover = 0;
    // Frozen image method +38100 uses these exact mode-2 conditions; the
    // exported IFileItem::getIndexLostFlag (+3130) reads item +30.
    const bool unsupportedPlaceholder = trustedFileInfo && read(fileInfo, 0x3E, nativeAnalysis) &&
        read(fileInfo, 0x53, cover) && read(item, 0x30, indexLost) &&
        nativeAnalysis == 0 && cover <= 0 && indexLost == 0;
    return {{QStringLiteral("available"), true}, {QStringLiteral("item_id"), QString::number(id)},
        {QStringLiteral("extension"), extension.toLower()},
        {QStringLiteral("native_unsupported_placeholder"), unsupportedPlaceholder}};
}

QJsonObject NativeAdapter::selectedItems() {
    auto* view = currentMaterialView();
    const auto owner = d_->owner();
    if (!view || !owner.ready) return {{QStringLiteral("available"), false},
        {QStringLiteral("code"), QStringLiteral("selection_context_unavailable")},
        {QStringLiteral("message"), QStringLiteral("The authenticated native material view is unavailable.")},
        {QStringLiteral("library_root"), owner.root}};
    QJsonArray ids;
    QSet<QString> seen;
    const auto indexes = view->selectionModel()->selectedIndexes();
    for (const auto& index : indexes) {
        const auto resolved = itemForIndex(index);
        if (!resolved.value(QStringLiteral("available")).toBool()) return {
            {QStringLiteral("available"), false}, {QStringLiteral("code"), QStringLiteral("selection_item_unavailable")},
            {QStringLiteral("message"), QStringLiteral("A selected native model item could not be authenticated.")},
            {QStringLiteral("library_root"), owner.root}};
        const auto id = resolved.value(QStringLiteral("item_id")).toString();
        if (!id.isEmpty() && !seen.contains(id)) { seen.insert(id); ids.append(id); }
    }
    const auto after = d_->owner();
    if (!after.ready || after.manager != owner.manager || after.library != owner.library || !samePath(after.root, owner.root))
        return {{QStringLiteral("available"), false}, {QStringLiteral("code"), QStringLiteral("library_changed")}};
    return {{QStringLiteral("available"), true}, {QStringLiteral("item_ids"), ids},
        {QStringLiteral("library_root"), owner.root}, {QStringLiteral("source"), QStringLiteral("native_current_view")}};
}

void NativeAdapter::observeLibrary() {
    if (QThread::currentThread() != thread() || thread() != QCoreApplication::instance()->thread()) return;
    const auto owner = d_->owner();
    if (!owner.manager) return;
    if (d_->lifecycleManager != owner.manager) {
        QObject::disconnect(d_->lifecycleClosed);
        QObject::disconnect(d_->lifecycleLoaded);
        QObject::disconnect(d_->lifecycleDestroyed);
        d_->lifecycleManager = owner.manager;
        d_->lifecycleClosed = connect(owner.manager, SIGNAL(signalLibraryClosed()), this, SLOT(libraryClosed()));
        d_->lifecycleLoaded = connect(owner.manager, SIGNAL(signalLoadFinished(int,QString)), this, SLOT(libraryLoaded(int,QString)));
        d_->lifecycleDestroyed = connect(owner.manager, &QObject::destroyed, this, &NativeAdapter::libraryClosed);
    }
    // Startup can observe the manager before it is ready, or after its load
    // signal. Reconcile every existing observation, including the first ready
    // one; publish after updating the cache so reentrant queries are idempotent.
    // An unready observation must not synthesize a library close.
    if (owner.ready && !owner.root.isEmpty() && owner.root != d_->activeRoot) {
        d_->activeRoot = owner.root;
        emit activeLibraryChanged(owner.root);
    }
}

void NativeAdapter::libraryLoaded(int code, const QString& message) {
    Q_UNUSED(code)
    Q_UNUSED(message)
    const auto owner = d_->owner();
    const QString root = owner.ready ? owner.root : QString();
    if (root != d_->activeRoot) {
        d_->activeRoot = root;
        emit activeLibraryChanged(root);
    }
}

QJsonObject NativeAdapter::fileTypeRegistrationReadiness(const QString& libraryRoot) {
    const auto owner = d_->owner();
    if (!owner.ready || !samePath(owner.root, libraryRoot))
        return {{QStringLiteral("ready"), false}, {QStringLiteral("code"), QStringLiteral("native_owner_unavailable")}};
    void* folderSync = nullptr; unsigned char synchronized = 0;
    // The frozen CLibrary sync-state getter (vtable +28) reads this exact success
    // flag. The service rejects AddTypes with 32105 until ScanSync sets it.
    if (!read(owner.library, 0x98, folderSync) || !read(folderSync, 0xC8, synchronized) || synchronized > 1)
        return {{QStringLiteral("ready"), false}, {QStringLiteral("code"), QStringLiteral("unsupported_library_sync_state")}};
    if (!synchronized)
        return {{QStringLiteral("ready"), false}, {QStringLiteral("code"), QStringLiteral("native_library_sync_required")},
            {QStringLiteral("message"), QStringLiteral("Billfish requires a successful native library synchronization before format registration. Synchronize this library in Billfish to continue.")}};
    return {{QStringLiteral("ready"), true}};
}

QJsonObject NativeAdapter::syncLibraryAfterTypeRegistration(const QString& libraryRoot) {
    const auto skipped = [](const QString& reason, bool retryable = false) {
        return QJsonObject{{QStringLiteral("requested"), false}, {QStringLiteral("reason"), reason},
            {QStringLiteral("retryable"), retryable}};
    };
    if (QThread::currentThread() != thread() || thread() != QCoreApplication::instance()->thread())
        return skipped(QStringLiteral("gui_thread_required"));
    if (d_->busy()) return skipped(QStringLiteral("native_request_in_progress"), true);
    auto* view = currentMaterialView();
    const auto owner = d_->owner();
    if (!owner.ready || normalizedPath(owner.root) != normalizedPath(libraryRoot))
        return skipped(QStringLiteral("native_owner_unavailable"), true);
    if (!view) return skipped(QStringLiteral("no_current_material_view"), true);
    QWidget* widget = view;
    while (widget && QByteArray(widget->metaObject()->className()) != QByteArrayLiteral("BfLocalResWidget"))
        widget = widget->parentWidget();
    if (!widget || !widget->isVisible() || widget->thread() != thread())
        return skipped(QStringLiteral("unsupported_library_widget"));
    void* priv = nullptr; void* resource = nullptr; void* sdk = nullptr; void* library = nullptr;
    void* table = nullptr; void* worker = nullptr; int state = -1;
    // Frozen local-resource widget -> resource manager -> SDK -> current CLibrary.
    if (!read(widget, 0x40, priv) || !read(priv, 0x38, resource) ||
        !read(resource, 0, table) || reinterpret_cast<quintptr>(table) != d_->dataModule.begin + 0x13D8E8 ||
        !read(resource, 0x10, sdk) || !read(sdk, 0, table) ||
        reinterpret_cast<quintptr>(table) != d_->dataModule.begin + 0x13DEB0 ||
        !read(sdk, 0x28, library) || library != owner.library ||
        !read(sdk, 0x10, state) || !read(sdk, 0x18, worker))
        return skipped(QStringLiteral("unsupported_library_sync_owner"));
    if (state != 2 || worker) return skipped(QStringLiteral("library_sync_in_progress"), true);
    if (widget->metaObject()->indexOfMethod("doSyncLibrary()") < 0)
        return skipped(QStringLiteral("unsupported_library_sync_slot"));
    // This slot starts native asynchronous sync; request ACK is not completion.
    if (!QMetaObject::invokeMethod(widget, "doSyncLibrary", Qt::DirectConnection))
        return skipped(QStringLiteral("library_sync_invoke_failed"));
    return {{QStringLiteral("requested"), true}, {QStringLiteral("completion"), QStringLiteral("native_library_sync_requested")}};
}

QJsonObject NativeAdapter::refreshCurrentSearch() {
    const auto skipped = [](const QString& reason) {
        return QJsonObject{{QStringLiteral("refreshed"), false}, {QStringLiteral("elapsed_ms"), 0},
                           {QStringLiteral("reason"), reason}};
    };
    if (QThread::currentThread() != thread() || thread() != QCoreApplication::instance()->thread())
        return skipped(QStringLiteral("gui_thread_required"));
    if (d_->busy()) return skipped(QStringLiteral("native_request_in_progress"));
    const auto owner = d_->owner();
    if (!owner.ready) return skipped(QStringLiteral("native_owner_unavailable"));
    if (!d_->toolModule.handle) {
        QString reason;
        if (!moduleIdentity(L"toolmodule.dll", QByteArrayLiteral("dd1018659189f191ea9203d3dca4e3b0ffde3064b291f4e78635f38fdb7d1923"),
                            d_->toolModule, reason)) {
            d_->toolModule = Module();
            return skipped(QStringLiteral("unsupported_search_runtime"));
        }
    }
    if (GetModuleHandleW(L"toolmodule.dll") != d_->toolModule.handle)
        return skipped(QStringLiteral("search_runtime_changed"));
    const auto widgets = QApplication::allWidgets();
    QList<QWidget*> navigationBars;
    for (QWidget* widget : widgets) {
        if (widget->isVisible() && QByteArray(widget->metaObject()->className()) == QByteArrayLiteral("ToolModule::BFNavigationBar"))
            navigationBars.append(widget);
    }
    if (navigationBars.isEmpty()) return skipped(QStringLiteral("no_search_context"));
    if (navigationBars.size() != 1) return skipped(QStringLiteral("ambiguous_search_context"));
    QWidget* navigation = navigationBars.first();
    QWidget* searchWidget = nullptr;
    QWidget* optionsWidget = nullptr;
    // These are the same two fields used by the frozen navigation slots B530/B510.
    if (!read(navigation, 0xB0, searchWidget) || !read(navigation, 0xB8, optionsWidget) ||
        !widgets.contains(searchWidget) || !widgets.contains(optionsWidget) ||
        QByteArray(searchWidget->metaObject()->className()) != QByteArrayLiteral("ToolModule::NavSearchWid") ||
        QByteArray(optionsWidget->metaObject()->className()) != QByteArrayLiteral("ToolModule::BFSearchOptionMenu") ||
        searchWidget->thread() != thread() || optionsWidget->thread() != thread())
        return skipped(QStringLiteral("unsupported_search_context"));
    void* searchUi = nullptr;
    QWidget* inputWidget = nullptr;
    // Native NavSearchWid::text (355F0) reads ui+18 and calls QLineEdit::text.
    if (!read(searchWidget, 0x30, searchUi) || !read(searchUi, 0x18, inputWidget) ||
        !widgets.contains(inputWidget) || !searchWidget->isAncestorOf(inputWidget))
        return skipped(QStringLiteral("unsupported_search_input"));
    auto* input = qobject_cast<QLineEdit*>(inputWidget);
    auto* options = qobject_cast<QListWidget*>(optionsWidget);
    if (!input || !options) return skipped(QStringLiteral("unsupported_search_widgets"));
    const QString text = input->text();
    if (text.isEmpty()) return skipped(QStringLiteral("no_active_query"));
    QAbstractItemModel* model = options->model();
    if (!model || model->rowCount() < 10) return skipped(QStringLiteral("unsupported_search_options"));
    int32_t mask = 0;
    // Exact native BFSearchOptionDelegate mask algorithm at toolmodule RVA14400.
    // Row zero is the menu heading. Rows1..9 store bools at UserRole+4.
    for (int row = 1; row <= 9; ++row) {
        const QVariant selected = model->index(row, 0).data(Qt::UserRole + 4);
        if (!selected.isValid() || selected.type() != QVariant::Bool)
            return skipped(QStringLiteral("unsupported_search_option_values"));
        if (selected.toBool()) mask |= int32_t(1 << (row - 1));
    }
    const QPointer<QWidget> focus(QApplication::focusWidget());
    const int cursor = input->cursorPosition();
    const int selectionStart = input->selectionStart();
    const int selectionLength = input->selectedText().size();
    QElapsedTimer elapsed;
    elapsed.start();
    const bool invoked = QMetaObject::invokeMethod(navigation, "SlotSummaryFlagChange", Qt::DirectConnection,
                                                   QGenericArgument("int32_t", &mask));
    const qint64 milliseconds = elapsed.elapsed();
    const bool queryUnchanged = input->text() == text;
    if (queryUnchanged) {
        if (selectionStart >= 0) input->setSelection(selectionStart, selectionLength);
        else input->setCursorPosition(cursor);
    }
    if (focus && QApplication::focusWidget() != focus.data() && focus->isVisible() && focus->isEnabled())
        focus->setFocus(Qt::OtherFocusReason);
    QJsonObject result{{QStringLiteral("refreshed"), invoked},
                       {QStringLiteral("elapsed_ms"), milliseconds}, {QStringLiteral("mask"), mask},
                       {QStringLiteral("query_unchanged"), queryUnchanged},
                       {QStringLiteral("focus_preserved"), !focus || QApplication::focusWidget() == focus.data()}};
    if (!invoked) result.insert(QStringLiteral("reason"), QStringLiteral("native_search_refresh_not_available"));
    else if (!queryUnchanged) result.insert(QStringLiteral("reason"), QStringLiteral("query_changed_during_refresh"));
    return result;
}

void NativeAdapter::setFileCover(const QString& id, const QString& root, const QString& itemId, const QString& path) {
    const QJsonObject intent{{QStringLiteral("item_id"), itemId}, {QStringLiteral("cover_path"), path}};
    auto args = intent; args.insert(QStringLiteral("dry_run"), false);
    execute(id, {{QStringLiteral("operation"), QStringLiteral("items.set_cover")},
        {QStringLiteral("library_root"), root}, {QStringLiteral("arguments"), args},
        {QStringLiteral("preflight"), QJsonObject{{QStringLiteral("operation"), QStringLiteral("items.set_cover")},
            {QStringLiteral("library_root"), root}, {QStringLiteral("arguments"), intent}}}});
}

void NativeAdapter::addFileTypes(const QString& id, const QString& root, const QStringList& extensions) {
    QJsonArray names;
    for (const auto& extension : extensions) names.append(extension);
    const QJsonObject intent{{QStringLiteral("extensions"), names}};
    auto args = intent; args.insert(QStringLiteral("dry_run"), false);
    execute(id, {{QStringLiteral("operation"), QStringLiteral("types.add")},
        {QStringLiteral("library_root"), root}, {QStringLiteral("arguments"), args},
        {QStringLiteral("preflight"), QJsonObject{{QStringLiteral("operation"), QStringLiteral("types.add")},
            {QStringLiteral("library_root"), root}, {QStringLiteral("arguments"), intent}}}});
}

void NativeAdapter::execute(const QString& id, const QJsonObject& request) {
    const auto reject = [this, &id](const QString& code, const QString& message) { emit failed(id, code, message, QJsonObject()); };
    if (d_->busy() || d_->shutdownPending) { reject(QStringLiteral("native_busy"), QStringLiteral("A native automation request or shutdown is already in progress.")); return; }
    if (id.isEmpty() || !allowedKeys(request, {QStringLiteral("operation"), QStringLiteral("arguments"), QStringLiteral("library_root"), QStringLiteral("preflight")})) {
        reject(QStringLiteral("invalid_arguments"), QStringLiteral("Invalid native request envelope.")); return;
    }
    const auto owner = d_->owner();
    if (!owner.ready) { reject(QStringLiteral("native_not_ready"), owner.reason); return; }
    const auto requestedRoot = request.value(QStringLiteral("library_root"));
    if (!requestedRoot.isString() || !QFileInfo(requestedRoot.toString()).isAbsolute() || !samePath(requestedRoot.toString(), owner.root)) {
        reject(QStringLiteral("library_mismatch"), QStringLiteral("The requested library is not the current native library owner.")); return;
    }
    const QString operation = request.value(QStringLiteral("operation")).toString();
    if (!request.value(QStringLiteral("arguments")).isObject()) {
        reject(QStringLiteral("invalid_arguments"), QStringLiteral("arguments must be an object.")); return;
    }
    const QJsonObject arguments = request.value(QStringLiteral("arguments")).toObject();
    if (arguments.contains(QStringLiteral("dry_run")) && !arguments.value(QStringLiteral("dry_run")).isBool()) {
        reject(QStringLiteral("invalid_arguments"), QStringLiteral("dry_run must be a boolean.")); return;
    }
    const bool dryRun = arguments.value(QStringLiteral("dry_run")).toBool(true);
    const QJsonObject preflight = request.value(QStringLiteral("preflight")).toObject();
    QJsonObject intent = arguments;
    intent.remove(QStringLiteral("dry_run"));
    if (!dryRun && (!request.value(QStringLiteral("preflight")).isObject() ||
        preflight.value(QStringLiteral("operation")).toString() != operation ||
        !samePath(preflight.value(QStringLiteral("library_root")).toString(), owner.root) ||
        !preflight.value(QStringLiteral("arguments")).isObject() ||
        preflight.value(QStringLiteral("arguments")).toObject() != intent)) {
        reject(QStringLiteral("preflight_required"), QStringLiteral("The native request requires a matching whole-request preflight.")); return;
    }
    QVector<NativeAdapterPrivate::Step> steps;
    QVector<qint64> tagIds;
    QVector<QString> typeNames;
    QJsonArray results;
    QHash<qint64, int> indexes;
    QString mode;
    ITagNodeInfo newTag;
    const auto addItem = [&results, &indexes](const qint64 item) {
        indexes.insert(item, results.size());
        results.append(QJsonObject{{QStringLiteral("item_id"), QString::number(item)},
                                   {QStringLiteral("ok"), true}, {QStringLiteral("fields"), QJsonObject()}});
    };
    if (operation == QStringLiteral("types.add")) {
        const auto extensions = arguments.value(QStringLiteral("extensions")).toArray();
        const QRegularExpression valid(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,31}$"));
        if (!allowedKeys(arguments, {QStringLiteral("extensions"), QStringLiteral("dry_run")}) ||
            !arguments.value(QStringLiteral("extensions")).isArray() || extensions.isEmpty() || extensions.size() > 128) {
            reject(QStringLiteral("invalid_arguments"), QStringLiteral("Native type registration requires 1 to 128 unique extension names.")); return;
        }
        for (const auto& value : extensions) {
            const auto name = value.toString();
            if (!value.isString() || !valid.match(name).hasMatch() || typeNames.contains(name)) {
                reject(QStringLiteral("invalid_arguments"), QStringLiteral("Native extension names must be unique lowercase safe suffixes without dots.")); return;
            }
            typeNames.append(name);
        }
        indexes.insert(0, 0);
        results.append(QJsonObject{{QStringLiteral("extensions"), extensions}, {QStringLiteral("ok"), true}, {QStringLiteral("fields"), QJsonObject()}});
        steps.append({0, QStringLiteral("types"), {}, {}});
    } else if (operation == QStringLiteral("items.set_cover")) {
        qint64 item = 0;
        const QString path = arguments.value(QStringLiteral("cover_path")).toString();
        if (!allowedKeys(arguments, {QStringLiteral("item_id"), QStringLiteral("cover_path"), QStringLiteral("dry_run")}) ||
            !positiveId(arguments.value(QStringLiteral("item_id")), item) || !QFileInfo(path).isAbsolute() ||
            !QFileInfo(path).isFile() || !path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
            reject(QStringLiteral("invalid_cover"), QStringLiteral("Cover publication requires one authenticated item and an existing absolute PNG.")); return;
        }
        addItem(item);
        steps.append({item, QStringLiteral("cover"), path, {}});
    } else if (operation == QStringLiteral("items.update")) {
        if (!allowedKeys(arguments, {QStringLiteral("updates"), QStringLiteral("dry_run")}) || !arguments.value(QStringLiteral("updates")).isArray()) {
            reject(QStringLiteral("invalid_arguments"), QStringLiteral("updates must be an array.")); return;
        }
        const auto updates = arguments.value(QStringLiteral("updates")).toArray();
        if (updates.isEmpty() || updates.size() > 500) {
            reject(QStringLiteral("invalid_arguments"), QStringLiteral("updates must contain 1 to 500 entries.")); return;
        }
        for (const auto& value : updates) {
            const auto update = value.toObject();
            qint64 item = 0;
            if (!value.isObject() || !allowedKeys(update, {QStringLiteral("item_id"), QStringLiteral("note"), QStringLiteral("origin")}) ||
                !positiveId(update.value(QStringLiteral("item_id")), item) || indexes.contains(item) ||
                (!update.contains(QStringLiteral("note")) && !update.contains(QStringLiteral("origin")))) {
                reject(QStringLiteral("invalid_arguments"), QStringLiteral("Each update needs a unique item_id and note and/or origin.")); return;
            }
            addItem(item);
            for (const QString field : {QStringLiteral("note"), QStringLiteral("origin")}) {
                if (!update.contains(field)) continue;
                const auto text = update.value(field);
                const int maximum = field == QStringLiteral("note") ? 100000 : 8192;
                if (!text.isString() || text.toString().toUcs4().size() > maximum || text.toString().contains(QChar(0))) {
                    reject(QStringLiteral("invalid_arguments"), QStringLiteral("The metadata text type, length or NUL content is unsupported.")); return;
                }
                steps.append({item, field, text.toString(), {}});
            }
        }
    } else if (operation == QStringLiteral("tags.assign")) {
        QVector<qint64> items;
        mode = arguments.value(QStringLiteral("mode")).toString(QStringLiteral("add"));
        if (!allowedKeys(arguments, {QStringLiteral("item_ids"), QStringLiteral("tag_ids"), QStringLiteral("mode"), QStringLiteral("dry_run")}) ||
            !parseIds(arguments.value(QStringLiteral("item_ids")), 500, items) ||
            !parseIds(arguments.value(QStringLiteral("tag_ids")), 100, tagIds) ||
            items.size() * tagIds.size() > 500 ||
            (arguments.contains(QStringLiteral("mode")) && !arguments.value(QStringLiteral("mode")).isString()) ||
            (mode != QStringLiteral("add") && mode != QStringLiteral("remove"))) {
            reject(QStringLiteral("invalid_arguments"), QStringLiteral("Tag assignment needs unique item/tag IDs and add or remove mode.")); return;
        }
        for (const qint64 item : items) addItem(item);
        if (dryRun) {
            for (const qint64 item : items) steps.append({item, QStringLiteral("tags"), QString(), tagIds});
        } else {
            if (!preflight.value(QStringLiteral("pairs")).isArray()) {
                reject(QStringLiteral("preflight_required"), QStringLiteral("The tag preflight must contain the exact changed pairs.")); return;
            }
            QHash<qint64, QVector<qint64>> changed;
            for (const QJsonValue& value : preflight.value(QStringLiteral("pairs")).toArray()) {
                const QJsonObject pair = value.toObject();
                qint64 item = 0, tag = 0;
                if (!positiveId(pair.value(QStringLiteral("item_id")), item) || !items.contains(item) ||
                    !positiveId(pair.value(QStringLiteral("tag_id")), tag) || !tagIds.contains(tag) ||
                    pair.value(QStringLiteral("action")).toString() != mode || changed.value(item).contains(tag)) {
                    reject(QStringLiteral("preflight_required"), QStringLiteral("The changed tag pairs do not match the requested operation.")); return;
                }
                changed[item].append(tag);
            }
            for (const qint64 item : items) {
                if (!changed.value(item).isEmpty()) steps.append({item, QStringLiteral("tags"), QString(), changed.value(item)});
            }
        }
    } else if (operation == QStringLiteral("tags.create")) {
        if (!allowedKeys(arguments, {QStringLiteral("name"), QStringLiteral("pid"), QStringLiteral("dry_run")}) ||
            !arguments.value(QStringLiteral("name")).isString() || arguments.value(QStringLiteral("name")).toString().trimmed().isEmpty() ||
            arguments.value(QStringLiteral("name")).toString().toUcs4().size() > 256 ||
            arguments.value(QStringLiteral("name")).toString().contains(QChar(0))) {
            reject(QStringLiteral("invalid_arguments"), QStringLiteral("Tag name must be nonempty text of at most 256 characters without NUL.")); return;
        }
        qint64 parent = 0;
        if (!positiveId(arguments.value(QStringLiteral("pid")).isUndefined() ? QJsonValue(QStringLiteral("0")) :
                        arguments.value(QStringLiteral("pid")), parent, true)) {
            reject(QStringLiteral("invalid_arguments"), QStringLiteral("Tag parent must be a nonnegative ID string.")); return;
        }
        newTag.name = arguments.value(QStringLiteral("name")).toString();
        newTag.pid = parent;
        if (!dryRun) {
            const auto plan = preflight.value(QStringLiteral("plan")).toObject();
            if (!plan.value(QStringLiteral("seq")).isDouble() || !std::isfinite(plan.value(QStringLiteral("seq")).toDouble()) ||
                plan.value(QStringLiteral("name")).toString() != newTag.name ||
                plan.value(QStringLiteral("pid")).toString() != QString::number(parent)) {
                reject(QStringLiteral("preflight_required"), QStringLiteral("Tag creation requires the validated name, parent and sequence plan.")); return;
            }
            newTag.sequence = plan.value(QStringLiteral("seq")).toDouble();
        }
        indexes.insert(0, 0);
        results.append(QJsonObject{{QStringLiteral("name"), newTag.name}, {QStringLiteral("pid"), QString::number(parent)},
                                   {QStringLiteral("ok"), true}, {QStringLiteral("fields"), QJsonObject()}});
        steps.append({0, QStringLiteral("tag.create"), QString(), {}});
    } else {
        reject(QStringLiteral("unknown_operation"), QStringLiteral("Unsupported native automation operation.")); return;
    }
    if (dryRun) {
        emit finished(id, QJsonObject{{QStringLiteral("operation"), operation}, {QStringLiteral("dry_run"), true},
                                     {QStringLiteral("library_root"), owner.root}, {QStringLiteral("step_count"), steps.size()},
                                     {QStringLiteral("item_count"), results.size()}, {QStringLiteral("submission"), QStringLiteral("none")}});
        return;
    }
    d_->requestId = id;
    d_->operation = operation;
    d_->mode = mode;
    d_->requestOwner = owner;
    d_->steps = steps;
    d_->tagIds = tagIds;
    d_->typeNames = typeNames;
    d_->newTag = newTag;
    d_->results = results;
    d_->resultIndexes = indexes;
    d_->watchedManager = owner.manager;
    d_->closedConnection = connect(owner.manager, SIGNAL(signalLibraryClosed()), this, SLOT(libraryClosed()));
    d_->destroyedConnection = connect(owner.manager, &QObject::destroyed, this, &NativeAdapter::libraryClosed);
    if (operation == QStringLiteral("items.update") || operation == QStringLiteral("items.set_cover")) {
        d_->fileAction = d_->makeAction(d_->requestOwner, 0x80, 0x1CF50, "IFileAction");
        if (!d_->fileAction ||
            !connect(d_->fileAction.data(), SIGNAL(signalFileResult(qlonglong,QSharedPointer<const IResponseInfo>)),
                     this, SLOT(fileResult(qlonglong,QSharedPointer<const IResponseInfo>))) ||
            !connect(d_->fileAction.data(), SIGNAL(signalFileProgress(qlonglong,double,int,int)),
                     this, SLOT(fileProgress(qlonglong,double,int,int)))) {
            d_->fail(QStringLiteral("native_surface_unavailable"), QStringLiteral("The native file action or result signal is unavailable.")); return;
        }
    } else if (operation == QStringLiteral("types.add")) {
        d_->typeAction = d_->makeAction(d_->requestOwner, 0x90, 0x1DA00, "ITypeAction");
        void* table = nullptr; void* function = nullptr;
        if (!d_->typeAction || !read(d_->typeAction.data(), 0, table) ||
            reinterpret_cast<quintptr>(table) != d_->dataModule.begin + 0x141B70 || !read(table, 0x58, function) ||
            reinterpret_cast<quintptr>(function) != d_->dataModule.begin + 0x8BDB0 ||
            !connect(d_->typeAction.data(), SIGNAL(signalTypeResult(qlonglong,QSharedPointer<const IResponseInfo>)),
                     this, SLOT(typeResult(qlonglong,QSharedPointer<const IResponseInfo>)))) {
            d_->fail(QStringLiteral("native_surface_unavailable"), QStringLiteral("The frozen native type-registration action is unavailable.")); return;
        }
    } else {
        d_->tagAction = d_->makeAction(d_->requestOwner, 0x98, 0x1D8A0, "ITagAction");
        if (!d_->tagAction ||
            !connect(d_->tagAction.data(), SIGNAL(signalTagResult(qlonglong,QSharedPointer<const IResponseInfo>)),
                     this, SLOT(tagResult(qlonglong,QSharedPointer<const IResponseInfo>)))) {
            d_->fail(QStringLiteral("native_surface_unavailable"), QStringLiteral("The native tag action or result signal is unavailable.")); return;
        }
    }
    if (operation == QStringLiteral("items.set_cover")) {
        void* table = nullptr; void* function = nullptr;
        if (!read(d_->fileAction.data(), 0, table) || !read(table, 0x98, function) ||
            reinterpret_cast<quintptr>(function) != d_->dataModule.begin + 0x2C650) {
            d_->fail(QStringLiteral("native_surface_unavailable"), QStringLiteral("The frozen native set-cover action identity differs.")); return;
        }
    }
    d_->ownerTimer->start();
    emit progress(id, 0, steps.size(), QStringLiteral("已提交到 Billfish 原生整理队列"));
    QTimer::singleShot(0, this, &NativeAdapter::continueRequest);
}

void NativeAdapter::continueRequest() {
    if (d_->requestId.isEmpty() || d_->activeActionId) return;
    if (!d_->stillOwner()) { libraryClosed(); return; }
    if (d_->stepIndex >= d_->steps.size()) {
        const QString id = d_->requestId;
        const QJsonObject result{
            {QStringLiteral("operation"), d_->operation}, {QStringLiteral("dry_run"), false},
            {QStringLiteral("library_root"), d_->requestOwner.root},
            {QStringLiteral("status"), d_->failedSteps ? QStringLiteral("partial_failure") : QStringLiteral("completed")},
            {QStringLiteral("completed_steps"), d_->stepIndex}, {QStringLiteral("failed_steps"), d_->failedSteps},
            {QStringLiteral("results"), d_->results}, {QStringLiteral("completion"), QStringLiteral("native_action_result")}};
        d_->reset();
        emit finished(id, result);
        return;
    }
    const auto step = d_->steps.at(d_->stepIndex);
    const QVector<qint64> ids{step.itemId};
    qlonglong token = 0;
    bool invoked = false;
    if (step.field == QStringLiteral("types")) {
        invoked = QMetaObject::invokeMethod(d_->typeAction.data(), "slotAddTypes", Qt::DirectConnection,
            Q_RETURN_ARG(qlonglong, token), QGenericArgument("QVector<QString>", &d_->typeNames));
    } else if (step.field == QStringLiteral("cover")) {
        invoked = QMetaObject::invokeMethod(d_->fileAction.data(), "slotSetFileCover", Qt::DirectConnection,
            Q_RETURN_ARG(qlonglong, token), Q_ARG(qlonglong, step.itemId), Q_ARG(QString, step.value));
    } else if (step.field == QStringLiteral("tag.create")) {
        const QVector<ITagNodeInfo> tags{d_->newTag};
        invoked = QMetaObject::invokeMethod(d_->tagAction.data(), "slotCreateTags", Qt::DirectConnection,
            Q_RETURN_ARG(qlonglong, token), QGenericArgument("QVector<ITagNodeInfo>", &tags));
    } else if (step.field == QStringLiteral("tags")) {
        if (d_->mode == QStringLiteral("add")) {
            QVector<DynamicTag> tags;
            for (const qint64 tagId : step.selectedTags) tags.append(DynamicTag{tagId, QString()});
            const bool checkConflict = true;
            invoked = QMetaObject::invokeMethod(d_->tagAction.data(), "slotFilesAssociateTags", Qt::DirectConnection,
                Q_RETURN_ARG(qlonglong, token), QGenericArgument("QVector<qint64>", &ids),
                QGenericArgument("QVector<DynamicTag>", &tags), Q_ARG(bool, checkConflict));
        } else {
            invoked = QMetaObject::invokeMethod(d_->tagAction.data(), "slotFilesDisassociateTags", Qt::DirectConnection,
                Q_RETURN_ARG(qlonglong, token), QGenericArgument("QVector<qint64>", &ids),
                QGenericArgument("QVector<qint64>", &step.selectedTags));
        }
    } else {
        invoked = QMetaObject::invokeMethod(d_->fileAction.data(),
            step.field == QStringLiteral("note") ? "slotSetFilesNote" : "slotSetFilesOrigin", Qt::DirectConnection,
            Q_RETURN_ARG(qlonglong, token), QGenericArgument("QVector<qint64>", &ids), Q_ARG(QString, step.value));
    }
    if (!invoked || token <= 0) {
        d_->fail(QStringLiteral("native_submission_failed"), QStringLiteral("The native action did not accept this request. Earlier completed actions remain applied."));
        return;
    }
    d_->activeActionId = token;
}

void NativeAdapter::fileResult(qlonglong actionId, QSharedPointer<const IResponseInfo> response) {
    d_->completeStep(actionId, response, false);
}

void NativeAdapter::tagResult(qlonglong actionId, QSharedPointer<const IResponseInfo> response) {
    d_->completeStep(actionId, response, true);
}

void NativeAdapter::typeResult(qlonglong actionId, QSharedPointer<const IResponseInfo> response) {
    d_->completeStep(actionId, response, false);
}

void NativeAdapter::fileProgress(qlonglong actionId, double fraction, int total, int already) {
    if (d_->requestId.isEmpty() || d_->activeActionId != actionId) return;
    Q_UNUSED(fraction)
    emit progress(d_->requestId, d_->stepIndex, d_->steps.size(),
                  QStringLiteral("原生任务处理进度: %1/%2").arg(already).arg(total));
}

void NativeAdapter::libraryClosed() {
    if (d_->importing) d_->abortImport(QStringLiteral("library_changed"),
        QStringLiteral("The active library closed or changed during native import. Its outcome is not rolled back."));
    if (!d_->activeRoot.isEmpty()) {
        d_->activeRoot.clear();
        emit activeLibraryChanged(QString());
    }
    if (!d_->requestId.isEmpty()) d_->fail(QStringLiteral("library_changed"),
        QStringLiteral("The active native library closed or changed. No remaining actions were submitted; earlier completed actions remain applied."));
}

void NativeAdapter::verifyOwner() {
    if (!d_->requestId.isEmpty() && !d_->stillOwner()) libraryClosed();
    if (d_->importing && !d_->stillImportOwner()) d_->abortImport(QStringLiteral("library_changed"),
        QStringLiteral("The active library changed during native import. Its outcome is not rolled back."));
}

} // namespace billfish::automation
