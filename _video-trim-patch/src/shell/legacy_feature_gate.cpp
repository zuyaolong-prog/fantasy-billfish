#include "shell/legacy_feature_gate.hpp"
#include "automation/scoped_popup.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QChildEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMetaMethod>
#include <QLineEdit>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpacerItem>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace billfish::shell {
namespace {
bool externalPage(const QObject* object) {
    return (object->inherits("BFWebWidget") && object->objectName() == QStringLiteral("BFWebWidget"))
        || (object->inherits("BFNormalWebWidget")
            && (object->objectName() == QStringLiteral("KuQue") || object->objectName() == QStringLiteral("YuanChuangAi")));
}

bool withinExternalPage(const QObject* object) {
    for (const QObject* parent = object; parent; parent = parent->parent()) {
        if (externalPage(parent)) return true;
    }
    return false;
}

bool cloudTitleButton(const QObject* object) {
    if (!object->inherits("QAbstractButton") || (object->objectName() != QStringLiteral("BFDockAreaAddButton")
        && object->objectName() != QStringLiteral("BFDockAreaRefreshButton"))) return false;
    const QObject* right = object->parent();
    return right && right->objectName() == QStringLiteral("right_title") && right->parent()
        && right->parent()->inherits("DraggableTabWidget");
}

bool cloudContext(const QObject* object) {
    return object->inherits("BFFileMenu") || object->inherits("BFFolderMenu") || object->inherits("BFTreeFolderMenu");
}

bool ancestorClass(const QObject* object, const char* className) {
    for (const QObject* parent = object->parent(); parent; parent = parent->parent())
        if (parent->inherits(className)) return true;
    return false;
}

bool cloudPreview(const QObject* object) {
    return (object->inherits("PreviewWidget") || object->inherits("PreviewDetailsWidget"))
        && ancestorClass(object, "BfLocalResWidget");
}

void hideCloudShortcutRow(QTableWidget* table) {
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto* name = table->item(row, 0);
        if (!name || (name->text() != QStringLiteral("发送至资源云库")
            && name->text().compare(QStringLiteral("send to cloud"), Qt::CaseInsensitive) != 0)) continue;
        table->hideRow(row);
        for (int column = 0; column < table->columnCount(); ++column) {
            QWidget* widget = table->cellWidget(row, column);
            if (!widget || !widget->inherits("ShortcutItemLineEdit")) continue;
            widget->setEnabled(false);
            if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
                const QSignalBlocker blockEdit(edit);
                edit->clear();
            }
        }
    }
}

QString label(QString text) {
    text.remove(QLatin1Char('&'));
    text.remove(QLatin1Char(' '));
    text.remove(QLatin1Char('\t'));
    return text;
}

void disconnectSignal(QObject* object, const char* signature) {
    const int index = object->metaObject()->indexOfSignal(signature);
    if (index >= 0) QObject::disconnect(object, object->metaObject()->method(index), nullptr, QMetaMethod());
}

bool mainCorner(const QObject* object, bool left) {
    return object->parent() && object->parent()->inherits("DraggableTabWidget")
        && (left ? object->inherits("LeftMainTitleBar") : object->objectName() == QStringLiteral("right_title"));
}

