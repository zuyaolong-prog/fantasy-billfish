#include "automation/native_cover_geometry.hpp"

#include <QCoreApplication>
#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QModelIndex>
#include <QPainter>
#include <QFont>
#include <QPalette>
#include <QRect>
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QPointer>
#include <QSize>
#include <QThread>
#include <array>
#include <intrin.h>
#include <limits>
#include <windows.h>
#pragma intrinsic(_ReturnAddress)

namespace billfish::automation {
namespace {
using Builder = QString* (__fastcall*)(const void*, QString*, const void*, const QSize*, int);
Builder originalBuilder = nullptr;
using ImagePainter = void (__fastcall*)(const void*, QPainter*, const QPoint*, const void*, const QModelIndex*);
ImagePainter originalImagePainter = nullptr;
QHash<const void*, NativeCoverGeometryPrivate*>& statusAttachments() {
    static QHash<const void*, NativeCoverGeometryPrivate*> value;
    return value;
}
QHash<const void*, NativeCoverGeometryPrivate*>& attachments() {
    static QHash<const void*, NativeCoverGeometryPrivate*> value;
    return value;
}
template<class T> bool read(const void* object, size_t offset, T& value) {
    SIZE_T bytes = 0;
    return object && ReadProcessMemory(GetCurrentProcess(), static_cast<const char*>(object) + offset,
        &value, sizeof(value), &bytes) && bytes == sizeof(value);
}
bool identity(HMODULE module, const QByteArray& expected) {
    wchar_t path[32768] = {};
    if (!module || !GetModuleFileNameW(module, path, 32768)) return false;
    QFile file(QString::fromWCharArray(path)); QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) && hash.result().toHex() == expected;
}
__declspec(noinline) QString* __fastcall buildCoverKey(const void* self, QString* key, const void* info, const QSize* size, int flag);
void __fastcall paintPreviewStatus(const void* self, QPainter* painter, const QPoint* topLeft,
    const void* geometry, const QModelIndex* index);
}

class NativeCoverGeometryPrivate {
public:
    NativeCoverGeometry* q;
    QPointer<QObject> manager;
    QMetaObject::Connection destroyed;
    HMODULE viewModule = nullptr, cacheModule = nullptr, dataModule = nullptr;
    quintptr view = 0, cache = 0, data = 0;
    bool checked = false, compatible = false;
    std::array<void*, 26> clone{};
    void* original = nullptr;
    QPointer<QAbstractItemView> statusView;
    QPointer<QAbstractItemDelegate> statusDelegate;
    QMetaObject::Connection statusDestroyed;
    std::array<void*, 52> statusClone{};
    void* originalStatusTable = nullptr;
    NativeCoverGeometry::CaptionProvider caption;

