#include "shell/eagle_toolbar.hpp"
#include "automation/app_input_scope.hpp"
#include "shell/legacy_feature_gate.hpp"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>

class DraggableTabBar : public QTabBar {
    Q_OBJECT
public:
    QSize sizeHint() const override { return QSize(QTabBar::sizeHint().width(), 42); }
};
class DraggableTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit DraggableTabWidget(QWidget* parent) : QTabWidget(parent) { setTabBar(new DraggableTabBar); }
};
class BfLocalResWidget : public QWidget { Q_OBJECT };
class BFItemIconSplitter : public QSplitter { Q_OBJECT public: using QSplitter::QSplitter; };
class LeftMainTitleBar : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class BFStyleMenu : public QMenu { Q_OBJECT };
class CConditionFilterWidget : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    int clears = 0;
public slots:
    void slotAllFilterBtnClear() { ++clears; }
};
namespace ToolModule {
class BFNavigationBar : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class NavSearchWid : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class BFNavLayoutMenu : public QMenu { Q_OBJECT };
class BFNavSortMenu : public QMenu { Q_OBJECT };
class BFNavInfoMenu : public QMenu { Q_OBJECT };
class BFNavSideMenu : public QMenu { Q_OBJECT };
}

struct ToolbarFixture {
    QWidget window;
    DraggableTabWidget* tabs = new DraggableTabWidget(&window);
    BfLocalResWidget* local = new BfLocalResWidget;
    BFItemIconSplitter* splitter = new BFItemIconSplitter(Qt::Horizontal, local);
    QWidget* left = new QWidget;
    QWidget* center = new QWidget;
    QWidget* right = new QWidget;
    CConditionFilterWidget* conditions = new CConditionFilterWidget(center);
    LeftMainTitleBar* leftCorner = new LeftMainTitleBar(tabs);
    QWidget* rightCorner = new QWidget(tabs);
    ToolModule::BFNavigationBar* navigation = new ToolModule::BFNavigationBar(tabs);
    QStackedWidget* stack = new QStackedWidget(navigation);
    QWidget* normal = new QWidget;
    QWidget* contents = new QWidget(normal);
    QLabel* title = new QLabel(QStringLiteral("当前文件夹"));
    QWidget* navButtons = new QWidget;
    QWidget* search = new QWidget;
    ToolModule::NavSearchWid* field = new ToolModule::NavSearchWid(search);
    QLineEdit* edit = new QLineEdit;
    QWidget* zoom = new QWidget;
    QWidget* nativeButtons = new QWidget;
    ToolModule::BFNavLayoutMenu layouts;
    ToolModule::BFNavSortMenu sorting;
    ToolModule::BFNavInfoMenu info;
    ToolModule::BFNavSideMenu side;
    BFStyleMenu dockSide;
    BFStyleMenu imports;
    QAction* listAction = nullptr;
    QAction* nameSort = nullptr;
    QAction* infoName = nullptr;
    QList<QAction*> legacyLayoutShortcuts;
    int sortCalls = 0;
    int importCalls = 0;

