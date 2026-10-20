#include "automation/automation_ui.hpp"
#include "automation/app_input_scope.hpp"
#include "automation/plugin_manager.hpp"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>
#include <QUrl>
#include <QUuid>
#include <QWindow>

#include <memory>

using billfish::automation::AutomationUi;
using billfish::automation::PluginManager;
using billfish::automation::PluginSettingsPanel;

// The fixture retains the observed parent structure and identities of the
// frozen Preferences UI. Its native navigation uses the real signal signature.
enum SettingMode { Common, Library, Media, Interface, Action, Shortcut, Notification };

class IconButtonWidget final : public QPushButton {
    Q_OBJECT
public:
    IconButtonWidget(SettingMode mode, QWidget* parent) : QPushButton(parent)
    {
        setCheckable(true);
        connect(this, &QPushButton::clicked, this, [this, mode] { emit SignBtnSelect(mode); });
    }
signals:
    void SignBtnSelect(SettingMode mode);
};

class PreferencesCommonWidget final : public QWidget {
    Q_OBJECT
public: using QWidget::QWidget;
};
class PreferencesLibraryWidget final : public QWidget {
    Q_OBJECT
public: using QWidget::QWidget;
};
class PreferencesMediaWidget final : public QWidget {
    Q_OBJECT
public: using QWidget::QWidget;
};
class PreferencesUserInterface final : public QWidget {
    Q_OBJECT
public: using QWidget::QWidget;
};
class PreferencesAction final : public QWidget {
    Q_OBJECT
public: using QWidget::QWidget;
};
class PreferencesShortcut final : public QWidget {
    Q_OBJECT
public: using QWidget::QWidget;
};
class PreferencesNotification final : public QWidget {
    Q_OBJECT
public: using QWidget::QWidget;
};

class Preferences final : public QWidget {
    Q_OBJECT
public:
    explicit Preferences(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("kLoading_dialog"));
        resize(720, 530);
        auto* layout = new QGridLayout(this);
        layout->setObjectName(QStringLiteral("main_layout"));
        auto* background = new QWidget(this);
        background->setObjectName(QStringLiteral("wid_main"));
        layout->addWidget(background);
        stack = new QStackedWidget(this);
        stack->setGeometry(165, 49, 550, 480);
        const QList<QWidget*> pages = {
            new PreferencesCommonWidget(stack), new PreferencesLibraryWidget(stack),
            new PreferencesMediaWidget(stack), new PreferencesUserInterface(stack),
            new PreferencesAction(stack), new PreferencesShortcut(stack), new PreferencesNotification(stack)};
        const QStringList names = {QStringLiteral("btn_common"), QStringLiteral("btn_library"),
            QStringLiteral("btn_media"), QStringLiteral("btn_ui"), QStringLiteral("btn_action"),
            QStringLiteral("btn_shortcut"), QStringLiteral("btn_notification")};
        for (int index = 0; index < pages.size(); ++index) {
            pages.at(index)->setObjectName(QString::fromLatin1(pages.at(index)->metaObject()->className()));
            stack->addWidget(pages.at(index));
            auto* button = new IconButtonWidget(static_cast<SettingMode>(index), this);
            button->setObjectName(names.at(index));
            button->setGeometry(12, 61 + index * 40, 142, 32);
        }
    }
    QStackedWidget* stack;
};

class ImportSurface final : public QWidget {
public:
    explicit ImportSurface(QWidget* parent) : QWidget(parent) { setAcceptDrops(true); }
    int entered = 0;
protected:
    void dragEnterEvent(QDragEnterEvent* event) override { ++entered; event->acceptProposedAction(); }
};

class BFLoadingDialog final : public QDialog {
    Q_OBJECT
public:
    explicit BFLoadingDialog(QWidget* parent = nullptr) : QDialog(parent)
    {
        setObjectName(QStringLiteral("kLoading_dialog"));
        setModal(true);
    }
};

class BFFileMenu final : public QMenu {
    Q_OBJECT
public: using QMenu::QMenu;
};

