#include "shell/compact_shell.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QVBoxLayout>

// Match the observed native inheritance and layout boundaries, without loading
// Billfish or touching the user's resource database.
class CFramelessWindow : public QWidget { Q_OBJECT };
class DraggableTabBar : public QTabBar {
    Q_OBJECT
public:
    QSize sizeHint() const override { return QSize(QTabBar::sizeHint().width(), 42); }
    QSize minimumSizeHint() const override { return QSize(QTabBar::minimumSizeHint().width(), 42); }
};
class DraggableTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit DraggableTabWidget(QWidget* parent) : QTabWidget(parent) { setTabBar(new DraggableTabBar); }
};
class BfLocalResWidget : public QWidget { Q_OBJECT };
class LeftMainTitleBar : public QWidget { Q_OBJECT };
namespace ToolModule { class BFNavigationBar : public QWidget { Q_OBJECT }; }

struct NativeShell {
    CFramelessWindow window;
    DraggableTabWidget* tabs = new DraggableTabWidget(&window);
    BfLocalResWidget* local = new BfLocalResWidget;
    LeftMainTitleBar* left = new LeftMainTitleBar;
    QWidget* right = new QWidget;
    QWidget* center = new QWidget(local);
    ToolModule::BFNavigationBar* navigation = new ToolModule::BFNavigationBar;
    QWidget* media = new QWidget;
    QLineEdit* search = new QLineEdit;
    QPushButton* maximize = new QPushButton(QStringLiteral("Max"));

    NativeShell() {
        window.setObjectName(QStringLiteral("mainWindow"));
        window.resize(1160, 700);
        auto* rootLayout = new QVBoxLayout(&window);
        rootLayout->setContentsMargins(1, 1, 1, 1);
        rootLayout->setSpacing(0);
        rootLayout->addWidget(tabs);
        tabs->setStyleSheet(QStringLiteral("QTabWidget::pane { border: 0px; }"));
        tabs->tabBar()->setFixedHeight(42);
        left->setFixedSize(201, 42);
        right->setObjectName(QStringLiteral("right_title"));
        right->setFixedSize(226, 42);
        auto* buttons = new QHBoxLayout(right);
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->addWidget(maximize);
        tabs->setCornerWidget(left, Qt::TopLeftCorner);
        tabs->setCornerWidget(right, Qt::TopRightCorner);
        auto* pageLayout = new QHBoxLayout(local);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);
        auto* sidebar = new QWidget;
        sidebar->setFixedWidth(208);
        pageLayout->addWidget(sidebar);
        pageLayout->addWidget(center, 1);
        auto* details = new QWidget;
        details->setFixedWidth(203);
        pageLayout->addWidget(details);
        auto* content = new QVBoxLayout(center);
        content->setContentsMargins(0, 0, 0, 0);
        content->setSpacing(0);
        navigation->setFixedHeight(42);
        auto* controls = new QHBoxLayout(navigation);
        controls->setContentsMargins(0, 0, 0, 0);
        controls->addWidget(search);
        controls->addWidget(new QPushButton(QStringLiteral("Filter")));
        content->addWidget(navigation);
        content->addWidget(media, 1);
        tabs->addTab(local, QStringLiteral("素材库"));
        for (const QString& title : {QStringLiteral("资源云库"), QStringLiteral("酷雀工具箱"), QStringLiteral("创意灵感")})
            tabs->addTab(new QWidget, title);
        window.show();
        QTest::qWait(1);
    }
};

class CompactShellTests final : public QObject {
    Q_OBJECT
private slots:
    void releasesNavigationRowAndPreservesNativeControls() {
        NativeShell fixture;
        const int before = fixture.media->height();
        const int originalTop = fixture.media->mapTo(&fixture.window, QPoint()).y();
        QObject* originalParent = fixture.local->parent();
        billfish::shell::CompactShell shell(nullptr, [](QWidget*, QWidget*) { return true; });
        QVERIFY(shell.tryApply(&fixture.window));
        QTRY_COMPARE(fixture.media->height(), before + 42);
        QCOMPARE(fixture.media->mapTo(&fixture.window, QPoint()).y(), originalTop - 42);
        QCOMPARE(fixture.tabs->count(), 1);
        QCOMPARE(fixture.tabs->currentWidget(), fixture.local);
        QCOMPARE(fixture.local->parent(), originalParent);
        QCOMPARE(fixture.tabs->cornerWidget(Qt::TopLeftCorner), fixture.left);
        QCOMPARE(fixture.tabs->cornerWidget(Qt::TopRightCorner), fixture.right);
        QCOMPARE(fixture.tabs->tabBar()->width(), 0);
        QVERIFY(fixture.navigation->isVisible());
        QVERIFY(fixture.media->mapTo(&fixture.window, QPoint()).y()
            >= fixture.navigation->mapTo(&fixture.window, QPoint(0, fixture.navigation->height())).y());
        QSignalSpy clicks(fixture.maximize, &QPushButton::clicked);
        QTest::mouseClick(fixture.maximize, Qt::LeftButton);
        QCOMPARE(clicks.count(), 1);
        QTest::mouseClick(fixture.search, Qt::LeftButton);
        QTest::keyClicks(fixture.search, QStringLiteral("test"));
        QCOMPARE(fixture.search->text(), QStringLiteral("test"));
    }

    void followsResizeAndNativeImmersiveVisibility() {
        NativeShell fixture;
        billfish::shell::CompactShell shell(nullptr, [](QWidget*, QWidget*) { return true; });
        QVERIFY(shell.tryApply(&fixture.window));
        fixture.window.resize(980, 600);
        QTRY_COMPARE(fixture.navigation->geometry().right(), fixture.right->geometry().left() - 1);
        QCOMPARE(fixture.navigation->x(), fixture.left->geometry().right() + 1);
        QCOMPARE(fixture.navigation->height(), 42);
        fixture.left->hide();
        fixture.right->hide();
        QTRY_VERIFY(!fixture.navigation->isVisible());
        fixture.left->show();
        fixture.right->show();
        QTRY_VERIFY(fixture.navigation->isVisible());
        QVERIFY(shell.tryApply(&fixture.window));
        QCOMPARE(fixture.tabs->count(), 1);
        QCOMPARE(fixture.center->layout()->count(), 1);
    }

    void rejectsIncompleteShellWithoutPartialChanges() {
        NativeShell fixture;
        fixture.right->setObjectName(QStringLiteral("unknown"));
        billfish::shell::CompactShell shell(nullptr, [](QWidget*, QWidget*) { return true; });
        QVERIFY(!shell.tryApply(&fixture.window));
        QCOMPARE(fixture.tabs->count(), 4);
        QCOMPARE(fixture.navigation->parentWidget(), fixture.center);
        QVERIFY(fixture.tabs->tabBar()->isVisible());
    }
};
QTEST_MAIN(CompactShellTests)
#include "test_compact_shell.moc"
