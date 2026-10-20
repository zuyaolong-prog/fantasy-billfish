#pragma once

#include <QObject>
#include <QPointer>

class QTabWidget;
class QTimer;
class QWidget;

namespace billfish::shell {

// Keeps the installed native title bar and local resource page alive. Only the
// obsolete navigation tabs and the redundant navigation-bar row are removed.
class CompactShell final : public QObject {
    Q_OBJECT
public:
    using TitleBarSetter = bool (*)(QWidget*, QWidget*);
    explicit CompactShell(QObject* parent = nullptr, TitleBarSetter titleBarSetter = nullptr);
    void start();
    [[nodiscard]] bool tryApply(QWidget* mainWindow);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void scheduleRefresh();
    void refresh();
    void positionNavigation();

    QTimer* refreshTimer_;
    TitleBarSetter titleBarSetter_;
    QPointer<QWidget> mainWindow_;
    QPointer<QTabWidget> tabs_;
    QPointer<QWidget> navigation_;
    QPointer<QWidget> sourceParent_;
    QPointer<QWidget> leftCorner_;
    QPointer<QWidget> rightCorner_;
};

void installCompactShell();

} // namespace billfish::shell
