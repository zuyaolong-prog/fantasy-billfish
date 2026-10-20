#include "shell/compact_shell.hpp"
#include "shell/eagle_toolbar.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QCryptographicHash>
#include <QEvent>
#include <QFile>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cstring>
#include <Windows.h>

namespace billfish::shell {
namespace {
constexpr auto appliedProperty = "billfish.shell.compact";

bool setNativeTitleBar(QWidget* mainWindow, QWidget* titleBar) {
    using Setter = void (*)(QWidget*, QWidget*);
    static const Setter setter = []() -> Setter {
        HMODULE core = GetModuleHandleW(L"Billfish.core.exe");
        if (!core) return nullptr;
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(core, path, 32768);
        if (!length || length >= 32768) return nullptr;
        QFile file(QString::fromWCharArray(path, static_cast<int>(length)));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)
            || hash.result().toHex() != QByteArrayLiteral("47abf9bf0464d88c1ca259965bbdd00165317fbf1fea8308c339c548c405c24f")) return nullptr;
        // Frozen CFramelessWindow::setTitleBar(QWidget*). This is the native
        // setter, including its QObject lifetime connection, not a field write.
        const auto* address = reinterpret_cast<const unsigned char*>(core) + 0xb5f40;
        const QByteArray prologue = QByteArray::fromHex(QByteArrayLiteral("4883ec38488951304885d27434488d050cf80700c7442428"));
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT
            || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            || reinterpret_cast<quintptr>(address) + static_cast<quintptr>(prologue.size())
                > reinterpret_cast<quintptr>(region.BaseAddress) + region.RegionSize
            || std::memcmp(address, prologue.constData(), static_cast<size_t>(prologue.size())) != 0) return nullptr;
        return reinterpret_cast<Setter>(const_cast<unsigned char*>(address));
    }();
    if (!setter) return false;
    setter(mainWindow, titleBar);
    return true;
}

QWidget* uniqueDescendant(QWidget* parent, const char* className) {
    QWidget* result = nullptr;
    for (QWidget* child : parent->findChildren<QWidget*>()) {
        if (!child->inherits(className)) continue;
        if (result) return nullptr;
        result = child;
    }
    return result;
}
} // namespace

CompactShell::CompactShell(QObject* parent, TitleBarSetter titleBarSetter)
    : QObject(parent), refreshTimer_(new QTimer(this)),
      titleBarSetter_(titleBarSetter ? titleBarSetter : &setNativeTitleBar) {
    setObjectName(QStringLiteral("BillfishCompactShell"));
    refreshTimer_->setSingleShot(true);
    connect(refreshTimer_, &QTimer::timeout, this, &CompactShell::refresh);
}

void CompactShell::start() {
    qApp->installEventFilter(this);
    scheduleRefresh();
}