void compactLeftCorner(QWidget* corner) {
    const bool eagle = corner->property("billfish.shell.eagleHeader").toBool();
    const int width = eagle ? std::max(44, corner->property("billfish.shell.headerWidth").toInt()) : 44;
    auto* contents = corner->findChild<QWidget*>(QStringLiteral("wid_main"), Qt::FindDirectChildrenOnly);
    auto* row = contents ? qobject_cast<QHBoxLayout*>(contents->layout()) : nullptr;
    auto* menu = contents ? contents->findChild<QAbstractButton*>(QStringLiteral("LeftMainTitleBar_btn_menu")) : nullptr;
    if (!row || !menu || !menu->parentWidget() || menu->parentWidget()->parentWidget() != contents
        || !corner->layout() || corner->layout()->count() != 1 || corner->layout()->itemAt(0)->widget() != contents) return;
    const QStringList brandNames {QStringLiteral("lab_logo"), QStringLiteral("lab_name"), QStringLiteral("lab_channel_name")};
    QList<QWidget*> brands;
    for (const QString& name : brandNames) {
        auto* brand = contents->findChild<QLabel*>(name, Qt::FindDirectChildrenOnly);
        if (!brand || row->indexOf(brand) < 0) return;
        brands.append(brand);
    }
    QWidget* menuHost = menu->parentWidget();
    for (int index = 0; index < row->count(); ++index) {
        QLayoutItem* item = row->itemAt(index);
        if (item->widget() && item->widget() != menuHost && !brands.contains(item->widget())
            && !(eagle && (item->widget()->objectName() == QStringLiteral("BillfishSidebarToggleButton")
                || item->widget()->objectName() == QStringLiteral("BillfishAddMenuButton")))) return;
        if (item->layout()) return;
    }
    for (QWidget* brand : brands) {
        brand->setEnabled(false);
        brand->hide();
    }
    for (int index = 0; index < row->count(); ++index) {
        const bool expanding = eagle && width > 44 && row->itemAt(index)->spacerItem();
        if (auto* spacer = row->itemAt(index)->spacerItem()) spacer->changeSize(0, 0, expanding ? QSizePolicy::Expanding : QSizePolicy::Fixed, QSizePolicy::Minimum);
        row->setStretch(index, expanding ? 1 : 0);
    }
    const int padding = eagle && width > 44 ? 12 : 9;
    row->setContentsMargins(padding, 0, padding, 0);
    row->setSpacing(0);
    menuHost->setFixedWidth(26);
    contents->setFixedWidth(width);
    if (eagle) { contents->setFixedHeight(48); corner->setFixedHeight(48); }
    corner->layout()->setContentsMargins(0, 0, 0, 0);
    corner->setFixedWidth(width);
    row->invalidate();
    corner->updateGeometry();
}

