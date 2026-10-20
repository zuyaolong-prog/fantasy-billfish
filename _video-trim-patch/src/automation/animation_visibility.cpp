#include "animation_visibility.hpp"
#include "native_adapter.hpp"

#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QThread>

#include <algorithm>
#include <array>
#include <memory>
#include <windows.h>

namespace billfish::automation {
namespace {
using Predicate = bool (__fastcall*)(const void*, int, const QModelIndex*);
constexpr quintptr FilterTable = 0x15488;
constexpr quintptr FilterLocator = 0x17a88;
constexpr quintptr PredicateAddress = 0x92f0;
constexpr size_t PredicateSlot = 0x190 / sizeof(void*);

bool guiThread() {
    auto* app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

template<class T> bool read(const void* object, size_t offset, T& value) {
    SIZE_T count = 0;
    return object && ReadProcessMemory(GetCurrentProcess(), static_cast<const char*>(object) + offset,
        &value, sizeof(value), &count) && count == sizeof(value);
}

QString normalized(QString path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
}

struct Binding {
    QPointer<QSortFilterProxyModel> proxy;
    QPointer<QAbstractItemModel> source;
    QPointer<NativeAdapter> native;
    void* original = nullptr;
    // The preceding Complete Object Locator followed by all 63 primary slots.
    std::array<void*, 64> clone{};
    QByteArray identity;
    QSet<QString> ids;
    quintptr module = 0;
    bool active = false;
    bool refreshing = false;
    QMetaObject::Connection sourceChanged;
    QMetaObject::Connection sourceDestroyed;
    QMetaObject::Connection proxyDestroyed;

    ~Binding() {
        QObject::disconnect(sourceChanged);
        QObject::disconnect(sourceDestroyed);
        QObject::disconnect(proxyDestroyed);
    }
};

// All access, including native predicate dispatch, is on the Qt GUI thread.
QHash<const void*, Binding*> bindings;
Predicate originalPredicate = nullptr;

QString fileId(const QModelIndex& index, quintptr module) {
    if (!index.isValid()) return {};
    const auto value = index.data(0x164);
    const auto itemType = QMetaType::type("IDataItem*");
    if (itemType == QMetaType::UnknownType || value.userType() != itemType) return {};
    void* item = nullptr;
    void* table = nullptr;
    void* info = nullptr;
    int type = 0;
    qint64 id = 0;
    if (!read(value.constData(), 0, item) || !read(item, 0, table) ||
        !read(item, 8, type) || type != 2 ||
        reinterpret_cast<quintptr>(table) != module + 0x15a80 ||
        !read(item, 0x20, info) || !read(info, 8, id) || id <= 0) return {};
    return QString::number(id);
}

bool __fastcall acceptsRow(const void* self, int row, const QModelIndex* parent) {
    if (guiThread()) {
        auto* state = bindings.value(self, nullptr);
        if (state && state->active && state->proxy && state->source && state->native && parent &&
            state->proxy->sourceModel() == state->source &&
            state->native->libraryIdentity() == state->identity) {
            const auto id = fileId(state->source->index(row, 0, *parent), state->module);
            if (!id.isEmpty() && state->ids.contains(id)) return false;
        }
    }
    // Every row outside this exact exclusion binding retains native semantics.
    return originalPredicate(self, row, parent);
}

struct SavedSelection {
    QPointer<QItemSelectionModel> model;
    QList<QPersistentModelIndex> indexes;
    QPersistentModelIndex current;
};

QList<SavedSelection> saveSelections(const Binding& state, QAbstractItemView* currentView) {
    QList<SavedSelection> result;
    QSet<QItemSelectionModel*> seen;
    for (auto* widget : QApplication::allWidgets()) {
        auto* view = qobject_cast<QAbstractItemView*>(widget);
        if (!view || view->model() != state.proxy || !view->selectionModel() ||
            seen.contains(view->selectionModel())) continue;
        seen.insert(view->selectionModel());
        SavedSelection saved;
        saved.model = view->selectionModel();
        for (const auto& index : saved.model->selectedIndexes())
            saved.indexes.append(state.proxy->mapToSource(index));
        saved.current = state.proxy->mapToSource(saved.model->currentIndex());
        // The current view's native selection/details notification must win.
        if (view == currentView) result.append(saved);
        else result.prepend(saved);
    }
    return result;
}

void restoreSelections(const Binding& state, const QList<SavedSelection>& selections) {
    if (!state.proxy || !state.source || state.proxy->sourceModel() != state.source) return;
    for (const auto& saved : selections) {
        if (!saved.model || saved.model->model() != state.proxy) continue;
        QItemSelection selection;
        for (const auto& source : saved.indexes) {
            if (!source.isValid() || source.model() != state.source) continue;
            const auto index = state.proxy->mapFromSource(source);
            if (index.isValid()) selection.select(index, index);
        }
        saved.model->clearSelection();
        saved.model->select(selection, QItemSelectionModel::Select);
        const auto current = saved.current.isValid() && saved.current.model() == state.source
            ? state.proxy->mapFromSource(saved.current) : QModelIndex{};
        saved.model->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    }
}

QJsonObject failure(const QString& code, const QString& message) {
    return {{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
        {QStringLiteral("message"), message}}}};
}

QJsonObject success(const QSet<QString>& ids) {
    auto sorted = ids.values();
    std::sort(sorted.begin(), sorted.end());
    QJsonArray hidden;
    for (const auto& id : sorted) hidden.append(id);
    return {{QStringLiteral("data"), QJsonObject{{QStringLiteral("applied"), true},
        {QStringLiteral("hidden_item_ids"), hidden},
        {QStringLiteral("state"), ids.isEmpty() ? QStringLiteral("visible") : QStringLiteral("filtered")}}}};
}
} // namespace

class AnimationVisibilityPrivate {
public:
    AnimationVisibility* q;
    QPointer<NativeAdapter> native;
    QHash<QSortFilterProxyModel*, std::shared_ptr<Binding>> states;
    quintptr module = 0;