class AutomationUiTests final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> environment_;
    QByteArray oldAppData_;
    QByteArray oldElectron_;
    bool appDataWasSet_ = false;
    bool electronWasSet_ = false;
    QString installAsynchronously(PluginManager& manager, const QString& source)
    {
        const auto helperSource = QFINDTESTDATA("../../../automation/plugin_runtime/package_installer.py");
        if (helperSource.isEmpty()) return QStringLiteral("Cannot locate package installer test support.");
        const auto helperDirectory = QDir(environment_->path()).filePath(QStringLiteral("automation/plugin_runtime"));
        if (!QDir().mkpath(helperDirectory)) return QStringLiteral("Cannot create installer helper directory.");
        const auto helperTarget = QDir(helperDirectory).filePath(QStringLiteral("package_installer.py"));
        if (!QFile::exists(helperTarget) && !QFile::copy(helperSource, helperTarget))
            return QStringLiteral("Cannot copy installer helper.");
        const auto request = QStringLiteral("ui-test-install-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool settled = false;
        QString failure;
        const auto finished = connect(&manager, &PluginManager::finished, this,
            [&settled, &request](const QString& id, const QJsonObject&) { if (id == request) settled = true; });
        const auto failed = connect(&manager, &PluginManager::failed, this,
            [&settled, &failure, &request](const QString& id, const QString& code, const QString& message) {
                if (id == request) { failure = code + QStringLiteral(": ") + message; settled = true; }
            });
        manager.installPluginAsync(request, source);
        QElapsedTimer timeout;
        timeout.start();
        while (!settled && timeout.elapsed() < 15000) QTest::qWait(20);
        disconnect(finished);
        disconnect(failed);
        return settled ? failure : QStringLiteral("Asynchronous plugin install timed out.");
    }
    QString installFixturePlugin(PluginManager& manager, const QString& id, const QString& name,
                                 const QString& mode = QStringLiteral("commands"), bool preview = false)
    {
        const auto source = QDir(environment_->path()).filePath(QStringLiteral("source-") + id);
        if (!QDir().mkpath(source)) return QStringLiteral("Cannot create fixture directory: ") + source;
        QFile entry(QDir(source).filePath(QStringLiteral("index.html")));
        if (!entry.open(QIODevice::WriteOnly) || entry.write("<!doctype html><title>Disabled UI fixture</title>") < 0) return entry.errorString();
        entry.close();
        QFile manifest(QDir(source).filePath(QStringLiteral("manifest.json")));
        if (!manifest.open(QIODevice::WriteOnly)) return manifest.errorString();
        QJsonObject main{{QStringLiteral("url"), QStringLiteral("index.html")}, {QStringLiteral("width"), 600},
                         {QStringLiteral("height"), 400}};
        if (mode == QStringLiteral("service")) main.insert(QStringLiteral("serviceMode"), true);
        QJsonObject value{{QStringLiteral("id"), id},
            {QStringLiteral("name"), name}, {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("main"), main}};
        if (mode == QStringLiteral("dependency")) {
            value.remove(QStringLiteral("main"));
            value.insert(QStringLiteral("preview"), QJsonObject{});
        }
        if (mode == QStringLiteral("commands")) {
            const QJsonObject command{{QStringLiteral("id"), QStringLiteral("run")},
                {QStringLiteral("title"), QStringLiteral("Run")},
                {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
            value.insert(QStringLiteral("billfish"), QJsonObject{{QStringLiteral("permissions"), QJsonArray{}},
                {QStringLiteral("commands"), QJsonArray{command}}});
        }
        if (preview) value.insert(QStringLiteral("preview"), QJsonObject{
            {QStringLiteral("glb,obj"), QJsonObject{{QStringLiteral("viewer"),
                QJsonObject{{QStringLiteral("path"), QStringLiteral("index.html")}}}}}});
        if (manifest.write(QJsonDocument(value).toJson()) < 0) return manifest.errorString();
        manifest.close();
        return installAsynchronously(manager, source);
    }
private slots:
    void fileContextMenuReusesExportStyleAndRefreshesPluginInventory()
    {
        PluginManager manager(environment_->path());
        auto error = installFixturePlugin(manager, QStringLiteral("test.enabled"), QStringLiteral("Enabled"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        error = installFixturePlugin(manager, QStringLiteral("test.disabled"), QStringLiteral("Disabled"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        error = installFixturePlugin(manager, QStringLiteral("test.dependency"), QStringLiteral("Dependency"), QStringLiteral("dependency"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto formatPlugin = QStringLiteral("89fb801f-7761-4de6-b7ce-3785a084edaf");
        error = installFixturePlugin(manager, formatPlugin, QStringLiteral("Reserved format window"), QStringLiteral("window"), true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        manager.setEnabled(QStringLiteral("test.enabled"), true);
        manager.setEnabled(QStringLiteral("test.dependency"), true);
        manager.setEnabled(formatPlugin, true);
        QWidget main;
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        ui.setMaterialContextProvider([] { return QJsonObject{{QStringLiteral("available"), true},
            {QStringLiteral("library_root"), QStringLiteral("X:/fixture")},
            {QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("item_id"), QStringLiteral("1")},
                {QStringLiteral("extension"), QStringLiteral("png")}}}}}; });
        BFFileMenu menu(&main);
        // QAction-owned submenus are not necessarily QObject children of BFFileMenu.
        QMenu exportMenu(QStringLiteral("导出"), &main);
        exportMenu.setWindowFlags(exportMenu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        exportMenu.setAttribute(Qt::WA_TranslucentBackground);
        exportMenu.setStyleSheet(QStringLiteral("QMenu {padding:9px;}"));
        menu.addMenu(&exportMenu);
        menu.addAction(QStringLiteral("丢到回收站"));
        menu.show();
        auto* plugins = menu.findChild<QMenu*>(QStringLiteral("BillfishContextPluginMenu"));
        QVERIFY(plugins);
        QCOMPARE(menu.actions().size(), 3);
        QCOMPARE(menu.actions().at(0), plugins->menuAction());
        QCOMPARE(plugins->styleSheet(), exportMenu.styleSheet());
        QVERIFY(!plugins->menuAction()->icon().isNull());
        QCOMPARE(plugins->windowFlags(), exportMenu.windowFlags());
        QCOMPARE(plugins->testAttribute(Qt::WA_TranslucentBackground), exportMenu.testAttribute(Qt::WA_TranslucentBackground));
        QCOMPARE(plugins->actions().first()->text(), QStringLiteral("Enabled"));
        QVERIFY(!plugins->actions().first()->isEnabled());
        QCOMPARE(plugins->actions().size(), 3); // One command group, separator, management.
        menu.hide();
        menu.show();
        QCOMPARE(menu.actions().size(), 3);
        QCOMPARE(menu.findChildren<QMenu*>(QStringLiteral("BillfishContextPluginMenu")).size(), 1);
        QCOMPARE(plugins->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
        menu.hide();
        menu.clear();
        menu.addMenu(&exportMenu);
        menu.show();
        QCOMPARE(menu.findChildren<QMenu*>(QStringLiteral("BillfishContextPluginMenu")).size(), 1);
        QCOMPARE(menu.actions().first(), plugins->menuAction());
        manager.setEnabled(QStringLiteral("test.enabled"), false);
        menu.hide();
        menu.show();
        QCOMPARE(plugins->actions().first()->text(), QStringLiteral("暂无可用的插件功能"));
        menu.hide();
        ui.showPluginMenu(QPoint(20, 20));
        auto* toolbarMenu = main.findChild<QMenu*>(QStringLiteral("BillfishPluginMenu"));
        QVERIFY(toolbarMenu);
        bool formatWindowAvailable = false;
        for (auto* action : toolbarMenu->actions())
            formatWindowAvailable = formatWindowAvailable || action->text() == QStringLiteral("Reserved format window");
        QVERIFY(formatWindowAvailable);
    }

    void contextPreviewUsesOneSupportedItemAndRejectsStaleSelection()
    {
        const auto runner = QFINDTESTDATA("../../../automation/plugin_runtime/main.cjs");
        QVERIFY(!runner.isEmpty());
        QDir installRoot(QFileInfo(runner).absolutePath());
        QVERIFY(installRoot.cdUp());
        QVERIFY(installRoot.cdUp());
        const auto electron = QDir(installRoot.path()).filePath(QStringLiteral("tools/electron/electron.exe"));
        QVERIFY2(QFileInfo::exists(electron), "The pinned Electron runtime is required for preview routing validation.");
        qputenv("BILLFISH_ELECTRON", electron.toUtf8());
        PluginManager manager(installRoot.path());
        auto error = installFixturePlugin(manager, QStringLiteral("test.viewer"), QStringLiteral("Viewer"), QStringLiteral("window"), true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        manager.setLibraryRoot(QStringLiteral("X:/fixture"));
        manager.setEnabled(QStringLiteral("test.viewer"), true);
        QTRY_COMPARE_WITH_TIMEOUT(manager.previewPlugin(QStringLiteral("item.glb"), QStringLiteral("viewer")), QStringLiteral("test.viewer"), 10000);
        QWidget main;
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        const QJsonObject first{{QStringLiteral("item_id"), QStringLiteral("11")}, {QStringLiteral("extension"), QStringLiteral("glb")}};
        QJsonObject context{{QStringLiteral("available"), true}, {QStringLiteral("library_root"), QStringLiteral("X:/fixture")},
            {QStringLiteral("items"), QJsonArray{first}}};
        ui.setMaterialContextProvider([&context] { return context; });
        QStringList opened;
        ui.setFormatPreviewHandler([&opened](QString plugin, QString item, QString root) { opened = QStringList{plugin, item, root}; });
        BFFileMenu menu(&main);
        menu.addMenu(QStringLiteral("导出"));
        menu.show();
        auto* plugins = menu.findChild<QMenu*>(QStringLiteral("BillfishContextPluginMenu"));
        QVERIFY(plugins);
        auto* preview = plugins->findChild<QAction*>(QStringLiteral("BillfishContextFormatPreview"));
        QVERIFY(preview);
        preview->trigger();
        QCOMPARE(opened, QStringList({QStringLiteral("test.viewer"), QStringLiteral("11"), QStringLiteral("X:/fixture")}));
        opened.clear();
        context.insert(QStringLiteral("library_root"), QStringLiteral("Y:/other"));
        preview->trigger();
        QVERIFY(opened.isEmpty());
        context.insert(QStringLiteral("library_root"), QStringLiteral("X:/fixture"));
        context.insert(QStringLiteral("items"), QJsonArray{first, QJsonObject{{QStringLiteral("item_id"), QStringLiteral("12")},
            {QStringLiteral("extension"), QStringLiteral("png")}}});
        preview->trigger();
        QVERIFY(opened.isEmpty());
        menu.hide();
        menu.show();
        QVERIFY(!plugins->findChild<QAction*>(QStringLiteral("BillfishContextFormatPreview")));
    }

    void init()
    {
        appDataWasSet_ = qEnvironmentVariableIsSet("APPDATA");
        electronWasSet_ = qEnvironmentVariableIsSet("BILLFISH_ELECTRON");
        oldAppData_ = qgetenv("APPDATA");
        oldElectron_ = qgetenv("BILLFISH_ELECTRON");
        environment_ = std::make_unique<QTemporaryDir>();
        QVERIFY(environment_->isValid());
        qputenv("APPDATA", environment_->path().toUtf8());
        qputenv("BILLFISH_ELECTRON", (environment_->path() + QStringLiteral("/unused-electron.exe")).toUtf8());
    }

    void cleanup()
    {
        if (appDataWasSet_) qputenv("APPDATA", oldAppData_);
        else qunsetenv("APPDATA");
        if (electronWasSet_) qputenv("BILLFISH_ELECTRON", oldElectron_);
        else qunsetenv("BILLFISH_ELECTRON");
        environment_.reset();
    }

    void progressUsesRealCountsAndKeepsEditingAvailable()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        PluginManager manager(root.path());
        QWidget main;
        main.resize(900, 600);
        QLineEdit editor(&main);
        editor.setGeometry(30, 30, 200, 30);
        main.show();
        main.activateWindow();
        editor.setFocus();
        QApplication::processEvents();
        QVERIFY(editor.hasFocus());
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        ui.updateProgress(QStringLiteral("write-1"), 3, 12, QStringLiteral("正在更新备注"));
        auto* overlay = main.findChild<QWidget*>(QStringLiteral("BillfishAutomationProgress"));
        auto* bar = main.findChild<QProgressBar*>(QStringLiteral("BillfishAutomationProgressBar"));
        QVERIFY(overlay);
        QVERIFY(bar);
        QVERIFY(!overlay->isWindow());
        QVERIFY(overlay->isVisible());
        QVERIFY(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&editor));
        QCOMPARE(bar->value(), 25);
        QTest::keyClicks(&editor, QStringLiteral("still editing"));
        QCOMPARE(editor.text(), QStringLiteral("still editing"));
        main.resize(1100, 700);
        QApplication::processEvents();
        QCOMPARE(overlay->geometry().right(), main.width() - 17);
        QCOMPARE(overlay->geometry().bottom(), main.height() - 17);
        ui.finishTask(QStringLiteral("write-1"), true, QStringLiteral("完成"));
        QCOMPARE(bar->value(), 100);
        QTRY_VERIFY_WITH_TIMEOUT(!overlay->isVisible(), 3500);
    }

    void parallelUnknownTaskDoesNotPretendToHaveAPercentage()
    {
        QTemporaryDir root;
        PluginManager manager(root.path());
        QWidget main;
        main.resize(900, 600);
        main.show();
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        ui.updateProgress(QStringLiteral("known"), 7, 10, QStringLiteral("写入"));
        ui.updateProgress(QStringLiteral("unknown"), 0, 0, QStringLiteral("运行插件"));
        auto* bar = main.findChild<QProgressBar*>(QStringLiteral("BillfishAutomationProgressBar"));
        QCOMPARE(bar->minimum(), 0);
        QCOMPARE(bar->maximum(), 0);
        ui.finishTask(QStringLiteral("unknown"), true, QStringLiteral("插件完成"));
        QCOMPARE(bar->maximum(), 100);
        QCOMPARE(bar->value(), 70);
        ui.finishTask(QStringLiteral("known"), false, QStringLiteral("写入失败"));
        QCOMPARE(bar->format(), QStringLiteral("失败"));
    }

    void pluginsAreAnActualPreferencesPageAndNativeNavigationStillWorks()
    {
        PluginManager manager(environment_->path());
        AutomationUi ui(&manager);
        Preferences preferences;
        preferences.show();
        QApplication::processEvents();
        auto* button = preferences.findChild<QPushButton*>(QStringLiteral("BillfishPluginsNavigation"));
        auto* page = preferences.findChild<QWidget*>(QStringLiteral("BillfishPluginSettingsPanel"));
        QVERIFY(button);
        QVERIFY(page);
        QCOMPARE(preferences.stack->count(), 8);
        QCOMPARE(page->parentWidget(), static_cast<QWidget*>(preferences.stack));
        QCOMPARE(button->geometry(), QRect(12, 341, 142, 32));
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(preferences.stack->currentWidget(), page);
        QVERIFY(button->isChecked());
        QCOMPARE(page->window(), static_cast<QWidget*>(&preferences));
        auto* native = preferences.findChild<IconButtonWidget*>(QStringLiteral("btn_common"));
        QTest::mouseClick(native, Qt::LeftButton);
        QCOMPARE(preferences.stack->currentWidget()->objectName(), QStringLiteral("PreferencesCommonWidget"));
        QVERIFY(!button->isChecked());
        QVERIFY(ui.showPluginSettings());
        QCOMPARE(preferences.stack->currentWidget(), page);
    }

    void settingsOpenUsesTheNativeShortcutActionWithoutSendingKeys()
    {
        PluginManager manager(environment_->path());
        AutomationUi ui(&manager);
        QWidget main;
        QAction action(QStringLiteral("Preferences"), &main);
        action.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
        main.addAction(&action);
        std::unique_ptr<Preferences> preferences;
        connect(&action, &QAction::triggered, &main, [&preferences] {
            preferences = std::make_unique<Preferences>();
            preferences->show();
        });
        QVERIFY(ui.showPluginSettings());
        QTRY_VERIFY(preferences);
        QTRY_COMPARE(preferences->stack->currentWidget()->objectName(), QStringLiteral("BillfishPluginSettingsPanel"));
    }

    void scopedSettingsOpenPreservesTheInputScopeAcrossTheNativeActionQueue()
    {
        PluginManager manager(environment_->path());
        AutomationUi ui(&manager);
        QWidget main;
        QAction action(QStringLiteral("Preferences"), &main);
        action.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
        main.addAction(&action);
        bool scopedOnTrigger = false;
        std::unique_ptr<Preferences> preferences;
        connect(&action, &QAction::triggered, &main, [&] {
            scopedOnTrigger = billfish::automation::AppInputScope::active();
            preferences = std::make_unique<Preferences>();
            preferences->show();
        });
        {
            billfish::automation::AppInputScope scope;
            QVERIFY(ui.showPluginSettings());
        }
        QTRY_VERIFY(preferences);
        QVERIFY(scopedOnTrigger);
        QVERIFY(preferences->testAttribute(Qt::WA_ShowWithoutActivating));
        QCOMPARE(preferences->stack->currentWidget()->objectName(), QStringLiteral("BillfishPluginSettingsPanel"));
        QVERIFY(ui.showPluginSettings());
        QVERIFY(!preferences->testAttribute(Qt::WA_ShowWithoutActivating));
    }

    void scopedLibraryLoadingPreservesModalityAndRestoresActivationAttribute()
    {
        PluginManager manager(environment_->path());
        AutomationUi ui(&manager);
        BFLoadingDialog loading;
        const auto modality = loading.windowModality();
        const auto flags = loading.windowFlags();
        loading.winId();
        auto* nativeWindow = loading.windowHandle();
        QVERIFY(nativeWindow);
        nativeWindow->setProperty("_q_showWithoutActivating", QVariant());
        QVERIFY(!loading.testAttribute(Qt::WA_ShowWithoutActivating));
        {
            billfish::automation::AppInputScope scope;
            loading.show();
        }
        QVERIFY(loading.isVisible());
        QVERIFY(loading.testAttribute(Qt::WA_ShowWithoutActivating));
        QCOMPARE(nativeWindow->property("_q_showWithoutActivating"), QVariant(true));
        QCOMPARE(loading.windowModality(), modality);
        QCOMPARE(loading.windowFlags(), flags);
        loading.hide();
        QVERIFY(!loading.testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(!nativeWindow->property("_q_showWithoutActivating").isValid());
        loading.show();
        QVERIFY(!loading.testAttribute(Qt::WA_ShowWithoutActivating));
        loading.hide();
        loading.setAttribute(Qt::WA_ShowWithoutActivating);
        nativeWindow->setProperty("_q_showWithoutActivating", false);
        {
            billfish::automation::AppInputScope scope;
            loading.show();
        }
        QCOMPARE(nativeWindow->property("_q_showWithoutActivating"), QVariant(true));
        loading.hide();
        QVERIFY(loading.testAttribute(Qt::WA_ShowWithoutActivating));
        QCOMPARE(nativeWindow->property("_q_showWithoutActivating"), QVariant(false));
        QWidget unrelated;
        unrelated.setObjectName(QStringLiteral("kLoading_dialog"));
        {
            billfish::automation::AppInputScope scope;
            unrelated.show();
        }
        QVERIFY(!unrelated.testAttribute(Qt::WA_ShowWithoutActivating));
    }

    void scopedPluginMenuRespondsToClicksWithoutAQtPopupGrab()
    {
        PluginManager manager(environment_->path());
        const auto error = installFixturePlugin(manager, QStringLiteral("test.menu-command"), QStringLiteral("菜单命令"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        manager.setEnabled(QStringLiteral("test.menu-command"), true);
        QWidget main;
        main.show();
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        Preferences preferences;
        preferences.show();
        {
            billfish::automation::AppInputScope scope;
            ui.showPluginMenu(QPoint(20, 20));
        }
        auto* menu = main.findChild<QMenu*>(QStringLiteral("BillfishPluginMenu"));
        QVERIFY(menu && menu->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        QVERIFY(menu->testAttribute(Qt::WA_ShowWithoutActivating));
        auto* manage = menu->actions().last();
        {
            billfish::automation::AppInputScope scope;
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(manage).center());
        }
        QVERIFY(!menu->isVisible());
        QCOMPARE(preferences.stack->currentWidget()->objectName(), QStringLiteral("BillfishPluginSettingsPanel"));
        QVERIFY(preferences.testAttribute(Qt::WA_ShowWithoutActivating));
        {
            billfish::automation::AppInputScope scope;
            ui.showPluginMenu(QPoint(20, 20));
        }
        menu = nullptr;
        for (auto* candidate : main.findChildren<QMenu*>(QStringLiteral("BillfishPluginMenu")))
            if (candidate->isVisible()) menu = candidate;
        QVERIFY(menu && menu->isVisible());
        QMenu* submenu = nullptr;
        for (auto* action : menu->actions()) if (action->menu()) submenu = action->menu();
        QVERIFY(submenu);
        QVERIFY(submenu->testAttribute(Qt::WA_ShowWithoutActivating));
        // This fixture intentionally has no Electron runtime. Enable the UI
        // branch to exercise menu navigation without starting a plugin process.
        submenu->setEnabled(true);
        {
            billfish::automation::AppInputScope scope;
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(submenu->menuAction()).center());
        }
        QTRY_VERIFY(submenu->isVisible());
        QVERIFY(menu->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        auto* plugins = preferences.findChild<QListWidget*>(QStringLiteral("BillfishPluginList"));
        QVERIFY(plugins);
        plugins->setCurrentRow(-1);
        QVERIFY(!plugins->currentItem());
        auto* command = submenu->actions().first();
        QSignalSpy commandTriggered(command, &QAction::triggered);
        {
            billfish::automation::AppInputScope scope;
            QTest::mouseClick(submenu, Qt::LeftButton, Qt::NoModifier, submenu->actionGeometry(command).center());
        }
        QCOMPARE(commandTriggered.count(), 1);
        QVERIFY(!menu->isVisible());
        QVERIFY(!submenu->isVisible());
        QCOMPARE(preferences.stack->currentWidget()->objectName(), QStringLiteral("BillfishPluginSettingsPanel"));
        QVERIFY(plugins->currentItem());
        QCOMPARE(plugins->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("test.menu-command"));
        auto* commands = preferences.findChild<QComboBox*>(QStringLiteral("BillfishPluginCommands"));
        QVERIFY(commands);
        // Executable commands are exposed only after the runtime is ready.
        // This fixture only declares manifest commands and has no runtime.
        QCOMPARE(commands->count(), 0);
        {
            billfish::automation::AppInputScope scope;
            ui.showPluginMenu(QPoint(20, 20));
        }
        menu = nullptr;
        for (auto* candidate : main.findChildren<QMenu*>(QStringLiteral("BillfishPluginMenu")))
            if (candidate->isVisible()) menu = candidate;
        QVERIFY(menu && menu->isVisible());
        QTest::mouseClick(&main, Qt::LeftButton);
        QVERIFY(!menu->isVisible());
    }

    void allInstalledPluginsRemainReadableAndTheResultFitsTheNativePage()
    {
        PluginManager manager(environment_->path());
        const auto firstError = installFixturePlugin(manager, QStringLiteral("test.first"), QStringLiteral("第一个插件"));
        QVERIFY2(firstError.isEmpty(), qPrintable(firstError));
        const auto secondError = installFixturePlugin(manager, QStringLiteral("test.second"), QStringLiteral("第二个插件"));
        QVERIFY2(secondError.isEmpty(), qPrintable(secondError));
        AutomationUi ui(&manager);
        Preferences preferences;
        preferences.setStyleSheet(QStringLiteral("QWidget {color:#000; background:#202020;} QPushButton {background:white;}"));
        preferences.show();
        QVERIFY(ui.showPluginSettings());
        QApplication::processEvents();
        auto* page = preferences.findChild<QWidget*>(QStringLiteral("BillfishPluginSettingsPanel"));
        auto* list = page->findChild<QListWidget*>(QStringLiteral("BillfishPluginList"));
        auto* result = page->findChild<QPlainTextEdit*>(QStringLiteral("BillfishPluginResult"));
        auto* arguments = page->findChild<QPlainTextEdit*>(QStringLiteral("BillfishPluginArguments"));
        QCOMPARE(list->count(), 2);
        QVERIFY(list->item(1)->text().contains(QStringLiteral("第二个插件")));
        QVERIFY(list->item(1)->foreground().color().lightness() > 160);
        QVERIFY(arguments->palette().color(QPalette::Text).lightness() > 160);
        const auto resultBottom = result->mapTo(page, QPoint(0, result->height())).y();
        QVERIFY2(resultBottom <= page->height(), qPrintable(QStringLiteral("Result bottom %1 exceeds page height %2")
            .arg(resultBottom).arg(page->height())));
        QVERIFY(page->minimumSizeHint().height() <= 480);
    }

    void windowAndServiceModesHideCommandArguments()
    {
        PluginManager manager(environment_->path());
        const auto windowError = installFixturePlugin(manager, QStringLiteral("test.window"), QStringLiteral("窗口插件"), QStringLiteral("window"));
        QVERIFY2(windowError.isEmpty(), qPrintable(windowError));
        const auto serviceError = installFixturePlugin(manager, QStringLiteral("test.service"), QStringLiteral("服务插件"), QStringLiteral("service"));
        QVERIFY2(serviceError.isEmpty(), qPrintable(serviceError));
        const auto commandError = installFixturePlugin(manager, QStringLiteral("test.command"), QStringLiteral("命令插件"));
        QVERIFY2(commandError.isEmpty(), qPrintable(commandError));
        PluginSettingsPanel panel(&manager);
        panel.resize(550, 480);
        panel.show();
        auto* arguments = panel.findChild<QPlainTextEdit*>(QStringLiteral("BillfishPluginArguments"));
        auto* commands = panel.findChild<QComboBox*>(QStringLiteral("BillfishPluginCommands"));
        auto* run = panel.findChild<QPushButton*>(QStringLiteral("BillfishRunPluginCommand"));
        panel.selectPlugin(QStringLiteral("test.window"));
        QVERIFY(arguments->isHidden());
        QVERIFY(commands->isHidden());
        QCOMPARE(run->text(), QStringLiteral("打开插件"));
        panel.selectPlugin(QStringLiteral("test.service"));
        QVERIFY(arguments->isHidden());
        QCOMPARE(run->text(), QStringLiteral("打开界面"));
        panel.selectPlugin(QStringLiteral("test.command"));
        QVERIFY(!arguments->isHidden());
        QVERIFY(!commands->isHidden());
        QCOMPARE(run->text(), QStringLiteral("运行命令"));
    }

    void legacyPythonPluginsRemainVisibleButCannotBeEnabled()
    {
        const auto directory = QDir(environment_->path()).filePath(QStringLiteral("Billfish/automation/plugins/legacy.python"));
        QVERIFY(QDir().mkpath(directory));
        QFile manifest(QDir(directory).filePath(QStringLiteral("manifest.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        const QJsonObject command{{QStringLiteral("id"), QStringLiteral("run")},
            {QStringLiteral("title"), QStringLiteral("Run")},
            {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
        const QJsonObject legacy{{QStringLiteral("apiVersion"), 1}, {QStringLiteral("id"), QStringLiteral("legacy.python")},
            {QStringLiteral("name"), QStringLiteral("旧插件")}, {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("entry"), QStringLiteral("plugin.py")}, {QStringLiteral("permissions"), QJsonArray{}},
            {QStringLiteral("commands"), QJsonArray{command}}};
        QVERIFY(manifest.write(QJsonDocument(legacy).toJson()) > 0);
        manifest.close();
        QFile entry(QDir(directory).filePath(QStringLiteral("plugin.py")));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        QVERIFY(entry.write("# Preserved legacy plugin\n") > 0);
        entry.close();
        PluginManager manager(environment_->path());
        PluginSettingsPanel panel(&manager);
        panel.selectPlugin(QStringLiteral("legacy.python"));
        auto* list = panel.findChild<QListWidget*>(QStringLiteral("BillfishPluginList"));
        QCOMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("不兼容")));
        QVERIFY(!panel.findChild<QCheckBox*>(QStringLiteral("BillfishPluginEnabled"))->isEnabled());
        QVERIFY(panel.findChild<QPushButton*>(QStringLiteral("BillfishRunPluginCommand"))->isHidden());
        QVERIFY(QFile::exists(manifest.fileName()));
        QVERIFY(QFile::exists(entry.fileName()));
    }

    void openingAPluginWindowIsNotReportedAsBusinessCompletion()
    {
        PluginManager manager(environment_->path());
        QWidget main;
        main.resize(900, 600);
        main.show();
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        const QJsonObject opened{{QStringLiteral("opened"), true},
            {QStringLiteral("completion"), QStringLiteral("plugin_window_opened")}};
        QVERIFY(QMetaObject::invokeMethod(&manager, "finished", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("window-request")), Q_ARG(QJsonObject, opened)));
        auto* bar = main.findChild<QProgressBar*>(QStringLiteral("BillfishAutomationProgressBar"));
        auto* message = main.findChild<QLabel*>(QStringLiteral("BillfishAutomationProgressMessage"));
        QCOMPARE(bar->format(), QStringLiteral("已打开"));
        QCOMPARE(message->text(), QStringLiteral("插件窗口已打开"));
    }

    void pluginShortcutKeepsTypingInSearchFields()
    {
        PluginManager manager(environment_->path());
        QWidget main;
        QLineEdit search(&main);
        main.show();
        main.activateWindow();
        search.setFocus();
        QApplication::processEvents();
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        auto* action = main.findChild<QAction*>(QStringLiteral("BillfishPluginMenuAction"));
        QVERIFY(action);
        QCOMPARE(action->shortcut(), QKeySequence(Qt::Key_P));
        QTest::keyClicks(&search, QStringLiteral("p"));
        QApplication::processEvents();
        QCOMPARE(search.text(), QStringLiteral("p"));
        QVERIFY(!main.findChild<QMenu*>(QStringLiteral("BillfishPluginMenu")));
    }

    void sharedPluginActionOpensAMenuWithTheRealManagementEntry()
    {
        PluginManager manager(environment_->path());
        QWidget main;
        main.show();
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        Preferences preferences;
        preferences.show();
        auto* action = main.findChild<QAction*>(QStringLiteral("BillfishPluginMenuAction"));
        QVERIFY(action);
        action->setProperty("billfishPluginMenuPosition", QPoint(20, 20));
        action->trigger();
        auto* menu = main.findChild<QMenu*>(QStringLiteral("BillfishPluginMenu"));
        QVERIFY(menu);
        QVERIFY(!menu->actions().isEmpty());
        auto* manage = menu->actions().last();
        QCOMPARE(manage->text(), QStringLiteral("插件管理..."));
        menu->close();
        manage->trigger();
        QCOMPARE(preferences.stack->currentWidget()->objectName(), QStringLiteral("BillfishPluginSettingsPanel"));
        QVERIFY(!action->property("billfishPluginMenuPosition").isValid());
    }

    void dependencyPackagesOnlyOfferActivationAfterAnUpdate()
    {
        PluginManager manager(environment_->path());
        auto error = installFixturePlugin(manager, QStringLiteral("test.dependency"), QStringLiteral("依赖资源"), QStringLiteral("dependency"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        PluginSettingsPanel panel(&manager);
        panel.resize(550, 480);
        panel.show();
        panel.selectPlugin(QStringLiteral("test.dependency"));
        auto* toggle = panel.findChild<QCheckBox*>(QStringLiteral("BillfishPluginEnabled"));
        auto* run = panel.findChild<QPushButton*>(QStringLiteral("BillfishRunPluginCommand"));
        QVERIFY(toggle->isHidden());
        QVERIFY(run->isHidden());
        error = installFixturePlugin(manager, QStringLiteral("test.dependency"), QStringLiteral("新版依赖资源"), QStringLiteral("dependency"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(toggle->isHidden());
        QVERIFY(!run->isHidden());
        QCOMPARE(run->text(), QStringLiteral("激活更新"));
        QTest::mouseClick(run, Qt::LeftButton);
        QVERIFY(run->isHidden());
        QVERIFY(toggle->isHidden());
    }

    void externalAsynchronousInstallSelectsThePluginWithoutExecutingIt()
    {
        PluginManager manager(environment_->path());
        AutomationUi ui(&manager);
        Preferences preferences;
        preferences.show();
        const auto error = installFixturePlugin(manager, QStringLiteral("test.external-install"), QStringLiteral("下载的插件"), QStringLiteral("window"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto* page = preferences.stack->currentWidget();
        QCOMPARE(page->objectName(), QStringLiteral("BillfishPluginSettingsPanel"));
        auto* list = page->findChild<QListWidget*>(QStringLiteral("BillfishPluginList"));
        QVERIFY(list->currentItem());
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("test.external-install"));
        QVERIFY(!page->findChild<QCheckBox*>(QStringLiteral("BillfishPluginEnabled"))->isChecked());
        QVERIFY(!page->findChild<QPushButton*>(QStringLiteral("BillfishRunPluginCommand"))->isEnabled());
    }

    void pluginPageDropRequestsOneLocalPackageAndMainImportKeepsOrdinaryFiles()
    {
        const auto package = QDir(environment_->path()).filePath(QStringLiteral("download.eagleplugin"));
        QFile file(package);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("package fixture") > 0);
        file.close();
        PluginManager manager(environment_->path());
        PluginSettingsPanel panel(&manager);
        panel.show();
        QSignalSpy requested(&panel, &PluginSettingsPanel::installRequested);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(package)});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&panel, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&panel, &drop);
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().first().toString(), package);
        QWidget main;
        ImportSurface imports(&main);
        main.resize(400, 300);
        main.show();
        AutomationUi ui(&manager);
        ui.setMainWindow(&main);
        QDragEnterEvent pluginEnter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&imports, &pluginEnter);
        QVERIFY(pluginEnter.isAccepted());
        QCOMPARE(imports.entered, 0);
        mime.setUrls({QUrl::fromLocalFile(QDir(environment_->path()).filePath(QStringLiteral("material.png")))});
        QDragEnterEvent imageEnter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&imports, &imageEnter);
        QCOMPARE(imports.entered, 1);
        const auto ordinaryZip = QDir(environment_->path()).filePath(QStringLiteral("material.zip"));
        QFile zip(ordinaryZip);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        QVERIFY(zip.write("ordinary zip fixture") > 0);
        zip.close();
        mime.setUrls({QUrl::fromLocalFile(ordinaryZip)});
        QDragEnterEvent zipEnter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&imports, &zipEnter);
        QCOMPARE(imports.entered, 2);
    }
};

QTEST_MAIN(AutomationUiTests)
#include "test_automation_ui.moc"
