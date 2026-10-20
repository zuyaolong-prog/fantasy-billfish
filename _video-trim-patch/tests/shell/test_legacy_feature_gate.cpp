#include "shell/legacy_feature_gate.hpp"
#include "automation/app_input_scope.hpp"

#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSharedPointer>
#include <QShortcut>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>

class BFStyleMenu : public QMenu { Q_OBJECT public: using QMenu::QMenu; };
class BFFileMenu : public QMenu {
    Q_OBJECT
public:
    using QMenu::QMenu;
signals:
    void SigMoveToCloud();
public slots:
    void SlotMoveToCloud() { emit SigMoveToCloud(); }
};
class BFTreeFolderMenu : public QMenu { Q_OBJECT public: using QMenu::QMenu; };
class DraggableTabWidget : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class LeftMainTitleBar : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class BFAvatarWidget : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
signals:
    void signAvatarClick();
};
class BFSystemTrayNoLoginWidget : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
signals:
    void SigLogin();
};
class PreferencesShortcut : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class PreferencesNotification : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class ShortcutItemLineEdit : public QLineEdit { Q_OBJECT public: using QLineEdit::QLineEdit; };
class BfLocalResWidget : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
signals:
    void sigMoveFilesToCloud(QVector<qint64> ids);
    void localPreviewChanged();
};
class PreviewDetailsWidget : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class BFNormalWebWidget : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    int switchCalls = 0;
    int cleanupCalls = 0;
public slots:
    void slotSwitchToWeb() { ++switchCalls; }
    void cleanup(QObject*) { ++cleanupCalls; }
};
struct CloudUploadData {};
Q_DECLARE_METATYPE(QSharedPointer<CloudUploadData>)
class BFWebWidget : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
signals:
    void signalCloudUpload(QSharedPointer<CloudUploadData> data);
};
class Emitter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void kick();
    void local();
};
class BFItemIconView : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
signals:
    void signalTransferFolderToCloud(QVector<qint64> ids);
    void localChanged();
};

