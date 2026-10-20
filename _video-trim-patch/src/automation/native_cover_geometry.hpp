#pragma once

#include <QObject>
#include <memory>
#include <functional>

class QAbstractItemView;
class QModelIndex;

namespace billfish::automation {
class NativeCoverGeometryPrivate;

// Adapts frozen native 3D cover geometry and plugin placeholder presentation.
class NativeCoverGeometry final : public QObject {
public:
    explicit NativeCoverGeometry(QObject* parent = nullptr);
    ~NativeCoverGeometry() override;
    // Returns true only when a new native instance was attached.
    bool attach();
    // A nonempty caption must authenticate the native mode-2 item and its
    // enabled preview handler. The callback must not parse or mutate sources.
    using CaptionProvider = std::function<QString(const QModelIndex&)>;
    bool attachStatus(QAbstractItemView* view, CaptionProvider provider);
    void clearStatus();
    void clear();
private:
    std::unique_ptr<NativeCoverGeometryPrivate> d_;
};
}
