#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

namespace billfish::automation {

class NativeAdapter;
class AnimationVisibilityPrivate;

// Owned, called and destroyed on the Qt GUI thread, alongside NativeAdapter.
class AnimationVisibility final : public QObject {
    Q_OBJECT
public:
    explicit AnimationVisibility(NativeAdapter* native, QObject* parent = nullptr);
    ~AnimationVisibility() override;

    // ids are a complete, trusted catalog-ID snapshot for this library root.
    // A changed source requires a new authenticated snapshot from the owner.
    QJsonObject replace(const QString& libraryRoot, const QSet<QString>& ids);
    void clear();

signals:
    // External source reset/replacement discarded an authenticated binding.
    // The owner must obtain a fresh path-to-ID snapshot before replacing it.
    void bindingInvalidated();

private:
    std::unique_ptr<AnimationVisibilityPrivate> d_;
};

} // namespace billfish::automation