void compactRightCorner(QWidget* corner) {
    const bool eagle = corner->property("billfish.shell.eagleHeader").toBool();
    auto* row = qobject_cast<QHBoxLayout*>(corner->layout());
    if (!row) return;
    const QStringList localNames {QStringLiteral("BFDockAreaSideButton"), QStringLiteral("BFDockAreaMinimizeButton"),
        QStringLiteral("BFDockAreaMaximizeButton"), QStringLiteral("BFDockAreaCloseButton")};
    const QStringList removedNames {QStringLiteral("BFDockAreaAddButton"), QStringLiteral("BFDockAreaRefreshButton"),
        QStringLiteral("BFDockAreaHeadButton"), QStringLiteral("BFDockAreaTitleBarFrame")};
    for (const QString& name : localNames) {
        auto* button = corner->findChild<QAbstractButton*>(name, Qt::FindDirectChildrenOnly);
        if (!button || row->indexOf(button) < 0) return;
    }
    for (int index = 0; index < row->count(); ++index) {
        QLayoutItem* item = row->itemAt(index);
        if (item->layout()) return;
        QWidget* widget = item->widget();
        if (widget && !localNames.contains(widget->objectName()) && !removedNames.contains(widget->objectName())
            && !(eagle && widget->objectName() == QStringLiteral("BillfishPinButton"))) return;
    }
    for (int index = 0; index < row->count(); ++index) {
        QLayoutItem* item = row->itemAt(index);
        if (auto* widget = item->widget()) {
            if (removedNames.contains(widget->objectName())) {
                widget->setEnabled(false);
                widget->hide();
                if (widget->inherits("BFAvatarWidget")) disconnectSignal(widget, "signAvatarClick()");
            }
            if (eagle && widget->objectName() == QStringLiteral("BFDockAreaSideButton")) widget->hide();
        } else if (auto* spacer = item->spacerItem()) spacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        row->setStretch(index, 0);
    }
    row->setContentsMargins(eagle ? 8 : 0, 8, 8, 8);
    row->setSpacing(6);
    row->invalidate();
    const int preferred = eagle ? corner->sizeHint().expandedTo(corner->minimumSizeHint()).width()
        : row->sizeHint().expandedTo(row->minimumSize()).width();
    const int width = eagle ? std::max(preferred, corner->property("billfish.shell.headerWidth").toInt()) : preferred;
    if (eagle && row->count() && row->itemAt(0)->spacerItem()) {
        row->itemAt(0)->spacerItem()->changeSize(width - preferred, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        row->invalidate();
    }
    corner->setFixedWidth(width);
    if (eagle) corner->setFixedHeight(48);
    corner->updateGeometry();
}

QByteArrayList cloudSlots(const QObject* object) {
    if (object->inherits("BfLocalResWidget")) return {
        QByteArrayLiteral("SlotMoveFolderToCloud(QVector<qint64>)"),
        QByteArrayLiteral("SlotMoveFilesToCloud(QVector<qint64>)"),
        QByteArrayLiteral("SlotMoveToCloudWithUploadData(QSharedPointer<CloudUploadData>)"),
        QByteArrayLiteral("slotCloudImport(QSharedPointer<BFBrowserDragData>)"),
        QByteArrayLiteral("slotCloudImportByRightMenuClick(QSharedPointer<BFBrowserDragData>)"),
        QByteArrayLiteral("InitCloudMenu()")};
    if (object->inherits("BFTabManager")) return {
        QByteArrayLiteral("slotCloudUpload(QSharedPointer<CloudUploadData>)"),
        QByteArrayLiteral("slotCloudImport(QSharedPointer<BFBrowserDragData>)")};
    if (object->inherits("BFBaseItemView")) return {
        QByteArrayLiteral("SlotMoveFoldersToCloud()"), QByteArrayLiteral("SlotMoveFilesToCloud()"),
        QByteArrayLiteral("slotShortCutSendToCloud()")};
    if (cloudPreview(object)) return {QByteArrayLiteral("slotSendToCloud()")};
    if (cloudContext(object)) return {QByteArrayLiteral("SlotMoveToCloud()")};
    if (externalPage(object)) {
        QByteArrayList result {QByteArrayLiteral("slotLoadFinished(bool)"), QByteArrayLiteral("slotLoadStarted()")};
        result.append(object->inherits("BFWebWidget") ? QByteArrayLiteral("slotShowLogin()") : QByteArrayLiteral("slotSwitchToWeb()"));
        return result;
    }
    return {};
}
} // namespace

LegacyFeatureGate::LegacyFeatureGate(QObject* parent) : QObject(parent), timer_(new QTimer(this)) {
    setObjectName(QStringLiteral("BillfishLegacyFeatureGate"));
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &LegacyFeatureGate::flush);
}

void LegacyFeatureGate::start() {
    qApp->installEventFilter(this);
    enqueue(qApp);
    for (QWidget* widget : QApplication::allWidgets()) enqueue(widget);
}

void LegacyFeatureGate::enqueue(QObject* object) {
    if (!object || object == this || object == timer_) return;
    pending_.append(QPointer<QObject>(object));
    if (!timer_->isActive()) timer_->start(0);
}

void LegacyFeatureGate::enforce(QObject* root) {
    enqueue(root);
    flush();
}

void LegacyFeatureGate::remember(QObject* object) {
    if (objects_.contains(object)) return;
    objects_.insert(object, QPointer<QObject>(object));
    connect(object, &QObject::destroyed, this, [this, object] {
        objects_.remove(object);
        protectedSlots_.remove(object);
        libraryMenus_.remove(object);
        accountMenus_.remove(object);
    });
    const QByteArrayList businessSlots = cloudSlots(object);
    if (!businessSlots.isEmpty()) protectedSlots_.insert(object, businessSlots);
    if (auto* menu = qobject_cast<QMenu*>(object)) {
        connect(menu, &QMenu::aboutToShow, this, [this, menu] { enforce(menu); });
    }
    if (auto* table = qobject_cast<QTableWidget*>(object)) {
        if (ancestorClass(table, "PreferencesShortcut")) {
            connect(table->model(), &QAbstractItemModel::dataChanged, this, [this, table] { enqueue(table); });
            connect(table->model(), &QAbstractItemModel::rowsInserted, this, [this, table] { enqueue(table); });
            connect(table->model(), &QAbstractItemModel::modelReset, this, [this, table] { enqueue(table); });
        }
    }
}

