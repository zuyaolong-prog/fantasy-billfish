#pragma once

#include <QByteArrayList>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

class QAction;
class QAbstractButton;
class QMenu;
class QTimer;

namespace billfish::shell {

class LegacyFeatureGate final : public QObject {
    Q_OBJECT
public:
    explicit LegacyFeatureGate(QObject* parent = nullptr);
    void start();
    void enforce(QObject* root);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void enqueue(QObject* object);
    void flush();
    void inspect(QObject* object);
    void inspectMenu(QMenu* menu);
    void blockAction(QAction* action);
    void moveImportHistory();
    void remember(QObject* object);
    void disconnectIncoming(QObject* sender, QObject* receiver, const QByteArrayList& businessSlots);

    QTimer* timer_;
    QList<QPointer<QObject>> pending_;
    QHash<QObject*, QPointer<QObject>> objects_;
    QHash<QObject*, QByteArrayList> protectedSlots_;
    QSet<QObject*> libraryMenus_;
    QSet<QObject*> accountMenus_;
    QPointer<QMenu> mainMenu_;
    QPointer<QAbstractButton> pressedMainMenuButton_;
    QPointer<QMenu> historyMenu_;
    QPointer<QAction> importHistory_;
};

void installLegacyFeatureGate();

} // namespace billfish::shell