    ToolbarFixture() {
        window.resize(1160, 700);
        auto* root = new QVBoxLayout(&window);
        root->setContentsMargins(1, 1, 1, 1);
        root->addWidget(tabs);
        tabs->setStyleSheet(QStringLiteral("QTabWidget::pane { border: 0; }"));
        tabs->setCornerWidget(leftCorner, Qt::TopLeftCorner);
        tabs->setCornerWidget(rightCorner, Qt::TopRightCorner);
        leftCorner->setFixedSize(44, 42);
        rightCorner->setObjectName(QStringLiteral("right_title"));
        rightCorner->setFixedSize(130, 42);
        auto* outer = new QGridLayout(leftCorner);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* leftContents = new QWidget;
        leftContents->setObjectName(QStringLiteral("wid_main"));
        leftContents->setFixedHeight(42);
        outer->addWidget(leftContents);
        auto* cornerRow = new QHBoxLayout(leftContents);
        for (const QString& name : {QStringLiteral("lab_logo"), QStringLiteral("lab_name"), QStringLiteral("lab_channel_name")}) {
            auto* label = new QLabel(QStringLiteral("Billfish")); label->setObjectName(name); cornerRow->addWidget(label);
        }
        cornerRow->addStretch();
        auto* menuHost = new QWidget;
        menuHost->setFixedSize(26, 26);
        auto* menuRow = new QHBoxLayout(menuHost);
        menuRow->setContentsMargins(0, 0, 0, 0);
        auto* hamburger = new QPushButton;
        hamburger->setFixedSize(26, 26);
        hamburger->setObjectName(QStringLiteral("LeftMainTitleBar_btn_menu"));
        menuRow->addWidget(hamburger);
        cornerRow->addWidget(menuHost);
        auto* rightRow = new QHBoxLayout(rightCorner);
        for (const QString& name : {QStringLiteral("BFDockAreaSideButton"), QStringLiteral("BFDockAreaMinimizeButton"),
                QStringLiteral("BFDockAreaMaximizeButton"), QStringLiteral("BFDockAreaCloseButton")}) {
            auto* button = new QToolButton; button->setFixedSize(26, 26); button->setObjectName(name); rightRow->addWidget(button);
        }
        auto* localRow = new QHBoxLayout(local);
        localRow->setContentsMargins(0, 0, 0, 0);
        localRow->addWidget(splitter);
        splitter->addWidget(left); splitter->addWidget(center); splitter->addWidget(right);
        left->setMinimumWidth(208); right->setMinimumWidth(203);
        splitter->setSizes({208, 747, 203});
        tabs->addTab(local, QStringLiteral("素材库"));
        tabs->tabBar()->setFixedWidth(0);
        auto* navigationRow = new QVBoxLayout(navigation);
        navigationRow->setContentsMargins(0, 0, 0, 0);
        stack->setObjectName(QStringLiteral("stackwid_nav")); navigationRow->addWidget(stack);
        normal->setObjectName(QStringLiteral("pag_normal_source"));
        auto* pageRow = new QVBoxLayout(normal); pageRow->setContentsMargins(0, 0, 0, 0); pageRow->addWidget(contents);
        contents->setObjectName(QStringLiteral("wid_main"));
        auto* row = new QHBoxLayout(contents);
        title->setObjectName(QStringLiteral("lab_view_name"));
        navButtons->setObjectName(QStringLiteral("wid_btn_stack")); navButtons->setFixedWidth(62);
        search->setObjectName(QStringLiteral("wid_search"));
        field->setObjectName(QStringLiteral("m_search_wid"));
        auto* searchRow = new QVBoxLayout(search); searchRow->setContentsMargins(0, 0, 0, 0); searchRow->addWidget(field);
        auto* fieldRow = new QHBoxLayout(field); fieldRow->setContentsMargins(0, 0, 0, 0);
        edit->setObjectName(QStringLiteral("edit_search_input")); fieldRow->addWidget(edit);
        zoom->setObjectName(QStringLiteral("zoom_slider")); zoom->setFixedWidth(130);
        nativeButtons->setObjectName(QStringLiteral("btn_widget")); nativeButtons->setFixedWidth(86);
        row->addWidget(navButtons); row->addWidget(title); row->addWidget(search); row->addStretch(); row->addWidget(zoom); row->addWidget(nativeButtons);
        stack->addWidget(normal); stack->addWidget(new QWidget);
        auto* layoutGroup = new QActionGroup(&layouts);
        for (const QString& name : {QStringLiteral("网格"), QStringLiteral("瀑布流"), QStringLiteral("自适应"), QStringLiteral("列表")}) {
            auto* action = layouts.addAction(name); action->setCheckable(true); layoutGroup->addAction(action);
            if (name == QStringLiteral("网格")) action->setChecked(true);
            if (name == QStringLiteral("列表")) listAction = action;
        }
        window.addAction(listAction);
        for (int index = 0; index < 4; ++index) {
            auto* action = new QAction(navigation);
            action->setShortcut(QKeySequence(Qt::ALT | (Qt::Key_1 + index)));
            navigation->addAction(action);
            legacyLayoutShortcuts.append(action);
        }
        auto* direction = new QActionGroup(&sorting);
        for (const QString& name : {QStringLiteral("升序"), QStringLiteral("降序")}) {
            auto* action = sorting.addAction(name); action->setCheckable(true); direction->addAction(action);
        }
        auto* fields = new QActionGroup(&sorting);
        for (const QString& name : {QStringLiteral("导入时间"), QStringLiteral("名称")}) {
            auto* action = sorting.addAction(name); action->setCheckable(true); fields->addAction(action);
            if (name == QStringLiteral("导入时间")) action->setChecked(true);
            if (name == QStringLiteral("名称")) nameSort = action;
        }
        QObject::connect(nameSort, &QAction::triggered, &window, [this] { ++sortCalls; });
        infoName = info.addAction(QStringLiteral("名称")); infoName->setCheckable(true);
        info.setProperty("test.layout", QVariant::fromValue(static_cast<QObject*>(&layouts)));
        dockSide.setObjectName(QStringLiteral("BFDockAreaSideMenu"));
        for (const QString& name : {QStringLiteral("BFDockAreaHideLeftAction"), QStringLiteral("BFDockAreaHideRightAction")}) {
            auto* action = dockSide.addAction(name); action->setObjectName(name);
            const int index = name == QStringLiteral("BFDockAreaHideLeftAction") ? 0 : 2;
            QObject::connect(action, &QAction::triggered, splitter, [this, index] {
                QList<int> sizes = splitter->sizes();
                sizes[index] = sizes.at(index) ? 0 : (index == 0 ? 208 : 203);
                splitter->setSizes(sizes);
            });
        }
        // The navigation menu's side actions have no live pane connection in
        // the frozen build; mirroring that prevents a checked-only regression.
        for (const QString& name : {QStringLiteral("Left column"), QStringLiteral("Right column"), QStringLiteral("Filter bar")})
            side.addAction(name)->setCheckable(true);
        for (const QString& name : {QStringLiteral("导入文件"), QStringLiteral("导入本地文件夹"), QStringLiteral("导入Billfish素材包")}) {
            auto* action = imports.addAction(name);
            QObject::connect(action, &QAction::triggered, &window, [this] { ++importCalls; });
        }
        window.show();
        QTest::qWait(1);
        navigation->setGeometry(44, 0, tabs->width() - 174, 42);
        navigation->raise();
    }
    static bool refreshMenu(QMenu* menu) {
        if (auto* layout = qobject_cast<QMenu*>(menu->property("test.layout").value<QObject*>())) {
            bool list = false;
            for (QAction* action : layout->actions()) if (action->text() == QStringLiteral("列表")) list = action->isChecked();
            for (QAction* action : menu->actions()) action->setEnabled(!list);
        }
        return true;
    }
};