class LegacyFeatureGateTests final : public QObject {
    Q_OBJECT
private slots:
    void scopedMainMenuClickPopulatesAndOpensRecentLibrariesWithoutNativeButtonInput() {
        QWidget window;
        window.resize(500, 300);
        DraggableTabWidget tabs(&window);
        tabs.resize(400, 200);
        LeftMainTitleBar left(&tabs);
        left.resize(100, 50);
        QPushButton button(&left);
        button.setObjectName(QStringLiteral("LeftMainTitleBar_btn_menu"));
        button.setGeometry(5, 5, 30, 30);
        BFStyleMenu main(&window);
        main.addAction(QStringLiteral("创建资源库"));
        QMenu libraries;
        libraries.setTitle(QStringLiteral("打开资源库"));
        main.addMenu(&libraries);
        main.addAction(QStringLiteral("退出"));
        QMenu recent;
        recent.setTitle(QStringLiteral("最近资源库"));
        auto* library = recent.addAction(QStringLiteral("QA library"));
        int libraryCalls = 0;
        int nativeButtonCalls = 0;
        int populationCalls = 0;
        connect(library, &QAction::triggered, &window, [&] { ++libraryCalls; });
        connect(&button, &QPushButton::clicked, &window, [&] { ++nativeButtonCalls; });
        connect(&main, &QMenu::aboutToShow, &window, [&] {
            ++populationCalls;
            libraries.clear();
            libraries.addMenu(&recent);
        });
        billfish::shell::LegacyFeatureGate gate;
        gate.start();
        gate.enforce(&window);
        window.show();
        {
            billfish::automation::AppInputScope scope;
            QTest::mouseClick(&button, Qt::LeftButton);
        }
        QCOMPARE(nativeButtonCalls, 0);
        QCOMPARE(populationCalls, 1);
        QVERIFY(main.isVisible());
        QCOMPARE(main.windowType(), Qt::Tool);
        QVERIFY(!QApplication::activePopupWidget());
        {
            billfish::automation::AppInputScope scope;
            QTest::mouseClick(&main, Qt::LeftButton, Qt::NoModifier, main.actionGeometry(libraries.menuAction()).center());
            QTest::mouseClick(&libraries, Qt::LeftButton, Qt::NoModifier, libraries.actionGeometry(recent.menuAction()).center());
        }
        QVERIFY(main.isVisible() && libraries.isVisible() && recent.isVisible());
        QCOMPARE(recent.windowType(), Qt::Tool);
        QVERIFY(recent.testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(!QApplication::activePopupWidget());
        {
            billfish::automation::AppInputScope scope;
            QTest::mouseClick(&recent, Qt::LeftButton, Qt::NoModifier, recent.actionGeometry(library).center());
        }
        QCOMPARE(libraryCalls, 1);
        QVERIFY(!main.isVisible() && !libraries.isVisible() && !recent.isVisible());
        QCOMPARE(main.windowType(), Qt::Popup);
        QCOMPARE(recent.windowType(), Qt::Popup);
        QVERIFY(!recent.testAttribute(Qt::WA_ShowWithoutActivating));
        QTest::mouseClick(&button, Qt::LeftButton);
        QCOMPARE(nativeButtonCalls, 1);
    }

    void cleansHeaderAndMovesOriginalHistoryWithoutChangingLocalActions() {
        QWidget root;
        DraggableTabWidget tabs(&root);
        LeftMainTitleBar left(&tabs);
        left.setFixedSize(201, 42);
        auto* outer = new QGridLayout(&left);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* contents = new QWidget;
        contents->setObjectName(QStringLiteral("wid_main"));
        contents->setFixedSize(201, 42);
        outer->addWidget(contents);
        auto* leftRow = new QHBoxLayout(contents);
        QList<QLabel*> brands;
        for (const QString& name : {QStringLiteral("lab_logo"), QStringLiteral("lab_name"), QStringLiteral("lab_channel_name")}) {
            auto* brand = new QLabel(QStringLiteral("Billfish"));
            brand->setObjectName(name);
            leftRow->addWidget(brand);
            brands.append(brand);
        }
        leftRow->addStretch();
        auto* menuHost = new QWidget;
        menuHost->setFixedSize(26, 26);
        auto* menuHostRow = new QHBoxLayout(menuHost);
        menuHostRow->setContentsMargins(0, 0, 0, 0);
        auto* menuButton = new QPushButton;
        menuButton->setObjectName(QStringLiteral("LeftMainTitleBar_btn_menu"));
        menuButton->setFixedSize(26, 26);
        menuHostRow->addWidget(menuButton);
        leftRow->addWidget(menuHost);
        QWidget right(&tabs);
        right.setObjectName(QStringLiteral("right_title"));
        right.setFixedSize(226, 42);
        auto* rightRow = new QHBoxLayout(&right);
        QList<QToolButton*> nativeButtons;
        for (const QString& name : {QStringLiteral("BFDockAreaSideButton"), QStringLiteral("BFDockAreaMinimizeButton"),
                QStringLiteral("BFDockAreaMaximizeButton"), QStringLiteral("BFDockAreaCloseButton")}) {
            auto* button = new QToolButton;
            button->setObjectName(name);
            button->setFixedSize(26, 26);
            rightRow->addWidget(button);
            nativeButtons.append(button);
        }
        auto* avatar = new BFAvatarWidget;
        avatar->setObjectName(QStringLiteral("BFDockAreaHeadButton"));
        avatar->setFixedSize(30, 30);
        rightRow->insertWidget(1, avatar);
        auto* divider = new QFrame;
        divider->setObjectName(QStringLiteral("BFDockAreaTitleBarFrame"));
        divider->setFixedSize(1, 20);
        rightRow->insertWidget(2, divider);
        rightRow->insertSpacing(3, 10);
        BFStyleMenu main;
        main.addAction(QStringLiteral("创建资源库"));
        QAction* open = main.addAction(QStringLiteral("打开资源库"));
        main.addSeparator();
        QAction* mainLogin = main.addAction(QStringLiteral("登录账号"));
        main.addAction(QStringLiteral("退出"));
        BFStyleMenu account;
        QObject nativeOwner(&root);
        QAction history(QStringLiteral("导入历史"), &nativeOwner);
        history.setShortcut(QKeySequence(Qt::ALT | Qt::Key_L));
        account.addAction(&history);
        root.addAction(&history);
        account.addSeparator();
        QAction* accountLogin = account.addAction(QStringLiteral("login account"));
        BFSystemTrayNoLoginWidget tray(&root);
        QPushButton trayLogin(QStringLiteral("立即登录"), &tray);
        trayLogin.setObjectName(QStringLiteral("SystemTray_m_login"));
        QPushButton trayLocal(QStringLiteral("显示/隐藏"), &tray);
        trayLocal.setObjectName(QStringLiteral("SystemTray_m_hide_or_show"));
        int historyCalls = 0;
        int loginCalls = 0;
        int localCalls = 0;
        connect(&history, &QAction::triggered, &root, [&] { ++historyCalls; });
        connect(mainLogin, &QAction::triggered, &root, [&] { ++loginCalls; });
        connect(accountLogin, &QAction::triggered, &root, [&] { ++loginCalls; });
        connect(avatar, &BFAvatarWidget::signAvatarClick, &root, [&] { ++loginCalls; });
        connect(&tray, &BFSystemTrayNoLoginWidget::SigLogin, &root, [&] { ++loginCalls; });
        connect(&trayLogin, &QPushButton::clicked, &root, [&] { ++loginCalls; });
        connect(&trayLocal, &QPushButton::clicked, &root, [&] { ++localCalls; });
        root.show();
        billfish::shell::LegacyFeatureGate gate;
        gate.start();
        gate.enforce(&root);
        QTRY_COMPARE(left.width(), 44);
        QTRY_VERIFY(right.width() < 200);
        for (QLabel* brand : brands) QVERIFY(brand->isHidden());
        QVERIFY(avatar->isHidden());
        QVERIFY(divider->isHidden());
        QVERIFY(menuButton->mapTo(&left, QPoint()).x() >= 0);
        QVERIFY(menuButton->mapTo(&left, QPoint(menuButton->width(), 0)).x() <= left.width());
        for (QToolButton* button : nativeButtons) {
            QVERIFY(button->isEnabled());
            QVERIFY(!button->isHidden());
            QVERIFY(button->geometry().right() < right.width());
        }
        QCOMPARE(main.actions().count(&history), 1);
        QCOMPARE(main.actions().at(main.actions().indexOf(open) + 1), &history);
        QVERIFY(!account.actions().contains(&history));
        QVERIFY(root.actions().contains(&history));
        QCOMPARE(history.parent(), &nativeOwner);
        QCOMPARE(history.shortcut(), QKeySequence(Qt::ALT | Qt::Key_L));
        history.trigger();
        QCOMPARE(historyCalls, 1);
        QVERIFY(!mainLogin->isVisible());
        QVERIFY(!accountLogin->isVisible());
        QVERIFY(!trayLogin.isEnabled());
        QVERIFY(trayLogin.isHidden());
        emit mainLogin->triggered(false);
        emit accountLogin->triggered(false);
        emit avatar->signAvatarClick();
        emit tray.SigLogin();
        emit trayLogin.clicked(false);
        QCOMPARE(loginCalls, 0);
        trayLocal.click();
        QCOMPARE(localCalls, 1);
        left.hide(); right.hide();
        left.show(); right.show();
        QTRY_COMPARE(left.width(), 44);
        for (QLabel* brand : brands) QVERIFY(brand->isHidden());
        QVERIFY(avatar->isHidden());
    }

    void removesOnlyKnownEntriesIncludingParentlessActions() {
        QWidget root;
        BFFileMenu files(&root);
        QAction cloud(QStringLiteral("发送至资源云库 (&U)"));
        cloud.setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
        files.addAction(&cloud); // Deliberately parentless, as in the native menu.
        QAction refresh(QStringLiteral("刷新"));
        refresh.setObjectName(QStringLiteral("BFDockAreaRefreshAction"));
        refresh.setShortcut(QKeySequence(Qt::Key_F5));
        files.addAction(&refresh);
        int cloudCalls = 0;
        int refreshCalls = 0;
        connect(&cloud, &QAction::triggered, this, [&] { ++cloudCalls; });
        connect(&refresh, &QAction::triggered, this, [&] { ++refreshCalls; });
        DraggableTabWidget tabs(&root);
        QWidget right(&tabs);
        right.setObjectName(QStringLiteral("right_title"));
        QToolButton plus(&right);
        plus.setObjectName(QStringLiteral("BFDockAreaAddButton"));
        BFStyleMenu dock(&plus);
        dock.setObjectName(QStringLiteral("BFDockAreaRefreshMenu"));
        QAction kuque(QStringLiteral("酷雀工具箱"));
        kuque.setObjectName(QStringLiteral("BFDockAreaRefreshAction"));
        dock.addAction(&kuque);
        QAction inspiration(QStringLiteral("创意灵感"));
        inspiration.setObjectName(QStringLiteral("BFDockAreaForceRefreshAction"));
        dock.addAction(&inspiration);
        plus.setMenu(&dock);
        BFTreeFolderMenu tree(&root);
        QShortcut mnemonic(QKeySequence(Qt::Key_U), &tree);
        QShortcut localMnemonic(QKeySequence(Qt::Key_N), &tree);
        BFStyleMenu main(&root);
        main.addAction(QStringLiteral("创建资源库"));
        QAction* open = main.addAction(QStringLiteral("打开资源库"));
        main.addAction(QStringLiteral("退出"));
        BFStyleMenu libraries;
        open->setMenu(&libraries);
        QAction cloudLibrary(QStringLiteral("Open Cloud Library"));
        QAction personalLibrary(QStringLiteral("游戏设计资料"));
        QAction sameNamedRecentLibrary(QStringLiteral("资源云库"));
        libraries.addAction(&cloudLibrary);
        libraries.addAction(&personalLibrary);
        libraries.addAction(&sameNamedRecentLibrary);
        QMenu userDefinedMenu(&root);
        QAction userDefinedName(QStringLiteral("资源云库"));
        userDefinedMenu.addAction(&userDefinedName);
        BfLocalResWidget localPage(&root);
        PreviewWidget preview(&localPage);
        PreviewDetailsWidget details(&localPage);
        QAction previewCloud(QStringLiteral("发送至资源云库(U)"));
        QAction detailsCloud(QStringLiteral("发送至资源云库(U)"));
        previewCloud.setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
        detailsCloud.setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
        preview.addAction(&previewCloud);
        details.addAction(&detailsCloud);
        QAction localPreviewAction(QStringLiteral("复制备注"));
        localPreviewAction.setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
        preview.addAction(&localPreviewAction);
        PreviewWidget unrelatedPreview(&root);
        QAction unrelatedPreviewCloud(QStringLiteral("发送至资源云库(U)"));
        unrelatedPreview.addAction(&unrelatedPreviewCloud);

        billfish::shell::LegacyFeatureGate gate;
        gate.enforce(&root);
        QVERIFY(!cloud.isVisible());
        QVERIFY(!cloud.isEnabled());
        QVERIFY(cloud.shortcuts().isEmpty());
        cloud.trigger();
        QCOMPARE(cloudCalls, 0);
        QVERIFY(refresh.isVisible());
        QVERIFY(refresh.isEnabled());
        QCOMPARE(refresh.shortcut(), QKeySequence(Qt::Key_F5));
        refresh.trigger();
        QCOMPARE(refreshCalls, 1);
        QVERIFY(!plus.isEnabled());
        QVERIFY(plus.isHidden());
        QVERIFY(!kuque.isVisible());
        QVERIFY(!inspiration.isVisible());
        QVERIFY(!mnemonic.isEnabled());
        QVERIFY(localMnemonic.isEnabled());
        QVERIFY(!cloudLibrary.isVisible());
        QVERIFY(personalLibrary.isVisible());
        QVERIFY(sameNamedRecentLibrary.isVisible());
        QVERIFY(userDefinedName.isVisible());
        QVERIFY(!previewCloud.isVisible());
        QVERIFY(!previewCloud.isEnabled());
        QVERIFY(previewCloud.shortcuts().isEmpty());
        QVERIFY(!detailsCloud.isVisible());
        QVERIFY(!detailsCloud.isEnabled());
        QVERIFY(detailsCloud.shortcuts().isEmpty());
        QVERIFY(localPreviewAction.isEnabled());
        QCOMPARE(localPreviewAction.shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
        QVERIFY(unrelatedPreviewCloud.isEnabled());
        QVERIFY(unrelatedPreviewCloud.isVisible());
    }

    void isolatesNewStylePageConnectionsAndKeepsBothLifetimeDirections() {
        QWidget root;
        auto* sender = new Emitter(&root);
        auto* page = new BFNormalWebWidget(&root);
        page->setObjectName(QStringLiteral("KuQue"));
        auto* cloud = new BFWebWidget(&root);
        cloud->setObjectName(QStringLiteral("BFWebWidget"));
        int lambdaCalls = 0;
        int localCalls = 0;
        int outgoingCalls = 0;
        int pageDestroyed = 0;
        BFItemIconView wrapper(&root);
        int wrapperUploads = 0;
        int wrapperLocalChanges = 0;
        BfLocalResWidget localPage(&root);
        PreviewWidget preview(&localPage);
        int previewUploads = 0;
        int localPreviewChanges = 0;
        connect(&preview, &PreviewWidget::sigMoveFilesToCloud, &root, [&] { ++previewUploads; });
        connect(&preview, &PreviewWidget::localPreviewChanged, &root, [&] { ++localPreviewChanges; });
        connect(&wrapper, &BFItemIconView::signalTransferFolderToCloud, &root, [&] { ++wrapperUploads; });
        connect(&wrapper, &BFItemIconView::localChanged, &root, [&] { ++wrapperLocalChanges; });
        connect(sender, &Emitter::kick, page, &BFNormalWebWidget::slotSwitchToWeb);
        connect(sender, &Emitter::kick, page, [&] { ++lambdaCalls; });
        connect(sender, &Emitter::local, &root, [&] { ++localCalls; });
        connect(sender, &QObject::destroyed, page, &BFNormalWebWidget::cleanup);
        connect(page, &QObject::destroyed, &root, [&] { ++pageDestroyed; });
        connect(cloud, &BFWebWidget::signalCloudUpload, &root, [&] { ++outgoingCalls; });
        billfish::shell::LegacyFeatureGate gate;
        gate.enforce(&root);
        emit sender->kick();
        emit sender->local();
        emit cloud->signalCloudUpload({});
        emit wrapper.signalTransferFolderToCloud({1});
        emit wrapper.localChanged();
        emit preview.sigMoveFilesToCloud({1});
        emit preview.localPreviewChanged();
        QCOMPARE(page->switchCalls, 0);
        QCOMPARE(lambdaCalls, 0);
        QCOMPARE(outgoingCalls, 0);
        QCOMPARE(localCalls, 1);
        QCOMPARE(wrapperUploads, 0);
        QCOMPARE(wrapperLocalChanges, 1);
        QCOMPARE(previewUploads, 0);
        QCOMPARE(localPreviewChanges, 1);
        QVERIFY(!page->isEnabled());
        QVERIFY(page->isHidden());
        delete sender;
        QCOMPARE(page->cleanupCalls, 1);
        delete page;
        QCOMPARE(pageDestroyed, 1);
    }

    void handlesNewActionsAndHidesOnlyRetiredPreferenceRows() {
        QWidget root;
        PreferencesShortcut shortcuts(&root);
        QTableWidget table(2, 2, &shortcuts);
        table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("发送至资源云库")));
        table.setItem(1, 0, new QTableWidgetItem(QStringLiteral("重命名")));
        auto* cloudKey = new ShortcutItemLineEdit;
        cloudKey->setText(QStringLiteral("Ctrl+Shift+U"));
        table.setCellWidget(0, 1, cloudKey);
        auto* localKey = new ShortcutItemLineEdit;
        localKey->setText(QStringLiteral("F2"));
        table.setCellWidget(1, 1, localKey);
        QSignalSpy configurationChanges(cloudKey, &QLineEdit::textChanged);
        PreferencesNotification notifications(&root);
        QWidget cloudNotice(&notifications);
        cloudNotice.setObjectName(QStringLiteral("label_cloud_notification"));
        QWidget localNotice(&notifications);
        localNotice.setObjectName(QStringLiteral("label_import_notification"));
        BFFileMenu files(&root);
        BfLocalResWidget localPage(&root);
        PreviewWidget preview(&localPage);
        billfish::shell::LegacyFeatureGate gate;
        gate.start();
        gate.enforce(&root);
        QCOMPARE(table.rowCount(), 2);
        QVERIFY(table.isRowHidden(0));
        QVERIFY(!table.isRowHidden(1));
        QVERIFY(!cloudKey->isEnabled());
        QVERIFY(cloudKey->text().isEmpty());
        QCOMPARE(configurationChanges.count(), 0);
        QCOMPARE(localKey->text(), QStringLiteral("F2"));
        QVERIFY(localKey->isEnabled());
        QVERIFY(cloudNotice.isHidden());
        QVERIFY(!cloudNotice.isEnabled());
        QVERIFY(localNotice.isEnabled());
        QAction late(QStringLiteral("send to cloud(U)"));
        files.addAction(&late);
        QVERIFY(!late.isVisible());
        QVERIFY(!late.isEnabled());
        QAction latePreview(QStringLiteral("发送至资源云库(U)"));
        preview.addAction(&latePreview);
        QVERIFY(!latePreview.isVisible());
        QVERIFY(!latePreview.isEnabled());
        latePreview.setVisible(true);
        latePreview.setEnabled(true);
        latePreview.setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
        QVERIFY(!latePreview.isVisible());
        QVERIFY(!latePreview.isEnabled());
        QVERIFY(latePreview.shortcuts().isEmpty());
    }
};
QTEST_MAIN(LegacyFeatureGateTests)
#include "test_legacy_feature_gate.moc"