    explicit NativeCoverGeometryPrivate(NativeCoverGeometry* object) : q(object) {}
    bool guiThread() const { return QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread(); }
    bool modules() {
        if (!GetModuleHandleW(L"viewmodule.dll") || !GetModuleHandleW(L"basemodule.dll") || !GetModuleHandleW(L"datacenter.dll")) return false;
        if (!checked) {
            checked = true;
            viewModule = GetModuleHandleW(L"viewmodule.dll"); cacheModule = GetModuleHandleW(L"basemodule.dll"); dataModule = GetModuleHandleW(L"datacenter.dll");
            compatible = identity(viewModule, QByteArrayLiteral("ad1e8e407433f8028ad7418afa485ff3a80dfabded2e4f65b4aece283ced57db")) &&
                identity(cacheModule, QByteArrayLiteral("d2fdefec8f03a806c44507c5030a0470b121ba5a3d4d720af93ecf6d1d867bf1")) &&
                identity(dataModule, QByteArrayLiteral("c3086ed8efce3d2dc11a482b2798eaa1b5f171970b05a681015bece4d5b27f09"));
            if (compatible) {
                view = reinterpret_cast<quintptr>(viewModule); cache = reinterpret_cast<quintptr>(cacheModule); data = reinterpret_cast<quintptr>(dataModule);
                originalBuilder = reinterpret_cast<Builder>(cache + 0x3440);
                originalImagePainter = reinterpret_cast<ImagePainter>(view + 0x38100);
            }
        }
        return compatible && GetModuleHandleW(L"viewmodule.dll") == viewModule &&
            GetModuleHandleW(L"basemodule.dll") == cacheModule && GetModuleHandleW(L"datacenter.dll") == dataModule;
    }
    void clearStatus() {
        if (!guiThread()) return;
        auto* object = statusDelegate.data(); void* table = nullptr;
        if (object && read(object, 0, table) && table == &statusClone[1])
            *reinterpret_cast<void**>(object) = originalStatusTable;
        if (object && statusAttachments().value(object) == this) statusAttachments().remove(object);
        QObject::disconnect(statusDestroyed); statusDestroyed = {};
        statusDelegate.clear(); statusView.clear(); caption = {};
    }
    bool attachStatus(QAbstractItemView* current, NativeCoverGeometry::CaptionProvider provider) {
        if (!guiThread() || !modules()) return false;
        auto* currentDelegate = current ? current->itemDelegate() : nullptr;
        if (statusView == current && statusDelegate == currentDelegate && statusDelegate) return false;
        clearStatus();
        if (!current || !currentDelegate || !provider || !current->model() ||
            current->thread() != q->thread() || currentDelegate->thread() != q->thread()) return false;
        void* table = nullptr; void* image = nullptr; void* locator = nullptr;
        if (!read(currentDelegate, 0, table) || !read(table, 0x130, image) ||
            image != reinterpret_cast<void*>(originalImagePainter)) return false;
        const auto address = reinterpret_cast<quintptr>(table);
        const auto expectedLocator = address == view + 0x504E8 ? view + 0x576E8 :
            address == view + 0x546E0 ? view + 0x58438 : 0;
        if (!expectedLocator || !read(static_cast<char*>(table) - 8, 0, locator) ||
            reinterpret_cast<quintptr>(locator) != expectedLocator) return false;
        SIZE_T bytes = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), static_cast<char*>(table) - 8,
            statusClone.data(), sizeof(statusClone), &bytes) || bytes != sizeof(statusClone)) return false;
        originalStatusTable = table;
        statusClone[1 + 0x130 / 8] = reinterpret_cast<void*>(&paintPreviewStatus);
        statusDelegate = currentDelegate; statusView = current; caption = std::move(provider);
        statusAttachments().insert(currentDelegate, this);
        statusDestroyed = QObject::connect(currentDelegate, &QObject::destroyed, q, [this, currentDelegate] {
            if (statusAttachments().value(currentDelegate) == this) statusAttachments().remove(currentDelegate);
            statusDelegate.clear(); statusView.clear(); caption = {};
        });
        *reinterpret_cast<void**>(currentDelegate) = &statusClone[1];
        return true;
    }
    bool paintStatus(QPainter* painter, const QPoint* topLeft, const void* geometry, const QModelIndex* index) {
        if (!guiThread() || !painter || !painter->isActive() || !topLeft || !geometry || !index ||
            !statusView || !statusDelegate || !caption || !index->isValid() ||
            index->model() != statusView->model() || statusView->itemDelegate() != statusDelegate) return false;
        QRect nativeRect; QPoint origin;
        if (!read(geometry, 0x14, nativeRect) || !read(topLeft, 0, origin) ||
            nativeRect.width() <= 0 || nativeRect.height() <= 0 ||
            nativeRect.width() > 8192 || nativeRect.height() > 8192) return false;
        const QString text = caption(*index);
        if (text.isEmpty()) return false;
        const QRect rect(origin, nativeRect.size());
        painter->save();
        painter->setClipRect(rect, Qt::IntersectClip);
        const auto palette = statusView->palette();
        painter->fillRect(rect, palette.brush(QPalette::Base));
        painter->setPen(palette.color(QPalette::Disabled, QPalette::Text));
        QFont font = painter->font(); font.setPixelSize(12); font.setBold(false); painter->setFont(font);
        painter->drawText(rect.adjusted(6, 4, -6, -4), Qt::AlignCenter | Qt::TextWordWrap, text);
        painter->restore();
        return true;
    }
    void clear() {
        if (!guiThread()) return;
        clearStatus();
        auto* object = manager.data(); void* table = nullptr;
        if (object && read(object, 0, table) && table == &clone[1]) *reinterpret_cast<void**>(object) = original;
        if (object && attachments().value(object) == this) attachments().remove(object);
        QObject::disconnect(destroyed); destroyed = {}; manager.clear();
    }
    bool attach() {
        if (!guiThread() || !modules()) return false;
        void* singleton = nullptr; QObject* current = nullptr; void* table = nullptr; void* builder = nullptr;
        if (!read(reinterpret_cast<void*>(view), 0x72970, singleton) || !read(singleton, 0, current)) return false;
        if (manager && manager.data() != current) clear();
        if (manager) return false;
        if (!read(current, 0, table) || reinterpret_cast<quintptr>(table) != cache + 0xde28 ||
            !read(table, 0x70, builder) || builder != reinterpret_cast<void*>(originalBuilder) ||
            !current->inherits("IBFCacheManager") || current->thread() != q->thread()) return false;
        SIZE_T bytes = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), static_cast<char*>(table) - 8, clone.data(), sizeof(clone), &bytes) ||
            bytes != sizeof(clone) || reinterpret_cast<quintptr>(clone[0]) != cache + 0xe9f8) return false;
        original = table; clone[1 + 0x70 / 8] = reinterpret_cast<void*>(&buildCoverKey); manager = current;
        attachments().insert(current, this);
        destroyed = QObject::connect(current, &QObject::destroyed, q, [this, current] {
            if (attachments().value(current) == this) attachments().remove(current);
            manager.clear();
        });
        *reinterpret_cast<void**>(current) = &clone[1];
        return true;
    }
    bool normalize(const void* caller, const void* sharedInfo, const QSize* requested, QSize& replacement) const {
        if (reinterpret_cast<quintptr>(caller) != view + 0x383c8 || !sharedInfo || !requested ||
            requested->width() <= 0 || requested->height() != (std::numeric_limits<int>::min)()) return false;
        void* info = nullptr; void* table = nullptr; void* extensionGetter = nullptr;
        qint64 cover = 0; int width = 0, height = 0;
        if (!read(sharedInfo, 0, info) || !read(info, 0, table) || reinterpret_cast<quintptr>(table) != data + 0x13b7d0 ||
            !read(info, 0x53, cover) || cover <= 0 || !read(info, 0x36, width) || !read(info, 0x3a, height) ||
            (width > 0 && height > 0) || !read(table, 0x38, extensionGetter) || reinterpret_cast<quintptr>(extensionGetter) != data + 0x2d8c0) return false;
        using Extension = void* (__fastcall*)(const void*, QString*);
        QString extension; reinterpret_cast<Extension>(extensionGetter)(info, &extension); extension = extension.toLower();
        if (extension != QStringLiteral(".fbx") && extension != QStringLiteral(".glb")) return false;
        // The source is a non-image mesh. Its native dimensions remain zero,
        // but its custom cover has already been preloaded under this square
        // request. Preserve all native flags and request/queue semantics.
        replacement = QSize(requested->width(), requested->width());
        return true;
    }
};