void LegacyFeatureGate::flush() {
    timer_->stop();
    QList<QPointer<QObject>> queue;
    queue.swap(pending_);
    QSet<QObject*> inspected;
    QSet<QObject*> newSenders;
    QSet<QObject*> changedReceivers;
    for (int index = 0; index < queue.size(); ++index) {
        QObject* object = queue.at(index).data();
        if (!object || object == this || object == timer_ || inspected.contains(object)) continue;
        inspected.insert(object);
        if (!objects_.contains(object)) newSenders.insert(object);
        remember(object);
        if (protectedSlots_.contains(object)) changedReceivers.insert(object);
        for (QObject* child : object->children()) queue.append(QPointer<QObject>(child));
        // Native menus also contain parentless QActions. QObject children alone
        // do not enumerate their commands or the linked library submenu.
        if (auto* widget = qobject_cast<QWidget*>(object)) {
            for (QAction* action : widget->actions()) {
                queue.append(QPointer<QObject>(action));
                if (action->menu()) queue.append(QPointer<QObject>(action->menu()));
            }
        }
        inspect(object);
    }
    for (QObject* receiver : changedReceivers) {
        const QByteArrayList businessSlots = protectedSlots_.value(receiver);
        for (auto sender = objects_.cbegin(); sender != objects_.cend(); ++sender)
            if (sender.value()) disconnectIncoming(sender.key(), receiver, businessSlots);
    }
    for (QObject* sender : newSenders) {
        for (auto receiver = protectedSlots_.cbegin(); receiver != protectedSlots_.cend(); ++receiver)
            if (!changedReceivers.contains(receiver.key())) disconnectIncoming(sender, receiver.key(), receiver.value());
    }
}

void LegacyFeatureGate::disconnectIncoming(QObject* sender, QObject* receiver, const QByteArrayList& businessSlots) {
    if (externalPage(receiver)) {
        // Qt5's named-slot disconnect cannot match PMF/lambda slot objects.
        // The retired pages have no remaining business input. Disconnect each
        // sender signal with a wildcard receiver method, retaining destroyed.
        const QMetaObject* meta = sender->metaObject();
        for (int index = 0; index < meta->methodCount(); ++index) {
            const QMetaMethod signal = meta->method(index);
            if (signal.methodType() == QMetaMethod::Signal && signal.name() != QByteArrayLiteral("destroyed"))
                QObject::disconnect(sender, signal, receiver, QMetaMethod());
        }
        return;
    }
    // Only named cloud business slots are removed. In particular this leaves
    // destroyed(QObject*) -> native cleanup connections intact in both directions.
    for (const QByteArray& slot : businessSlots) {
        if (receiver->metaObject()->indexOfSlot(slot.constData()) < 0) continue;
        const QByteArray encoded = QByteArrayLiteral("1") + slot;
        QObject::disconnect(sender, nullptr, receiver, encoded.constData());
    }
}

void LegacyFeatureGate::blockAction(QAction* action) {
    action->setProperty("billfish.shell.legacyBlocked", true);
    action->setEnabled(false);
    action->setVisible(false);
    action->setShortcuts(QList<QKeySequence>());
    disconnectSignal(action, "triggered(bool)");
    disconnectSignal(action, "toggled(bool)");
}