class EagleToolbarTests final : public QObject {
    Q_OBJECT
    QTemporaryDir profile_;
private slots:
    void initTestCase() {
        QVERIFY(profile_.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile_.path());
    }
    void init() {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Billfish"), QStringLiteral("maintained-shell"));
        settings.clear();
    }
    void reusesNativeControlsAndPreservesSpecialModes() {
        ToolbarFixture fixture;
        const QList<int> sizes = fixture.splitter->sizes();
        billfish::shell::EagleToolbar toolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar.attach());
        billfish::shell::LegacyFeatureGate gate; gate.start(); gate.enforce(&fixture.window);
        QTRY_COMPARE(fixture.leftCorner->width(), fixture.left->width());
        QTRY_COMPARE(fixture.rightCorner->geometry().right() + 1, fixture.tabs->width());
        QTRY_COMPARE(fixture.rightCorner->sizeHint().width(), fixture.rightCorner->width());
        for (const QString& name : {QStringLiteral("BFDockAreaMinimizeButton"), QStringLiteral("BFDockAreaMaximizeButton"), QStringLiteral("BFDockAreaCloseButton")}) {
            auto* button = fixture.rightCorner->findChild<QToolButton*>(name);
            QVERIFY(button && fixture.rightCorner->rect().contains(button->geometry()));
        }
        QCOMPARE(fixture.navigation->height(), 48);
        QCOMPARE(fixture.field->height(), 32);
        QCOMPARE(fixture.search->parentWidget(), fixture.contents);
        QCOMPARE(fixture.zoom->parentWidget(), fixture.contents);
        QCOMPARE(fixture.splitter->sizes(), sizes);
        for (QWidget* region : fixture.contents->findChildren<QWidget*>(QStringLiteral("spacerWidget"), Qt::FindDirectChildrenOnly)) {
            if (!region->isVisible() || region->width() == 0) continue;
            QVERIFY(region->geometry().top() >= 0 && region->geometry().bottom() < 48);
            for (QWidget* control : {fixture.navButtons, static_cast<QWidget*>(fixture.title), fixture.search, fixture.zoom})
                if (!control->isHidden()) QVERIFY(!region->geometry().intersects(control->geometry()));
        }
        QTRY_VERIFY(fixture.local->mapTo(fixture.tabs, QPoint()).y() >= 48);
        fixture.leftCorner->hide(); fixture.rightCorner->hide(); fixture.tabs->tabBar()->hide();
        QTRY_COMPARE(fixture.local->mapTo(fixture.tabs, QPoint()).y(), 0);
        fixture.leftCorner->show(); fixture.rightCorner->show(); fixture.tabs->tabBar()->show();
        QTRY_VERIFY(fixture.local->mapTo(fixture.tabs, QPoint()).y() >= 48);
        auto* sidebar = fixture.window.findChild<QToolButton*>(QStringLiteral("BillfishSidebarToggleButton"));
        QVERIFY(sidebar);
        QTest::mouseClick(sidebar, Qt::LeftButton);
        QTRY_VERIFY(fixture.splitter->sizes().at(0) == 0 && fixture.splitter->sizes().at(2) == 0);
        QVERIFY(fixture.left->isVisible() && fixture.right->isVisible());
        QVERIFY(fixture.left->width() > 0 && fixture.right->width() > 0);
        QTRY_COMPARE(fixture.leftCorner->width(), 44);
        fixture.dockSide.actions().at(1)->trigger();
        QVERIFY(fixture.splitter->sizes().at(0) == 0 && fixture.splitter->sizes().at(2) > 0);
        QTest::mouseClick(sidebar, Qt::LeftButton);
        QTRY_VERIFY(fixture.splitter->sizes().at(0) > 0 && fixture.splitter->sizes().at(2) > 0);
        QTRY_COMPARE(fixture.leftCorner->width(), fixture.left->width());
        auto* filter = fixture.contents->findChild<QToolButton*>(QStringLiteral("BillfishFilterToolbarButton"));
        QVERIFY(filter && filter->defaultAction());
        QTRY_VERIFY(filter->isChecked());
        QTest::mouseClick(filter, Qt::LeftButton);
        QVERIFY(fixture.conditions->isHidden());
        fixture.conditions->show();
        QTRY_VERIFY(fixture.conditions->isHidden());
        QTRY_VERIFY(!filter->isChecked());
        QTest::mouseClick(filter, Qt::LeftButton);
        QVERIFY(!fixture.conditions->isHidden());
        auto* add = fixture.window.findChild<QToolButton*>(QStringLiteral("BillfishAddMenuButton"));
        QVERIFY(add && add->menu() == &fixture.imports);
        fixture.imports.actions().first()->trigger();
        QCOMPARE(fixture.importCalls, 1);
        fixture.edit->setText(QStringLiteral("current query"));
        const QString caption = fixture.title->text();
        QTest::mouseClick(fixture.title, Qt::LeftButton);
        QVERIFY(fixture.edit->text().isEmpty());
        QCOMPARE(fixture.conditions->clears, 1);
        QCOMPARE(fixture.title->text(), caption);
        fixture.stack->setCurrentIndex(1);
        QTest::qWait(1);
        QCOMPARE(fixture.stack->currentIndex(), 1);
        QVERIFY(!fixture.search->isVisible());
        fixture.stack->setCurrentIndex(0);
        fixture.navigation->resize(410, 48);
        QTRY_VERIFY(fixture.zoom->isHidden());
        QVERIFY(fixture.search->width() >= 180);
        QVERIFY(!fixture.search->isHidden());
    }

    void filterPreferenceSurvivesProviderAndToolbarRestart() {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Billfish"), QStringLiteral("maintained-shell"));
        {
        ToolbarFixture fixture;
        billfish::shell::EagleToolbar toolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar.attach());
        fixture.center->hide(); fixture.center->show();
        QVERIFY(!settings.contains(QStringLiteral("filter-visible")));
        auto* filter = fixture.contents->findChild<QToolButton*>(QStringLiteral("BillfishFilterToolbarButton"));
        QVERIFY(filter && filter->isChecked());
        QTest::mouseClick(filter, Qt::LeftButton);
        QVERIFY(fixture.conditions->isHidden());
        fixture.conditions->show();
        QTRY_VERIFY(fixture.conditions->isHidden());
        QCOMPARE(settings.value(QStringLiteral("filter-visible")).toBool(), false);
        }
        ToolbarFixture fixture;
        QVERIFY(!fixture.conditions->isHidden());
        billfish::shell::EagleToolbar toolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar.attach());
        QVERIFY(fixture.conditions->isHidden());
        auto* filter = fixture.contents->findChild<QToolButton*>(QStringLiteral("BillfishFilterToolbarButton"));
        QVERIFY(filter && !filter->isChecked());
        QTest::mouseClick(filter, Qt::LeftButton);
        QVERIFY(!fixture.conditions->isHidden());
        fixture.center->hide(); fixture.center->show();
        QCOMPARE(settings.value(QStringLiteral("filter-visible")).toBool(), true);
        fixture.conditions->hide();
        QCoreApplication::processEvents();
        QVERIFY(fixture.conditions->isHidden());
        fixture.stack->setCurrentIndex(1); fixture.stack->setCurrentIndex(0);
        QTRY_VERIFY(!fixture.conditions->isHidden());
    }

    void savedVisibleFilterRestoresAtNormalBindingAndRespectsNativeHide() {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Billfish"), QStringLiteral("maintained-shell"));
        settings.setValue(QStringLiteral("filter-visible"), true);
        ToolbarFixture fixture;
        fixture.conditions->hide();
        billfish::shell::EagleToolbar toolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar.attach());
        QVERIFY(!fixture.conditions->isHidden());
        fixture.conditions->hide();
        QCoreApplication::processEvents();
        QVERIFY(fixture.conditions->isHidden());
        QCOMPARE(settings.value(QStringLiteral("filter-visible")).toBool(), true);
    }

    void captionRetainsWidthAfterNativeTextWasShortened() {
        ToolbarFixture fixture;
        fixture.title->setText(QStringLiteral(".."));
        billfish::shell::EagleToolbar toolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar.attach());
        QTRY_VERIFY(fixture.title->width() >= 144);
        QCOMPARE(fixture.title->text(), QStringLiteral(".."));
        const QString fullTitle = QStringLiteral("在资源库中搜索 API-Test-40");
        fixture.title->setText(fullTitle);
        QCoreApplication::processEvents();
        QCOMPARE(fixture.title->text(), fullTitle);
        QVERIFY(fixture.title->width() >= 144);
        QVERIFY(!fixture.title->geometry().intersects(fixture.search->geometry()));
    }

    void sharedLayoutActionsStayLiveInsideOnePanel() {
        ToolbarFixture fixture;
        billfish::shell::EagleToolbar toolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar.attach());
        auto* button = fixture.contents->findChild<QToolButton*>(QStringLiteral("BillfishLayoutToolbarButton"));
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton);
        auto* panel = fixture.window.findChild<QFrame*>(QStringLiteral("BillfishLayoutPanel"));
        QVERIFY(panel && panel->isVisible());
        QCOMPARE(panel->grab().toImage().pixelColor(4, 4), QColor(37, 39, 43));
        QToolButton* infoButton = nullptr;
        for (QToolButton* candidate : panel->findChildren<QToolButton*>()) {
            if (candidate->defaultAction() == fixture.infoName) infoButton = candidate;
        }
        auto* layoutMode = panel->findChild<QComboBox*>(QStringLiteral("BillfishLayoutMode"));
        QVERIFY(layoutMode && infoButton);
        QVERIFY(infoButton->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(layoutMode, "activated", Q_ARG(int, layoutMode->findText(QStringLiteral("列表")))));
        QVERIFY(fixture.listAction->isChecked());
        QVERIFY(panel->isVisible());
        QVERIFY(!infoButton->isEnabled());
        for (QAction* action : fixture.layouts.actions()) if (action->text() == QStringLiteral("网格")) action->trigger();
        QCOMPARE(layoutMode->currentText(), QStringLiteral("网格"));
        QVERIFY(infoButton->isEnabled());
        auto* sort = panel->findChild<QComboBox*>(QStringLiteral("BillfishSortField"));
        QVERIFY(sort);
        const int index = sort->findText(QStringLiteral("名称"));
        QVERIFY(index >= 0);
        QVERIFY(QMetaObject::invokeMethod(sort, "activated", Q_ARG(int, index)));
        QCOMPARE(fixture.sortCalls, 1);
        QTest::keyClick(panel, Qt::Key_Escape);
        QVERIFY(!panel->isVisible());
    }

    void scopedLayoutPanelAndChoicesWorkWithoutGrabbingGlobalInput() {
        ToolbarFixture fixture;
        billfish::shell::EagleToolbar toolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar.attach());
        auto* button = fixture.contents->findChild<QToolButton*>(QStringLiteral("BillfishLayoutToolbarButton"));
        QVERIFY(button);
        {
            billfish::automation::AppInputScope scope;
            QTest::mouseClick(button, Qt::LeftButton);
        }
        auto* panel = fixture.window.findChild<QFrame*>(QStringLiteral("BillfishLayoutPanel"));
        QVERIFY(panel && panel->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        QVERIFY(panel->testAttribute(Qt::WA_ShowWithoutActivating));
        auto* combo = panel->findChild<QComboBox*>(QStringLiteral("BillfishLayoutMode"));
        QVERIFY(combo);
        {
            billfish::automation::AppInputScope scope;
            combo->showPopup();
        }
        auto* choices = combo->findChild<QListView*>(QStringLiteral("BillfishLayoutChoicePopup"));
        QVERIFY(choices && choices->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        const auto list = choices->model()->index(combo->findText(QStringLiteral("列表")), 0);
        QVERIFY(list.isValid());
        QTest::mouseClick(choices->viewport(), Qt::LeftButton, Qt::NoModifier, choices->visualRect(list).center());
        QVERIFY(fixture.listAction->isChecked());
        QVERIFY(!choices->isVisible());
        QVERIFY(panel->isVisible());
        QTest::mouseClick(fixture.edit, Qt::LeftButton);
        QVERIFY(!panel->isVisible());
        QTest::mouseClick(button, Qt::LeftButton);
        panel = fixture.window.findChild<QFrame*>(QStringLiteral("BillfishLayoutPanel"));
        QVERIFY(panel && panel->isVisible());
        QCOMPARE(panel->windowType(), Qt::Popup);
        panel->close();
    }

    void layoutShortcutsWorkBeforeOpeningPanelForAssociatedActions() {
        ToolbarFixture fixture;
        QVERIFY(fixture.window.actions().contains(fixture.listAction));
        billfish::shell::EagleToolbar toolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar.attach());
        for (QAction* action : fixture.legacyLayoutShortcuts) QVERIFY(action->shortcut().isEmpty());
        QCOMPARE(fixture.listAction->shortcut(), QKeySequence(Qt::ALT | Qt::Key_4));
        QCOMPARE(fixture.listAction->shortcutContext(), Qt::WindowShortcut);
        fixture.window.activateWindow(); fixture.window.setFocus();
        QCoreApplication::processEvents();
        QTest::keyClick(&fixture.window, Qt::Key_4, Qt::AltModifier);
        QTRY_VERIFY(fixture.listAction->isChecked());
        QVERIFY(!fixture.window.findChild<QFrame*>(QStringLiteral("BillfishLayoutPanel")));
    }

    void pluginAnchorInputProtectionAndDestruction() {
        ToolbarFixture fixture;
        auto* plugin = new QAction(QStringLiteral("插件"), &fixture.window);
        plugin->setObjectName(QStringLiteral("BillfishPluginMenuAction")); fixture.window.addAction(plugin);
        QPoint anchor;
        int calls = 0;
        QObject::connect(plugin, &QAction::triggered, &fixture.window, [&] {
            anchor = plugin->property("billfishPluginMenuPosition").toPoint();
            plugin->setProperty("billfishPluginMenuPosition", QVariant()); ++calls;
        });
        auto* toolbar = new billfish::shell::EagleToolbar(&fixture.window, fixture.tabs, fixture.navigation, &ToolbarFixture::refreshMenu);
        QVERIFY(toolbar->attach());
        auto* button = fixture.contents->findChild<QToolButton*>(QStringLiteral("BillfishPluginToolbarButton"));
        QVERIFY(button && button->defaultAction() == plugin);
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(calls, 1);
        QCOMPARE(anchor, button->mapToGlobal(QPoint(0, button->height() + 4)));
        QTest::mousePress(button, Qt::LeftButton);
        QTest::mouseRelease(button, Qt::LeftButton, Qt::NoModifier, QPoint(-10, -10));
        QVERIFY(!plugin->property("billfishPluginMenuPosition").isValid());
        QAction* pin = fixture.window.findChild<QAction*>(QStringLiteral("BillfishPinWindowAction"));
        QVERIFY(pin);
        QSignalSpy pinChanges(pin, &QAction::triggered);
        fixture.window.activateWindow(); fixture.edit->setFocus();
        QTRY_VERIFY(fixture.edit->hasFocus());
        QTest::keyClick(fixture.edit, Qt::Key_T, Qt::ShiftModifier);
        QCOMPARE(fixture.edit->text().toLower(), QStringLiteral("t"));
        QCOMPARE(pinChanges.count(), 0);
        auto* rightRow = fixture.rightCorner->layout();
        const int beforeDelete = rightRow->count();
        delete fixture.field;
        QCoreApplication::processEvents();
        delete toolbar;
        QCoreApplication::processEvents();
        QVERIFY(rightRow->count() < beforeDelete);
    }
};
QTEST_MAIN(EagleToolbarTests)
#include "test_eagle_toolbar.moc"
