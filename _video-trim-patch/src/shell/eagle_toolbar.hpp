#pragma once

#include <QObject>
#include <QPointer>
#include <QMargins>
#include <QPoint>
#include <QSet>

#include <optional>

class QAction;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLayoutItem;
class QMenu;
class QSplitter;
class QSpacerItem;
class QTabWidget;
class QTimer;
class QToolButton;
class QWidget;

namespace billfish::shell {

class EagleToolbar final : public QObject {
    Q_OBJECT
public:
    using MenuRefresher = bool (*)(QMenu*);
    explicit EagleToolbar(QWidget* mainWindow, QTabWidget* tabs, QWidget* navigation,
                          MenuRefresher menuRefresher = nullptr, QObject* parent = nullptr);
    ~EagleToolbar() override;
    [[nodiscard]] bool attach();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void scheduleRefresh();
    void refresh();
    void bindActions();
    void positionControls();
    void toggleSidebars();
    void showSidebarMenu(const QPoint& position);
    void showLayoutPanel();
    void buildLayoutPanel();
    void resetCurrentView();

    QPointer<QWidget> mainWindow_;
    QPointer<QTabWidget> tabs_;
    QPointer<QWidget> navigation_;
    QPointer<QWidget> contents_;
    QPointer<QWidget> leftCorner_;
    QPointer<QWidget> rightCorner_;
    QPointer<QWidget> leftPane_;
    QPointer<QWidget> rightPane_;
    QPointer<QWidget> bodyStack_;
    QMargins bodyMargins_;
    QPointer<QSplitter> splitter_;
    QPointer<QHBoxLayout> row_;
    QPointer<QLabel> title_;
    QPointer<QWidget> navigationStack_;
    QPointer<QWidget> search_;
    QPointer<QWidget> searchField_;
    QPointer<QWidget> conditionFilter_;
    QPointer<QWidget> normalPage_;
    std::optional<bool> filterVisiblePreference_;
    QPointer<QWidget> zoom_;
    QPointer<QWidget> nativeButtons_;
    QPointer<QToolButton> sidebarButton_;
    QPointer<QToolButton> addButton_;
    QPointer<QToolButton> pluginButton_;
    QPointer<QToolButton> layoutButton_;
    QPointer<QToolButton> filterButton_;
    QPointer<QToolButton> pinButton_;
    QPointer<QAction> pluginAction_;
    QPointer<QAction> pinAction_;
    QPointer<QAction> leftAction_;
    QPointer<QAction> rightAction_;
    QPointer<QAction> filterAction_;
    QPointer<QMenu> layoutMenu_;
    QPointer<QMenu> sortMenu_;
    QPointer<QMenu> infoMenu_;
    QPointer<QMenu> importMenu_;
    QPointer<QFrame> panel_;
    QTimer* refreshTimer_;
    MenuRefresher refreshMenu_;
    QSpacerItem* rightStretch_ = nullptr;
    QSet<QAction*> observedLayouts_;
    QList<QLayoutItem*> dragItems_;
    QList<QPointer<QWidget>> dragRegions_;
    QPointer<QWidget> leftDragRegion_;
    QPointer<QWidget> rightDragRegion_;
    QPoint captionPress_;
    bool captionPressed_ = false;
};

void installEagleToolbar(QWidget* mainWindow, QTabWidget* tabs, QWidget* navigation);

} // namespace billfish::shell