void LegacyFeatureGate::moveImportHistory() {
    if (!mainMenu_ || !importHistory_) return;
    if (!mainMenu_->actions().contains(importHistory_)) {
        const auto actions = mainMenu_->actions();
        QAction* before = nullptr;
        for (int index = 0; index < actions.size(); ++index) {
            const QString text = label(actions.at(index)->text());
            if (text.startsWith(QStringLiteral("打开资源库")) || text.compare(QStringLiteral("OpenLibrary"), Qt::CaseInsensitive) == 0) {
                if (index + 1 < actions.size()) before = actions.at(index + 1);
                break;
            }
        }
        mainMenu_->insertAction(before, importHistory_);
    }
    if (historyMenu_ && historyMenu_ != mainMenu_ && historyMenu_->actions().contains(importHistory_))
        historyMenu_->removeAction(importHistory_);
}

void LegacyFeatureGate::inspectMenu(QMenu* menu) {
    const QList<QAction*> actions = menu->actions();
    bool createLibrary = false;
    bool openLibrary = false;
    bool exit = false;
    if (menu->inherits("BFStyleMenu")) {
        for (QAction* action : actions) {
            const QString text = label(action->text());
            createLibrary |= text.startsWith(QStringLiteral("创建资源库"));
            openLibrary |= text.startsWith(QStringLiteral("打开资源库"))
                || text.compare(QStringLiteral("OpenLibrary"), Qt::CaseInsensitive) == 0;
            exit |= text.startsWith(QStringLiteral("退出")) || text.compare(QStringLiteral("Exit"), Qt::CaseInsensitive) == 0;
        }
    }
    if (createLibrary && openLibrary && exit) {
        mainMenu_ = menu;
        for (QAction* action : actions) {
            const QString text = label(action->text());
            if ((text.startsWith(QStringLiteral("打开资源库")) || text.compare(QStringLiteral("OpenLibrary"), Qt::CaseInsensitive) == 0)
                && action->menu()) {
                libraryMenus_.insert(action->menu());
                inspectMenu(action->menu());
            }
        }
    }
    // The native avatar menu owns no QAction objects itself. Keep the existing
    // history action's QObject owner, callbacks and Alt+L shortcut unchanged.
    if (menu->inherits("BFStyleMenu") && menu != mainMenu_) {
        bool login = false;
        QAction* history = nullptr;
        for (QAction* action : actions) {
            const QString text = label(action->text());
            login |= text == QStringLiteral("登录账号") || text.compare(QStringLiteral("loginaccount"), Qt::CaseInsensitive) == 0;
            if (text == QStringLiteral("导入历史") && action->shortcuts().contains(QKeySequence(Qt::ALT | Qt::Key_L))) history = action;
        }
        if (login && history) {
            accountMenus_.insert(menu);
            historyMenu_ = menu;
            importHistory_ = history;
        }
    }
    const bool dockMenu = menu->inherits("BFStyleMenu")
        && menu->objectName() == QStringLiteral("BFDockAreaRefreshMenu") && menu->parent()
        && cloudTitleButton(menu->parent()) && menu->parent()->objectName() == QStringLiteral("BFDockAreaAddButton");
    for (QAction* action : actions) {
        const QString text = label(action->text());
        if (((menu == mainMenu_ || accountMenus_.contains(menu))
                && (text == QStringLiteral("登录账号") || text.compare(QStringLiteral("loginaccount"), Qt::CaseInsensitive) == 0))
            || (cloudContext(menu) && (text.startsWith(QStringLiteral("发送至资源云库"))
                || text.startsWith(QStringLiteral("sendtocloud"), Qt::CaseInsensitive)))
            || (libraryMenus_.contains(menu) && !actions.isEmpty() && action == actions.first()
                && (text == QStringLiteral("资源云库")
                || text.compare(QStringLiteral("OpenCloudLibrary"), Qt::CaseInsensitive) == 0))
            || (dockMenu && ((action->objectName() == QStringLiteral("BFDockAreaRefreshAction") && text == QStringLiteral("酷雀工具箱"))
                || (action->objectName() == QStringLiteral("BFDockAreaForceRefreshAction") && text == QStringLiteral("创意灵感"))))) {
            blockAction(action);
        }
    }
    moveImportHistory();
}

