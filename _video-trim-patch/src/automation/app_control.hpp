#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <functional>

class QWidget;
class QAbstractItemView;

namespace billfish::automation {
class AppControl final : public QObject {
public:
    explicit AppControl(QObject* parent = nullptr);
    void setContextProvider(std::function<QJsonObject()> provider);
    // GUI thread only. Input results acknowledge queued dispatch, never business success.
    QJsonObject handle(const QString& operation, const QJsonObject& arguments, const QString& requestId = QString());
private:
    struct Target {
        QPointer<QObject> object;
        QPointer<QWidget> ownerWidget;
        QPointer<QAbstractItemView> view;
        QPersistentModelIndex index;
        bool modelItem = false;
    };
    QJsonObject snapshot(const QJsonObject& arguments, const QJsonObject& context);
    QString registerTarget(QObject* object, QAbstractItemView* view = nullptr,
                           const QModelIndex& index = QModelIndex(), QWidget* ownerWidget = nullptr);
    bool current(const QString& snapshotId, const QString& libraryRoot) const;
    QHash<QString, Target> targets_;
    QString snapshotId_;
    QString libraryRoot_;
    quint64 nextRef_ = 0;
    quint64 epoch_ = 0;
    quint64 actionSequence_ = 0;
    QJsonObject latestAction_;
    std::function<QJsonObject()> contextProvider_;
};
}
