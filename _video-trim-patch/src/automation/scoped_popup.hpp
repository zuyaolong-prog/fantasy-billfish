#pragma once

#include "automation/app_input_scope.hpp"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QSet>
#include <QVector>

namespace billfish::automation {
namespace detail {

class ScopedPopupSession final : public QObject {
public:
    explicit ScopedPopupSession(QMenu* root) : QObject(root), root_(root)
    {
        prepareMenus();
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!active_ || !root_) return false;
        if ((event->type() == QEvent::ActionAdded || event->type() == QEvent::ActionChanged)
            && knownMenu(qobject_cast<QMenu*>(watched))) prepareMenus();
        if (watched == root_ && event->type() == QEvent::Hide) {
            finish();
            return false;
        }
        if (!root_->isVisible()) return false;
        auto* widget = qobject_cast<QWidget*>(watched);
        if (event->type() == QEvent::MouseButtonPress && widget && !belongsToMenuTree(widget)) {
            root_->close();
            return false;
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            root_->close();
            return true;
        }
        auto* menu = qobject_cast<QMenu*>(widget);
        if (!menu || !knownMenu(menu)) return false;
        if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            pressedMenu_ = menu;
            pressedAction_ = menu->actionAt(static_cast<QMouseEvent*>(event)->pos());
            if (pressedAction_) {
                menu->setActiveAction(pressedAction_);
                if (pressedAction_->isEnabled() && pressedAction_->menu()) openSubmenu(menu, pressedAction_);
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            const QPointer<QAction> action = menu->actionAt(static_cast<QMouseEvent*>(event)->pos());
            const bool activate = pressedMenu_ == menu && pressedAction_ == action && action && action->isEnabled()
                && !action->isSeparator() && !action->menu();
            pressedMenu_.clear();
            pressedAction_.clear();
            if (activate) {
                root_->close();
                action->trigger();
            }
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            const QPointer<QAction> action = menu->activeAction();
            if (action && action->isEnabled() && !action->isSeparator()) {
                if (action->menu() && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Right)) {
                    openSubmenu(menu, action);
                    return true;
                }
                if (!action->menu() && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
                    root_->close();
                    action->trigger();
                    return true;
                }
            }
        }
        return false;
    }

private:
    struct Original {
        QPointer<QMenu> menu;
        Qt::WindowFlags flags;
        bool showWithoutActivating;
    };

    bool knownMenu(const QMenu* menu) const
    {
        if (!menu) return false;
        for (const auto& original : originals_) if (original.menu == menu) return true;
        return false;
    }

    bool belongsToMenuTree(const QWidget* widget) const
    {
        for (auto* ancestor = widget; ancestor; ancestor = ancestor->parentWidget())
            if (knownMenu(qobject_cast<const QMenu*>(ancestor))) return true;
        return false;
    }

    void prepareMenus()
    {
        QSet<QMenu*> visited;
        prepareMenu(root_, visited);
    }

    void prepareMenu(QMenu* menu, QSet<QMenu*>& visited)
    {
        if (!menu || visited.contains(menu)) return;
        visited.insert(menu);
        if (!knownMenu(menu)) {
            originals_.append({menu, menu->windowFlags(), menu->testAttribute(Qt::WA_ShowWithoutActivating)});
            menu->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
            menu->setAttribute(Qt::WA_ShowWithoutActivating);
            // Native aboutToShow handlers populate recent-library actions.
            // Capture their new submenus before QMenu shows its native window.
            connect(menu, &QMenu::aboutToShow, this, [this] { if (active_) prepareMenus(); });
        }
        for (auto* action : menu->actions()) prepareMenu(action->menu(), visited);
    }

    void openSubmenu(QMenu* menu, QAction* action)
    {
        auto* submenu = action->menu();
        for (auto* candidate : menu->actions())
            if (candidate->menu() && candidate->menu() != submenu) candidate->menu()->hide();
        if (!submenu->isVisible())
            submenu->popup(menu->mapToGlobal(menu->actionGeometry(action).topRight() + QPoint(1, 0)));
    }

    void finish()
    {
        active_ = false;
        // Hide happens after the native window has been hidden. Restore the
        // original flags so reusable menus keep their normal mouse behavior.
        for (const auto& original : originals_) {
            if (!original.menu) continue;
            if (original.menu != root_) original.menu->hide();
            original.menu->setWindowFlags(original.flags);
            original.menu->setAttribute(Qt::WA_ShowWithoutActivating, original.showWithoutActivating);
        }
        deleteLater();
    }

    QPointer<QMenu> root_;
    QPointer<QMenu> pressedMenu_;
    QPointer<QAction> pressedAction_;
    QVector<Original> originals_;
    bool active_ = true;
};

} // namespace detail

inline void showScopedPopup(QMenu* menu, const QPoint& position)
{
    if (!menu) return;
    if (menu->isVisible()) menu->close();
    if (AppInputScope::active()) new detail::ScopedPopupSession(menu);
    menu->popup(position);
}

} // namespace billfish::automation