void LegacyFeatureGate::inspect(QObject* object) {
    if (auto* widget = qobject_cast<QWidget*>(object)) {
        if ((widget->objectName() == QStringLiteral("lab_logo") || widget->objectName() == QStringLiteral("lab_name")
                || widget->objectName() == QStringLiteral("lab_channel_name"))
            && widget->parent() && widget->parent()->objectName() == QStringLiteral("wid_main")
            && widget->parent()->parent() && mainCorner(widget->parent()->parent(), true)) {
            widget->setEnabled(false);
            widget->hide();
        }
        if (mainCorner(widget, true)) compactLeftCorner(widget);
        else if (mainCorner(widget, false)) compactRightCorner(widget);
        if (widget->inherits("BFAvatarWidget") && widget->objectName() == QStringLiteral("BFDockAreaHeadButton")
            && widget->parent() && mainCorner(widget->parent(), false)) {
            widget->setEnabled(false);
            widget->hide();
            disconnectSignal(widget, "signAvatarClick()");
        }
        const bool trayLogin = widget->objectName() == QStringLiteral("SystemTray_m_login")
            && ancestorClass(widget, "BFSystemTrayNoLoginWidget");
        const bool cloudLogin = widget->objectName() == QStringLiteral("login_button")
            && ancestorClass(widget, "BFWebNoLoginWidget");
        if (trayLogin || cloudLogin) {
            widget->setEnabled(false);
            widget->hide();
            disconnectSignal(widget, "clicked(bool)");
        }
        if (widget->inherits("BFSystemTrayNoLoginWidget")) disconnectSignal(widget, "SigLogin()");
    }
    if (cloudPreview(object)) {
        if (auto* widget = qobject_cast<QWidget*>(object)) {
            for (QAction* action : widget->actions()) {
                const QString text = label(action->text());
                if (text.startsWith(QStringLiteral("发送至资源云库")) || text.startsWith(QStringLiteral("sendtocloud"), Qt::CaseInsensitive))
                    blockAction(action);
            }
        }
        disconnectSignal(object, "sigMoveFilesToCloud(QVector<qint64>)");
    }
    if (auto* table = qobject_cast<QTableWidget*>(object)) {
        if (ancestorClass(table, "PreferencesShortcut")) hideCloudShortcutRow(table);
    }
    if (ancestorClass(object, "PreferencesNotification")
        && (object->objectName() == QStringLiteral("label_cloud_notification")
            || object->objectName() == QStringLiteral("btn_show_cloud_completed_notification")
            || object->objectName() == QStringLiteral("btn_not_show_cloud_completed_notification"))) {
        if (auto* widget = qobject_cast<QWidget*>(object)) {
            widget->setEnabled(false);
            widget->hide();
        }
    }
    if (externalPage(object)) {
        if (auto* widget = qobject_cast<QWidget*>(object)) {
            widget->setEnabled(false);
            widget->hide();
        }
    }
    if (cloudTitleButton(object)) {
        auto* button = qobject_cast<QAbstractButton*>(object);
        button->setEnabled(false);
        button->hide();
        disconnectSignal(button, "clicked(bool)");
    }
    if (auto* menu = qobject_cast<QMenu*>(object)) inspectMenu(menu);
    if (object->objectName() == QStringLiteral("lab_cloud_task_gif") && ancestorClass(object, "BFAvatarWidget")) {
        if (auto* widget = qobject_cast<QWidget*>(object)) {
            widget->setEnabled(false);
            widget->hide();
        }
    }
    if (auto* action = qobject_cast<QAction*>(object)) {
        if (action->property("billfish.shell.legacyBlocked").toBool()) blockAction(action);
        else if (auto* menu = qobject_cast<QMenu*>(action->parent())) inspectMenu(menu);
    }
    if (auto* shortcut = qobject_cast<QShortcut*>(object)) {
        const QObject* owner = shortcut->parent();
        const bool treeMnemonic = owner && owner->inherits("BFTreeFolderMenu") && shortcut->key() == QKeySequence(Qt::Key_U);
        const bool viewShortcut = owner && owner->inherits("BFBaseItemView")
            && shortcut->key() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U);
        if (treeMnemonic || viewShortcut || shortcut->property("billfish.shell.legacyBlocked").toBool()) {
            shortcut->setProperty("billfish.shell.legacyBlocked", true);
            shortcut->setEnabled(false);
            shortcut->setKey(QKeySequence());
            disconnectSignal(shortcut, "activated()");
            disconnectSignal(shortcut, "activatedAmbiguously()");
        }
    }
    if (cloudContext(object)) {
        disconnectSignal(object, "SigMoveToCloud()");
        if (object->inherits("BFTreeFolderMenu")) disconnectSignal(object, "SigTransferFolderToCloud()");
    }
    if (object->inherits("IBFBaseItemView") || object->inherits("BFItemIconView") || object->inherits("BFStackedView")) {
        disconnectSignal(object, "sigMoveFilesToCloud(QVector<qint64>)");
        disconnectSignal(object, "sigMoveFoldersToCloud(QVector<qint64>)");
        disconnectSignal(object, "signalCloudImport(QSharedPointer<BFBrowserDragData>)");
    }
    if (object->inherits("BFItemIconView")) {
        disconnectSignal(object, "signalTransferFolderToCloud(QVector<qint64>)");
        disconnectSignal(object, "SigMoveFolderToCloud(QVector<qint64>)");
    }
    if (externalPage(object)) {
        disconnectSignal(object, "signalCloudUpload(QSharedPointer<CloudUploadData>)");
        disconnectSignal(object, "signalCloudImport(QSharedPointer<BFBrowserDragData>)");
    } else if (withinExternalPage(object)) {
        if (object->inherits("BFNormalWebErrorWidget")) disconnectSignal(object, "signalRefresh()");
        if (object->inherits("BFWebNoLoginWidget")) disconnectSignal(object, "signalLogin()");
    }
}