namespace {
void __fastcall paintPreviewStatus(const void* self, QPainter* painter, const QPoint* topLeft,
    const void* geometry, const QModelIndex* index) {
    auto* patch = statusAttachments().value(self, nullptr);
    if (patch && patch->paintStatus(painter, topLeft, geometry, index)) return;
    if (originalImagePainter) originalImagePainter(self, painter, topLeft, geometry, index);
}
__declspec(noinline) QString* __fastcall buildCoverKey(const void* self, QString* key, const void* info, const QSize* size, int flag) {
    const void* caller = _ReturnAddress(); QSize replacement;
    const auto* patch = attachments().value(self, nullptr);
    const bool normalized = patch && patch->normalize(caller, info, size, replacement);
    // Frozen 0x3440 reads its fifth argument as DWORD, not bool. Passing the
    // exact int32 preserves ordinary image/video keys and all native options.
    return originalBuilder(self, key, info, normalized ? &replacement : size, flag);
}
}

NativeCoverGeometry::NativeCoverGeometry(QObject* parent) : QObject(parent), d_(std::make_unique<NativeCoverGeometryPrivate>(this)) {}
NativeCoverGeometry::~NativeCoverGeometry() { d_->clear(); }
bool NativeCoverGeometry::attach() { return d_->attach(); }
bool NativeCoverGeometry::attachStatus(QAbstractItemView* view, CaptionProvider provider) { return d_->attachStatus(view, std::move(provider)); }
void NativeCoverGeometry::clearStatus() { d_->clearStatus(); }
void NativeCoverGeometry::clear() { d_->clear(); }
}