bool CompactShell::tryApply(QWidget* mainWindow) {
    if (!mainWindow || !mainWindow->inherits("CFramelessWindow")
        || mainWindow->objectName() != QStringLiteral("mainWindow")) return false;
    if (mainWindow->property(appliedProperty).toBool() && mainWindow_ == mainWindow && navigation_) {
        positionNavigation();
        return true;
    }

    auto* tabs = qobject_cast<QTabWidget*>(uniqueDescendant(mainWindow, "DraggableTabWidget"));
    if (!tabs || tabs->parentWidget() != mainWindow) return false;
    QWidget* localPage = uniqueDescendant(tabs, "BfLocalResWidget");
    QWidget* navigation = localPage ? uniqueDescendant(localPage, "ToolModule::BFNavigationBar") : nullptr;
    QWidget* left = tabs->cornerWidget(Qt::TopLeftCorner);
    QWidget* right = tabs->cornerWidget(Qt::TopRightCorner);
    auto* sourceLayout = navigation && navigation->parentWidget()
        ? qobject_cast<QVBoxLayout*>(navigation->parentWidget()->layout()) : nullptr;
    const int localIndex = localPage ? tabs->indexOf(localPage) : -1;
    // Validate the complete frozen shell before changing any parent or layout.
    // Vendor immersive-mode callbacks retain the page's two-level parent chain
    // and dereference both original corner widgets without null checks.
    if (localIndex < 0 || !navigation || !sourceLayout || sourceLayout->indexOf(navigation) != 0
        || !left || !left->inherits("LeftMainTitleBar") || !right
        || right->objectName() != QStringLiteral("right_title")
        || !tabs->tabBar() || !tabs->tabBar()->inherits("DraggableTabBar")
        || left->height() <= 0 || right->height() != left->height()) return false;

    if (!mainWindow->property(appliedProperty).toBool() && !titleBarSetter_(mainWindow, tabs)) return false;

    mainWindow_ = mainWindow;
    tabs_ = tabs;
    navigation_ = navigation;
    sourceParent_ = navigation->parentWidget();
    leftCorner_ = left;
    rightCorner_ = right;
    // Reparenting the bar must not extend its lifetime beyond the native view
    // that created it. Rebind when a library switch constructs a new view.
    const QPointer<QWidget> nativeNavigation(navigation);
    connect(sourceParent_, &QObject::destroyed, this, [this, nativeNavigation] {
        if (nativeNavigation) nativeNavigation->deleteLater();
        if (navigation_ == nativeNavigation) navigation_.clear();
        scheduleRefresh();
    });
    connect(navigation, &QObject::destroyed, this, [this] { scheduleRefresh(); });

    tabs->setCurrentWidget(localPage);
    {
        const QSignalBlocker blockTabs(tabs);
        for (int index = tabs->count() - 1; index >= 0; --index) {
            if (tabs->widget(index) != localPage) tabs->removeTab(index);
        }
    }
    // QTabWidget drops its corner/title row when the bar is hidden. Keep a
    // zero-width bar in the native 42-pixel row so media cannot sit underneath
    // the relocated controls. It has no painted or clickable tab area.
    tabs->tabBar()->setFixedWidth(0);
    tabs->tabBar()->setFixedHeight(left->height());
    tabs->tabBar()->show();
    sourceLayout->removeWidget(navigation);
    navigation->setParent(tabs);
    navigation->show();
    mainWindow->setProperty(appliedProperty, true);
    tabs->installEventFilter(this);
    left->installEventFilter(this);
    right->installEventFilter(this);
    tabs->tabBar()->installEventFilter(this);
    sourceLayout->invalidate();
    if (mainWindow->layout()) mainWindow->layout()->activate();
    positionNavigation();
    installEagleToolbar(mainWindow, tabs, navigation);
    scheduleRefresh();
    return true;
}

void CompactShell::positionNavigation() {
    if (!tabs_ || !navigation_ || !leftCorner_ || !rightCorner_) return;
    // The native corners continue to reserve the original 42-pixel title row.
    // Geometry comes from their live layout, including DPI and window changes.
    const int left = leftCorner_->geometry().right() + 1;
    const int right = rightCorner_->geometry().left();
    const int height = std::max(leftCorner_->height(), rightCorner_->height());
    const int minimum = std::max(navigation_->minimumWidth(), navigation_->minimumSizeHint().width());
    const int required = leftCorner_->width() + rightCorner_->width() + minimum;
    if (mainWindow_ && required > tabs_->width()) {
        const int frame = mainWindow_->width() - tabs_->width();
        mainWindow_->setMinimumWidth(std::max(mainWindow_->minimumWidth(), required + frame));
    }
    navigation_->setGeometry(left, 0, std::max(0, right - left), height);
    navigation_->setVisible(leftCorner_->isVisible() && rightCorner_->isVisible());
    navigation_->raise();
}

void CompactShell::scheduleRefresh() {
    if (!refreshTimer_->isActive()) refreshTimer_->start(0);
}

void CompactShell::refresh() {
    if (mainWindow_ && tabs_ && navigation_) {
        positionNavigation();
        return;
    }
    for (QWidget* window : QApplication::topLevelWidgets()) {
        if (tryApply(window)) break;
    }
}

bool CompactShell::eventFilter(QObject* watched, QEvent* event) {
    const auto type = event->type();
    if (watched == tabs_ || watched == leftCorner_ || watched == rightCorner_) {
        if (type == QEvent::Resize || type == QEvent::Move || type == QEvent::Show
            || type == QEvent::Hide || type == QEvent::LayoutRequest) scheduleRefresh();
    } else if (type == QEvent::Show || type == QEvent::ChildAdded || type == QEvent::ChildRemoved) {
        // Construction and library changes are event driven; no periodic scan.
        if (!navigation_) scheduleRefresh();
    }
    return QObject::eventFilter(watched, event);
}

void installCompactShell() {
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;
    const auto install = [app] {
        if (app->findChild<CompactShell*>(QStringLiteral("BillfishCompactShell"), Qt::FindDirectChildrenOnly)) return;
        auto* shell = new CompactShell(app);
        shell->start();
    };
    if (QThread::currentThread() == app->thread()) install();
    else QMetaObject::invokeMethod(app, install, Qt::QueuedConnection);
}
} // namespace billfish::shell