bool LegacyFeatureGate::eventFilter(QObject* watched, QEvent* event) {
    if (automation::AppInputScope::active() && mainMenu_
        && watched->objectName() == QStringLiteral("LeftMainTitleBar_btn_menu")
        && ancestorClass(watched, "LeftMainTitleBar") && ancestorClass(watched, "DraggableTabWidget")) {
        auto* button = qobject_cast<QAbstractButton*>(watched);
        if (button && button->isEnabled() && event->type() == QEvent::MouseButtonPress
            && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            pressedMainMenuButton_ = button;
            button->setDown(true);
            return true;
        }
        if (button && event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && pressedMainMenuButton_ == button) {
            pressedMainMenuButton_.clear();
            button->setDown(false);
            if (button->rect().contains(static_cast<QMouseEvent*>(event)->pos()))
                automation::showScopedPopup(mainMenu_, button->mapToGlobal(QPoint(0, button->height())));
            return true;
        }
    }
    switch (event->type()) {
    case QEvent::ChildAdded:
    case QEvent::ChildPolished:
        enqueue(static_cast<QChildEvent*>(event)->child());
        break;
    case QEvent::ActionAdded:
    case QEvent::ActionChanged:
        if (auto* menu = qobject_cast<QMenu*>(watched)) inspectMenu(menu);
        else if (cloudPreview(watched)) inspect(watched);
        enqueue(watched);
        break;
    case QEvent::Show:
        inspect(watched);
        if (auto* menu = qobject_cast<QMenu*>(watched)) enforce(menu);
        else enqueue(watched);
        break;
    case QEvent::Resize:
    case QEvent::LayoutRequest:
        if (mainCorner(watched, true) || mainCorner(watched, false)) inspect(watched);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void installLegacyFeatureGate() {
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;
    const auto install = [app] {
        if (app->findChild<LegacyFeatureGate*>(QStringLiteral("BillfishLegacyFeatureGate"), Qt::FindDirectChildrenOnly)) return;
        (new LegacyFeatureGate(app))->start();
    };
    if (QThread::currentThread() == app->thread()) install();
    else QMetaObject::invokeMethod(app, install, Qt::QueuedConnection);
}
} // namespace billfish::shell