    AnimationVisibilityPrivate(AnimationVisibility* owner, NativeAdapter* adapter) : q(owner), native(adapter) {}

    bool authenticateModule() {
        const auto handle = GetModuleHandleW(L"datamodel.dll");
        if (module) return reinterpret_cast<quintptr>(handle) == module;
        wchar_t path[32768] = {};
        if (!handle || !GetModuleFileNameW(handle, path, 32768)) return false;
        QFile file(QString::fromWCharArray(path));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file) ||
            hash.result().toHex() != QByteArrayLiteral("4d47008537fd72ccbe63f84c87b1b822f661e1f83186d56acbae37cf3af3d244")) return false;
        module = reinterpret_cast<quintptr>(handle);
        originalPredicate = reinterpret_cast<Predicate>(module + PredicateAddress);
        return true;
    }

    bool sameOwner(const Binding& state) const {
        return native && !state.identity.isEmpty() && native->libraryIdentity() == state.identity;
    }

    bool refresh(Binding& state) {
        if (!state.proxy || !state.source || state.proxy->sourceModel() != state.source || !sameOwner(state)) return false;
        state.refreshing = true;
        const bool invoked = QMetaObject::invokeMethod(state.proxy.data(), "slotFileOrFolderHidden", Qt::DirectConnection);
        state.refreshing = false;
        return invoked && state.proxy && state.source && state.proxy->sourceModel() == state.source && sameOwner(state);
    }

    void detach(const std::shared_ptr<Binding>& state, bool update, QAbstractItemView* view = nullptr) {
        state->active = false;
        state->ids.clear();
        if (bindings.value(state->proxy.data(), nullptr) == state.get()) bindings.remove(state->proxy.data());
        void* current = nullptr;
        if (!state->proxy || !read(state->proxy.data(), 0, current) || current != &state->clone[1]) return;
        const bool canRefresh = update && state->source && state->proxy->sourceModel() == state->source && sameOwner(*state);
        const auto saved = canRefresh ? saveSelections(*state, view) : QList<SavedSelection>{};
        *reinterpret_cast<void**>(state->proxy.data()) = state->original;
        if (canRefresh && refresh(*state)) restoreSelections(*state, saved);
    }

    bool discard(QSortFilterProxyModel* key, const std::shared_ptr<Binding>& state) {
        if (states.value(key) != state) return false;
        states.remove(key);
        detach(state, false);
        return true;
    }

    void clear(bool update) {
        auto previous = states;
        states.clear();
        if (previous.isEmpty()) return;
        // This may observe a library transition. No installed state is freed by
        // its reentrant signal; the local shared ownership covers native refresh.
        QPointer<QAbstractItemView> view = update && native ? native->currentMaterialView() : nullptr;
        for (const auto& state : previous) detach(state, update, view);
    }

    QJsonObject replace(const QString& libraryRoot, const QSet<QString>& ids) {
        if (!guiThread() || q->thread() != QThread::currentThread() ||
            !native || native->thread() != QThread::currentThread())
            return failure(QStringLiteral("gui_thread_required"), QStringLiteral("Animation visibility requires the native Qt GUI thread."));
        if (ids.isEmpty()) {
            clear(true);
            return success({});
        }
        for (const auto& id : ids) {
            bool ok = false;
            const auto numeric = id.toLongLong(&ok);
            if (!ok || numeric <= 0 || QString::number(numeric) != id) {
                clear(true);
                return failure(QStringLiteral("invalid_item_id"), QStringLiteral("Animation visibility requires positive canonical catalog IDs."));
            }
        }
        const auto info = native->info();
        const auto identity = native->libraryIdentity();
        auto* view = native->currentMaterialView();
        if (!info.value(QStringLiteral("ready")).toBool() || identity.isEmpty() ||
            normalized(libraryRoot) != normalized(info.value(QStringLiteral("library_root")).toString()) ||
            native->libraryIdentity() != identity) {
            clear(false);
            return failure(QStringLiteral("library_changed"), QStringLiteral("The native library no longer matches the animation snapshot."));
        }
        auto* proxy = view ? qobject_cast<QSortFilterProxyModel*>(view->model()) : nullptr;
        if (!proxy || QByteArray(proxy->metaObject()->className()) != QByteArrayLiteral("BFDataModelFilter") ||
            !proxy->sourceModel() || !authenticateModule()) {
            clear(true);
            return failure(QStringLiteral("unsupported_material_filter"), QStringLiteral("The authenticated native material filter is unavailable."));
        }
        auto state = states.value(proxy);
        void* installed = nullptr;
        if (state && (state->source != proxy->sourceModel() || state->identity != identity ||
            !read(proxy, 0, installed) || installed != &state->clone[1])) {
            discard(proxy, state);
            state.reset();
        }
        if (state && state->active && state->ids == ids) return success(ids);
        if (!state) {
            void* table = nullptr;
            void* predicate = nullptr;
            if (bindings.contains(proxy) || !read(proxy, 0, table) ||
                reinterpret_cast<quintptr>(table) != module + FilterTable ||
                !read(table, 0x190, predicate) || predicate != reinterpret_cast<void*>(originalPredicate)) {
                clear(true);
                return failure(QStringLiteral("unsupported_material_filter"), QStringLiteral("The native filter table is not the authenticated frozen table."));
            }
            state = std::make_shared<Binding>();
            state->proxy = proxy;
            state->source = proxy->sourceModel();
            state->native = native;
            state->original = table;
            state->module = module;
            state->identity = identity;
            SIZE_T bytes = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), static_cast<char*>(table) - sizeof(void*),
                    state->clone.data(), sizeof(state->clone), &bytes) || bytes != sizeof(state->clone) ||
                reinterpret_cast<quintptr>(state->clone[0]) != module + FilterLocator) {
                clear(true);
                return failure(QStringLiteral("unsupported_material_filter"), QStringLiteral("The native filter RTTI/table could not be authenticated."));
            }
            state->clone[1 + PredicateSlot] = reinterpret_cast<void*>(&acceptsRow);
            const std::weak_ptr<Binding> weak = state;
            state->sourceChanged = QObject::connect(proxy, &QAbstractProxyModel::sourceModelChanged, q, [this, proxy, weak] {
                if (const auto current = weak.lock()) {
                    // Native layout changes can reset null -> the same source,
                    // so polling the final pointer cannot detect this loss.
                    if (!current->refreshing && discard(proxy, current))
                        emit q->bindingInvalidated();
                }
            });
            state->sourceDestroyed = QObject::connect(state->source.data(), &QObject::destroyed, q, [this, proxy, weak] {
                if (const auto current = weak.lock()) {
                    if (discard(proxy, current)) emit q->bindingInvalidated();
                }
            });
            state->proxyDestroyed = QObject::connect(proxy, &QObject::destroyed, q, [this, proxy, weak] {
                if (const auto current = weak.lock()) {
                    bindings.remove(proxy);
                    states.remove(proxy);
                    // Native destruction already restored its vptr. Never write
                    // to an object that has entered QObject/base destruction.
                }
            });
            states.insert(proxy, state);
        }
        // Apply a complete snapshot to every already-authenticated instance of
        // this owner. Layouts sharing one proxy naturally share this binding.
        const auto attached = states;
        for (const auto& candidate : attached) {
            void* table = nullptr;
            if (candidate->identity != identity || !candidate->source || !candidate->proxy ||
                candidate->proxy->sourceModel() != candidate->source ||
                !read(candidate->proxy.data(), 0, table) ||
                table != (candidate->active ? &candidate->clone[1] : candidate->original)) {
                if (candidate->proxy) discard(candidate->proxy, candidate);
                continue;
            }
            if (candidate->active && candidate->ids == ids) continue;
            const auto saved = saveSelections(*candidate, view);
            candidate->identity = identity;
            candidate->ids = ids;
            candidate->active = true;
            bindings.insert(candidate->proxy, candidate.get());
            *reinterpret_cast<void**>(candidate->proxy.data()) = &candidate->clone[1];
            if (!refresh(*candidate) || !candidate->active ||
                states.value(candidate->proxy.data()) != candidate ||
                bindings.value(candidate->proxy.data(), nullptr) != candidate.get()) {
                clear(true);
                return failure(QStringLiteral("native_filter_refresh_failed"), QStringLiteral("The native visibility refresh lost its authenticated binding."));
            }
            restoreSelections(*candidate, saved);
        }
        if (states.value(proxy) != state || !state->active || !sameOwner(*state)) {
            clear(true);
            return failure(QStringLiteral("native_filter_refresh_failed"), QStringLiteral("The native visibility binding changed during refresh."));
        }
        return success(ids);
    }
};

AnimationVisibility::AnimationVisibility(NativeAdapter* native, QObject* parent)
    : QObject(parent), d_(std::make_unique<AnimationVisibilityPrivate>(this, native)) {
    Q_ASSERT(guiThread() && native && native->thread() == thread());
    if (native) {
        connect(native, &NativeAdapter::activeLibraryChanged, this, [this] { d_->clear(false); });
        connect(native, &QObject::destroyed, this, [this] { d_->clear(false); });
    }
    if (auto* app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, [this] { d_->clear(false); });
}

AnimationVisibility::~AnimationVisibility() {
    Q_ASSERT(guiThread());
    d_->clear(!QCoreApplication::closingDown());
}

QJsonObject AnimationVisibility::replace(const QString& libraryRoot, const QSet<QString>& ids) {
    return d_->replace(libraryRoot, ids);
}

void AnimationVisibility::clear() {
    Q_ASSERT(guiThread());
    d_->clear(true);
}

} // namespace billfish::automation
